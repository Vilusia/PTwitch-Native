"""Apply pinned PTwitch engine adaptations, idempotently."""
from pathlib import Path
import hashlib
import shutil

root = Path(__file__).resolve().parents[1]
target = root / 'upstream/komi-tube/vendor/tilefinch/src/fetch/scheduler.inc'
before = '''    if (!success && error != NULL && error[0] != '\\0') {
        snprintf(item->result.error, sizeof(item->result.error), "%s", error);
    }'''
after = '''    if (!success && error != NULL && error[0] != '\\0') {
        /* PTube: error often points at result.error itself. snprintf's
           source and destination must not overlap (glibc erases it). */
        size_t length = strlen(error);
        if (length >= sizeof(item->result.error))
            length = sizeof(item->result.error) - 1u;
        memmove(item->result.error, error, length);
        item->result.error[length] = '\\0';
    }'''
source = target.read_text()
if after in source:
    print('Scheduler error-overlap patch already applied.')
elif source.count(before) == 1:
    target.write_text(source.replace(before, after))
    print('Applied scheduler error-overlap patch.')
else:
    raise SystemExit('Upstream scheduler no longer matches the audited patch')

engine = root / 'upstream/komi-tube/vendor/tilefinch'
hls_header = engine / 'include/tilefinch/media_hls.h'
header_source = hls_header.read_text()
if 'bool delivery_backpressured;' not in header_source:
    anchor = '    bool ended;\n} MediaHlsStats;'
    if header_source.count(anchor) != 1:
        raise SystemExit('HLS stats backpressure seam changed')
    header_source = header_source.replace(anchor,
        '    bool ended;\n    bool delivery_backpressured;\n} MediaHlsStats;')
    hls_header.write_text(header_source)
hls = engine / 'src/media_hls.c'
header_source = hls_header.read_text()
if 'bool media_hls_source_track_views(' not in header_source:
    header_source += '\nbool media_hls_source_track_views(MediaHlsSource *, MediaSampleSource *, MediaSampleSource *);\n'
    hls_header.write_text(header_source)
overlay = root / 'native/engine/media_hls.c'
if (hls.read_bytes() != overlay.read_bytes()
    and hashlib.sha256(hls.read_bytes()).hexdigest() != 'c2da339e7ccbbb30e2479583f6b24decfddbca9c2452c8e3de9a5932a9ea5f02'
    and 'ptwitch_hls_fmp4.inc' not in hls.read_text()):
    raise SystemExit('Upstream HLS source no longer matches the pinned engine')
shutil.copyfile(overlay, hls)
shutil.copyfile(root / 'native/engine/ptwitch_hls_fmp4.inc', engine / 'src/ptwitch_hls_fmp4.inc')
shutil.copyfile(root / 'native/engine/ptwitch_hls_tracks.inc', engine / 'src/ptwitch_hls_tracks.inc')

# Preserve each track's original tfdt origin. The file parser normalizes
# origins independently; live HLS needs the shared clock to retain A/V skew
# across fragments and avoid anchoring playback hours into a broadcast.
mp4 = engine / 'src/media_mp4.c'
source = mp4.read_text()
before = '''        track->info.sample_count = plan->samples;
        track->info.duration = plan->duration;'''
after = '''        track->fragment_decode_origin = plan->decode_time_origin;
        track->have_fragment_decode_origin = plan->have_decode_time_origin;
        track->info.sample_count = plan->samples;
        track->info.duration = plan->duration;'''
if after not in source:
    if source.count(before) != 1:
        raise SystemExit('Upstream MP4 origin seam changed')
    source = source.replace(before, after)
if 'bool media_mp4_fragment_decode_origin(' not in source:
    source += '''
/* PTwitch: raw decode clock for a bounded HLS init+fragment reader. */
bool media_mp4_fragment_decode_origin(const MediaMp4Demux *demux,
                                     size_t index, uint64_t *origin)
{
    if (demux == NULL || origin == NULL || index >= demux->track_count
        || !demux->tracks[index].have_fragment_decode_origin) return false;
    *origin = demux->tracks[index].fragment_decode_origin;
    return true;
}
'''
mp4.write_text(source)
print('Applied PTwitch live fragmented-MP4 adapter.')

backend = engine / 'src/media_backend_psp.c'
source = backend.read_text()
anchor = '#include "media_h264_psp_compat.h"'
if '#include "ptwitch_avc_snapshot.h"' not in source:
    if source.count(anchor) != 1:
        raise SystemExit('Upstream AVC include seam changed')
    source = source.replace(anchor, anchor + '\n#include "ptwitch_avc_snapshot.h"')
anchor = '    backend->last_nal_mode = nal.mode;'
capture = '''    backend->last_nal_mode = nal.mode;
    if (!primed_before) {
        ptwitch_avc_snapshot(nal.sps_buffer, (size_t)nal.sps_size,
            nal.pps_buffer, (size_t)nal.pps_size,
            nal.nal_buffer, (size_t)nal.nal_size, (unsigned)nal.nal_prefix_size,
            backend->decoded_width, backend->decoded_height,
            backend->mpeg_mode, backend->me_boot_type);
    }'''
if capture not in source:
    if source.count(anchor) != 1:
        raise SystemExit('Upstream AVC submission seam changed')
    source = source.replace(anchor,capture)
guarded = '#if defined(PTWITCH_AVC_SNAPSHOT_ENABLED)\n' + capture + '\n#endif'
if guarded not in source:
    source = source.replace(capture,guarded)
backend.write_text(source)
shutil.copyfile(root / 'native/common/ptwitch_avc_snapshot.h',engine / 'src/ptwitch_avc_snapshot.h')
print('Applied bounded AVC startup snapshot diagnostics.')

shutil.copyfile(root / 'native/engine/ptwitch_avc_packet.h', engine / 'src/ptwitch_avc_packet.h')
source = backend.read_text()
if '#include "ptwitch_avc_packet.h"' not in source:
    source = source.replace('#include "ptwitch_avc_snapshot.h"',
        '#include "ptwitch_avc_snapshot.h"\n#include "ptwitch_avc_packet.h"')
before = '        backend->last_packet_bytes = converted_bytes;'
after = '''        /* TS-only firmware compatibility candidate: remove AUD/SEI;
           preserve SPS/PPS and all coded picture slices. */
        size_t picture_bytes = 0;
        if (!ptwitch_ts_avcc_picture(backend->packet_staging,
                                     converted_bytes, &picture_bytes)) {
            psp_media_error(error, error_size, "PSP TS AVC packet invalid");
            return MEDIA_BACKEND_ERROR;
        }
        if (!backend->decoder_primed)
            printf("ptwitch-ts: firmware packet bytes=%u metadata-removed=%u\\n",
                   (unsigned)picture_bytes,(unsigned)(converted_bytes-picture_bytes));
        backend->last_packet_bytes = picture_bytes;'''
if after not in source:
    if source.count(before) != 1:
        raise SystemExit('TS firmware packet seam changed')
    source = source.replace(before, after)
backend.write_text(source)
print('Applied TS firmware packet compatibility candidate.')

# A connection can complete with a TLS/DNS/timeout error before publishing
# HTTP headers. Collect that completion instead of waiting forever for headers.
hls_transport = engine / 'src/psp_media_hls.c'
source = hls_transport.read_text()
before = '''            return MEDIA_HLS_TRANSPORT_WAIT;
        }
    }
    if (fetch_background_transport_take_chunk(
            handle, destination, capacity, length)) {'''
after = '''            if (!progress.complete) return MEDIA_HLS_TRANSPORT_WAIT;
        }
    }
    /* A failed connection may never publish HTTP headers. Retire its
       completed result below, but never expose an unadmitted response body. */
    if (request->headers_admitted && fetch_background_transport_take_chunk(
            handle, destination, capacity, length)) {'''
if after not in source:
    if source.count(before) != 1:
        raise SystemExit('HLS pre-header completion seam changed')
    source = source.replace(before, after)
before = '''    bool success = fetch_background_transport_take_fetch_result_consumed(
        handle, context->budget, result, request->consumed);'''
after = '''    bool collected = fetch_background_transport_take_fetch_result_consumed(
        handle, context->budget, result, request->consumed);
    bool success = collected && request->headers_admitted;'''
if after not in source:
    if source.count(before) != 1:
        raise SystemExit('HLS completion admission seam changed')
    source = source.replace(before, after)
hls_error_log = '''    if (!success) {
        printf("ptwitch-hls: request failed headers=%d http=%ld transport=%ld timeout=%d consumed=%u\\n",
               request->headers_admitted, result->status_code,
               result->transport_code, result->timed_out,
               (unsigned)request->consumed);
    }
'''
anchor = '    fetch_result_free(result);\n    *request = (PspHlsRequest) {0};'
if hls_error_log not in source:
    if source.count(anchor) != 1:
        raise SystemExit('HLS failure diagnostic seam changed')
    source = source.replace(anchor, hls_error_log + anchor)
hls_transport.write_text(source)
print('Applied HLS pre-header failure completion fix.')

source = hls_transport.read_text()
anchor = '''    bool separate = context->source_count == 2u
        && context->source[1] != NULL;'''
after = '''    if (context->source_count == 1u && audio_source != NULL
        && media_hls_source_track_views(context->source[0], video_source, audio_source)) {
        if (has_separate_audio != NULL) *has_separate_audio = true;
        return true;
    }
''' + anchor
if after not in source:
    if source.count(anchor) != 1:
        raise SystemExit('HLS shared track handoff seam changed')
    source = source.replace(anchor,after)
hls_transport.write_text(source)

source = hls_transport.read_text()
anchor = '        stats->active_requests += one.active_requests;'
after = anchor + '''
        /* A mixed single-source TS queue has both tracks resident. A split
           source must not relax buffering on behalf of a missing track. */
        if (context->source_count == 1u)
            stats->delivery_backpressured = one.delivery_backpressured;'''
if after not in source:
    if source.count(anchor) != 1:
        raise SystemExit('HLS aggregate backpressure seam changed')
    source = source.replace(anchor,after)
hls_transport.write_text(source)

opening = engine / 'src/psp_media_open.c'
source = opening.read_text()
before = '''                        uint8_t profile = 0;
                        MediaH264DecoderRoute route =
                            media_h264_annexb_decoder_route(
                                video.codec_config,
                                video.codec_config_length, &profile);'''
after = '''                        uint8_t profile = 0;
                        unsigned constraints = 0, level = 0;
                        MediaH264DecoderRoute route = ptwitch_hls_video_codec(
                            &video, &profile, &constraints, &level);'''
if after not in source:
    if source.count(before) != 1:
        raise SystemExit('Upstream HLS codec route seam changed')
    source = source.replace(before,after)
    start = source.index('                        const unsigned char *sps = NULL, *pps = NULL;',source.index(after))
    end = source.index('                        snprintf(',start)
    source = source[:start] + source[end:]
    source = source.replace('''                            "video/mp2t; codecs=avc1.%02x%02x%02x,"
                            "mp4a.40.2", (unsigned) profile,''','''                            "%s; codecs=avc1.%02x%02x%02x,"
                            "mp4a.40.2", video.packet_format == MEDIA_PACKET_FORMAT_CONTAINER_NATIVE
                                ? "video/mp4" : "video/mp2t", (unsigned) profile,''')
    source = source.replace('#include "psp_media_session_internal.h"',
        '#include "psp_media_session_internal.h"\n#include "ptwitch_hls_codec.h"')
opening.write_text(source)
shutil.copyfile(root / 'native/engine/ptwitch_hls_codec.h',engine / 'src/ptwitch_hls_codec.h')
print('Applied container-aware HLS decoder routing.')

policy = engine / 'src/media_backend_psp_policy.h'
source = policy.read_text()
if 'uint64_t network_target_cap_us;' not in source:
    anchor = '    uint64_t network_ahead_us;'
    if source.count(anchor) != 1:
        raise SystemExit('Buffer policy input seam changed')
    source = source.replace(anchor,anchor+'\n    uint64_t network_target_cap_us;')
    anchor = '    if (input.remaining_us != 0 && decision.target_us > input.remaining_us)'
    if source.count(anchor) != 1:
        raise SystemExit('Buffer policy target seam changed')
    source = source.replace(anchor,'''    if (input.network_target_cap_us != 0
        && decision.target_us > input.network_target_cap_us)
        decision.target_us = input.network_target_cap_us;
'''+anchor)
policy.write_text(source)
buffering = engine / 'src/psp_media_buffering.c'
source = buffering.read_text()
if '#include "ptwitch_hls_buffer_policy.h"' not in source:
    source = source.replace('#include "media_backend_psp_policy.h"',
        '#include "media_backend_psp_policy.h"\n#include "ptwitch_hls_buffer_policy.h"')
    anchor = '    policy_input.network_ahead_us = network_ahead_us;'
    if source.count(anchor) != 1:
        raise SystemExit('HLS resume cap seam changed')
    source = source.replace(anchor,anchor+'''
    if (media->hls != NULL)
        policy_input.network_target_cap_us = ptwitch_hls_resume_cap(
            network_ahead_us, fill_pending, PSP_MEDIA_BUFFER_TARGET_US);''')
buffering.write_text(source)
source = buffering.read_text()
before = '''    if (media->hls != NULL)
        policy_input.network_target_cap_us = ptwitch_hls_resume_cap(
            network_ahead_us, fill_pending, PSP_MEDIA_BUFFER_TARGET_US);'''
after = '''    if (media->hls != NULL) {
        MediaHlsStats delivery = {0};
        bool blocked = psp_media_hls_stats(media->hls, &delivery)
            && delivery.delivery_backpressured;
        /* A pending request cannot fill a queue the producer has deliberately
           stopped draining. Consume its resident runway to release space. */
        policy_input.network_target_cap_us = ptwitch_hls_resume_cap(
            network_ahead_us, fill_pending && !blocked, PSP_MEDIA_BUFFER_TARGET_US);
    }'''
if after not in source:
    if source.count(before) != 1:
        raise SystemExit('HLS backpressured resume seam changed')
    source = source.replace(before,after)
buffering.write_text(source)
shutil.copyfile(root / 'native/engine/ptwitch_hls_buffer_policy.h',engine / 'src/ptwitch_hls_buffer_policy.h')
print('Applied bounded HLS resume target.')

# Live HLS bodies can pause longer than page-resource traffic on PSP Wi-Fi.
# Give background requests a 12-second no-progress window; the per-request hard
# deadline (30 seconds for HLS) remains the upper bound. Blocking fetches keep
# their existing six-second low-speed policy.
background = engine / 'src/fetch/background_transport.inc'
source = background.read_text()
before_define = '#define FETCH_BACKGROUND_REFERER_CAPACITY 2048u'
after_define = before_define + '\n#define FETCH_BACKGROUND_STALL_TIMEOUT_SECONDS 12u'
if after_define not in source:
    if 'FETCH_BACKGROUND_STALL_TIMEOUT_SECONDS' in source:
        raise SystemExit('Background transport stall timeout was changed')
    if source.count(before_define) != 1:
        raise SystemExit('Background transport stall-timeout seam changed')
    source = source.replace(before_define, after_define)
before_use = '>= (uint64_t) FETCH_STALL_LOW_SPEED_SECONDS'
after_use = '>= (uint64_t) FETCH_BACKGROUND_STALL_TIMEOUT_SECONDS'
if after_use not in source:
    if source.count(before_use) != 1:
        raise SystemExit('Background transport stall check changed')
    source = source.replace(before_use, after_use)
background.write_text(source)
print('Applied 12-second background no-progress grace.')

# The attached Build 11 logs ended before worker initialization completed, so
# record that synchronous setup path in the always-flushed PTwitch app log.
# These are stage markers only; they do not log URLs, certificates or tokens.
def patch_once(path, before, after, description):
    text = path.read_text()
    if after in text:
        return
    if text.count(before) != 1:
        raise SystemExit(description + ' seam changed')
    path.write_text(text.replace(before, after))


response_stream = engine / 'src/fetch/response_stream.inc'
patch_once(
    response_stream,
    "static bool curl_global_acquire(Budget *budget)\n{\n    curl_global_error[0] = '\\0';",
    "static bool curl_global_acquire(Budget *budget)\n{\n"
    "#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n"
    "    printf(\"ptwitch-init: curl-global enter\\n\");\n"
    "#endif\n    curl_global_error[0] = '\\0';",
    'PTwitch curl-global entry diagnostic')
patch_once(
    response_stream,
    '        if (!budget_concurrent_pool_init(\n',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '        printf("ptwitch-init: curl-pool begin\\n");\n'
    '#endif\n        if (!budget_concurrent_pool_init(\n',
    'PTwitch curl-pool diagnostic')
patch_once(
    response_stream,
    '        curl_budget = budget;\n        CURLcode code = curl_global_init_mem(',
    '        curl_budget = budget;\n'
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '        printf("ptwitch-init: curl-global-init begin\\n");\n'
    '#endif\n        CURLcode code = curl_global_init_mem(',
    'PTwitch curl-global-init diagnostic')
patch_once(
    response_stream,
    '        curl_shared_state = curl_share_init();',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '        printf("ptwitch-init: curl-global-init ready\\n");\n'
    '        printf("ptwitch-init: curl-share begin\\n");\n'
    '#endif\n        curl_shared_state = curl_share_init();',
    'PTwitch curl-share diagnostic')
patch_once(
    response_stream,
    '        curl_shared_state = curl_share_init();',
    '        curl_shared_state = curl_share_init();\n'
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '        printf("ptwitch-init: curl-share returned\\n");\n'
    '#endif',
    'PTwitch curl-share completion diagnostic')
patch_once(
    response_stream,
    '        fetch_tls_sessions_import();',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '        printf("ptwitch-init: tls-cache import begin\\n");\n'
    '#endif\n        fetch_tls_sessions_import();\n'
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '        printf("ptwitch-init: tls-cache import done\\n");\n'
    '#endif',
    'PTwitch TLS-cache diagnostic')

background = engine / 'src/fetch/background_transport.inc'
patch_once(
    background,
    '    if (budget == NULL) return false;\n    if (!curl_global_acquire(budget)) {',
    '    if (budget == NULL) return false;\n'
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '    printf("ptwitch-init: transport enter\\n");\n'
    '    printf("ptwitch-init: transport curl-global begin\\n");\n'
    '#endif\n    if (!curl_global_acquire(budget)) {',
    'PTwitch transport-entry diagnostic')
patch_once(
    background,
    '    const char *failure_stage = "unknown";\n',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '    printf("ptwitch-init: transport curl-global ready\\n");\n'
    '#endif\n    const char *failure_stage = "unknown";\n',
    'PTwitch transport-global diagnostic')
patch_once(
    background,
    '    if (!budget_reservation_acquire(\n',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '    printf("ptwitch-init: stack reservation begin\\n");\n'
    '#endif\n    if (!budget_reservation_acquire(\n',
    'PTwitch stack-reservation diagnostic')
patch_once(
    background,
    '    fetch_background.multi = curl_multi_init();',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '    printf("ptwitch-init: stack reservation ready\\n");\n'
    '    printf("ptwitch-init: curl-multi begin\\n");\n'
    '#endif\n    fetch_background.multi = curl_multi_init();',
    'PTwitch curl-multi diagnostic')
patch_once(
    background,
    '    fetch_background.event = sceKernelCreateEventFlag(',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '    printf("ptwitch-init: curl-multi ready\\n");\n'
    '    printf("ptwitch-init: event-create begin\\n");\n'
    '#endif\n    fetch_background.event = sceKernelCreateEventFlag(',
    'PTwitch event diagnostic')
patch_once(
    background,
    '    fetch_background.thread = sceKernelCreateThread(',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '    printf("ptwitch-init: event-create ready\\n");\n'
    '    printf("ptwitch-init: thread-create begin\\n");\n'
    '#endif\n    fetch_background.thread = sceKernelCreateThread(',
    'PTwitch thread-create diagnostic')
patch_once(
    background,
    '    failure_code = sceKernelStartThread(fetch_background.thread, 0, NULL);',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '    printf("ptwitch-init: thread-create ready\\n");\n'
    '    printf("ptwitch-init: thread-start begin\\n");\n'
    '#endif\n    failure_code = sceKernelStartThread(fetch_background.thread, 0, NULL);',
    'PTwitch thread-start diagnostic')
patch_once(
    background,
    '    fetch_background.initialized = true;\n',
    '#if defined(TILEFINCH_PSP_OWNED_TRANSPORT)\n'
    '    printf("ptwitch-init: thread-start ready\\n");\n'
    '#endif\n    fetch_background.initialized = true;\n',
    'PTwitch worker-ready diagnostic')
print('Applied PTwitch network initialization stage diagnostics.')
