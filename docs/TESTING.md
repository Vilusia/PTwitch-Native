# PTwitch validation — 2026-11-11

## Build 19 TS reserve (current source)

The uploaded build 18 log `ptwitch(5).log` plays stopsigncam at 640x360 for
over a minute, advances playlists and segments, and recovers from buffering.
Its ordinary network runway is roughly 198-359 ms; another paused sample
shows 458 ms. This is still a short reserve, not proof that Wi-Fi bandwidth
is intrinsically insufficient: the producer's 17-entry cutoff constrains how
much the transport may publish.

Build 19 raises the fixed descriptor array from 64 to 256 entries and the
TS proactive-fill cutoff from 16 to 160 (leaving 95 descriptors beyond the
cutoff for parser callbacks). MP4 keeps a logical 64-sample limit and its
existing fill cutoff. The compressed-payload caps and all Budget ceilings
remain unchanged; large-payload streams can still reach byte backpressure
before reaching the sample cutoff, and still use the build 18 resume escape.

Acceptance: a six-second-segment fixture with 30 fps video and 48 kHz AAC
delivers all 924 samples, builds at least two seconds of runway while held,
then satisfies the actual buffer resume policy with a request still active.
A video-only fixture also resumes and drains all 420 samples. The reserve
regression fails on build 18 with 600,000 us held and passes with build 19.
Memory-refusal, cancellation, source cleanup, fMP4, and lifecycle tests pass.

Memory comparison against build 18 (optimized 64-bit isolated host harness):
ordinary TS peak increases from 1,190,414 to 1,199,638 bytes; muxed MP4 peak
from 5,456,898 to 5,466,122 bytes: 9,224 additional bytes in either fixture.
All source-owned Budget bytes return at teardown. This is host allocation
evidence, not a PSP memory or timing measurement; hardware verification is
still needed before treating the reserve as a proven playback improvement.
Frontend syntax and package identity checks also pass. The PSP build attempt
remains blocked before compilation by WSL path conversion exit -1 here.

## Build 18 TS delivery and buffering resume (previous source)

The uploaded build 17 log confirms stopsigncam's first 640x360 frame at
6,664 ms after stripping 27 metadata bytes from the first AVC packet. It then
buffers with 17 samples queued, 452 ms ahead, and media bytes frozen at
87,153. The worker's perform/poll counters continue advancing, while body
callbacks stop. The source pump deliberately stops at more than 16 queued
samples, but the pending segment prevents the old resume cap from using that
short resident queue: producer and consumer wait on each other.

Build 18 reports TS delivery backpressure separately from active network
requests. For a single mixed HLS source, the buffering policy may use its
resident tail when the queue itself prevents further delivery; requests stay
active and ordinary decode-ready/stability checks remain enforced. Split
sources do not relax their target based on another track's queue. Queue and
allocation limits are unchanged.

The TS proactive pump previously processed only one 188-byte packet per
display frame. It now takes up to 128 bounded packet steps, stopping on the
existing queue/payload headroom checks, failure, end, or transport WAIT. MP4
keeps its single-step path. This removes an artificial throughput ceiling
without enlarging a parser's individual slice.

Host regressions reproduce a held TS queue with an outstanding segment and
show it can resume after the change. A paced TS fixture includes twelve null
packets between pictures: its frame-step ceiling fails with the one-packet
baseline (see test output) and passes with batching. The pre-fix paused-queue
case also failed before the backpressure change. The complete HLS/MP4/TS and
lifecycle suites pass, including memory refusal and cancellation.
Frontend and PSP buffering-source syntax checks and package identity checks
also pass. The real PSP build attempt is blocked before compilation by WSL
path conversion exit -1 in this session. Device playback is not yet verified.

## Build 17 network progress diagnostics (previous source)

The uploaded build 16 log successfully fetches a 640x360 TS playlist, then
reports zero media bytes and one active request through 40 seconds. It ends
with the engine's opening timeout in resolve. No IDR, packet-normalization or
decoder call appears, so this run does not test the build 16 AVC candidate.
The uploaded build 13/15 captures are older; their names do not identify a new
build 16 decode attempt.

Build 17 adds read-only worker metrics to the existing five-second playback
reports and three-second cleanup reports: queue/running/completion counts,
perform/poll/header/body counters, cancelled retirement counts, and setup/
service/poll maximum durations. It logs no URLs or access tokens and changes
no request limits, decoder policy or connection behavior. This is diagnostic
instrumentation, not a claimed fix for the zero-byte transfer.

## Build 16 TS firmware packet candidate (previous source)

The build 15 log confirms both 640x360 and 284x160 MPEG-TS renditions reach
an IDR, then fail at `sceMpegAvcDecode` with `0x80628002`. Their uploaded
startup captures both decode without errors using FFmpeg 9.0.2's bundled
Windows decoder (imageio-ffmpeg 0.8.1). Main profile, CABAC, progressive
frames, POC type 2 and three reference frames are present. Resolution alone
does not explain the failure. The packets contain NAL types 9,7,8,6,5;
the earlier successful fMP4 capture submitted 7,8,5.

Build 16 is a compatibility candidate, not a confirmed firmware fix. Only
TS-converted AVCC staging packets drop AUD (9) and SEI (6). SPS/PPS and every
picture slice remain intact, with the demux retaining frame timestamps.
Native MP4 packets do not enter this filter. The filter validates its entire
bounded packet before in-place compaction and rejects malformed/no-picture
input without partial mutation.

The real C helper was run on both uploaded packets: each drops exactly 27
bytes, yielding NALs 7,8,5. Original versus filtered software decode produces
identical frame MD5s: 360p `6ea49848c1ed369ebbfd9e5a745d52c5`, 160p
`87b62cc020c119a861896d9d729d6caa`. Every picture-slice byte also compares
equal. A host regression fails with the original pass-through behavior, then
passes with filtering; it covers multiple slices and malformed/no-picture
packets. HLS integration, memory and lifecycle checks pass. Firmware behavior
still requires a real PSP build and device test. Package identity checks pass;
the PSP build attempt stops at WSL path conversion (exit -1) before compiling
in this session.

## Build 15 connection retirement (previous source)

The user reports repeated "network still closing" failures during stream
selection. The uploaded build 14 logs stop at the first Contacting Twitch
request and do not record this cleanup incident. The app's cleanup guard
previously allowed only three seconds for cancellation, shorter than its
10-second connection timeout, and subsequent attempts immediately refused
while retirement remained incomplete.

Build 15 waits responsively for up to 15 seconds, logs elapsed cleanup time
and active leases every three seconds, and resumes bounded cleanup on a later
channel attempt. It never force-frees a worker or reuses an active slot. This
is a recovery change, not proof of the underlying slow connection's cause.
A host regression using the actual frontend cleanup function fails before
the change for a 4.2-second cancellation. The regression also checks a stuck
worker remains protected, later cleanup succeeds, and cancellation persists.
The regression and HLS integration/lifecycle suites pass, as do frontend
syntax and package identity checks. The PSP build attempt stops before
compilation because WSL path conversion returns exit -1 in this session.
Real PSP compilation and behavior remain unverified for this build.

## Build 14 HLS pre-header error recovery (previous source)

The current build 13 device log selects 640x360 TS, receives its first IDR,
and fails in `sceMpegAvcDecode` with `0x80628002`. Its automatic 284x160 TS
fallback receives no media bytes and ends in the generic opening timeout.
This is not evidence of missing lower resolutions.

The HLS transport poll returned WAIT whenever headers were unavailable, even
when the worker had completed with a connection error before HTTP headers.
Build 14 collects that terminal result, exposes the transport error to the
existing bounded live retry path, and keeps unadmitted bodies inaccessible.
It preserves the decoder and rendition limits. The 360p decoder refusal is
unresolved; this change targets recovery of the lower-quality attempt.

A host regression compiles the actual engine poll function with worker
responses. It failed on the pre-fix source (completed pre-header timeout
returned WAIT), then passed with the fix. Pending requests, unadmitted
completion, and admitted body draining are also checked. The HLS/MP4/TS and
media lifecycle host suites pass, as do the core Twitch parser tests.
The PSP build was attempted but WSL path conversion returned exit -1 in this
session, before compilation. Hardware playback still requires validation.
Failed HLS completions log HTTP/transport status, timeout, admitted-header
state, and consumed byte count without URLs. AVC captures now use the current
build number; the snapshot test checks those filenames rather than old files.

## Build 13 TS AVC capture (previous source)

Build 12 device logs confirm the small-channel attempts advertised 640x360
MPEG-TS and yielded an IDR, but the PSP decoder refused the first AVC access
unit with `0x80628002`. A lower TS rendition also failed in a separate attempt.
The large-channel test instead provided 640x360 fMP4 and displayed a frame.
The issue is therefore the delivered AVC/container path, not missing resolution
metadata or Twitch request access.

Build 13 temporarily enables the existing bounded AVC snapshot helper for the
first two startup access units. It writes `ptwitch-avc-13-{0,1}.h264` plus
matching `.txt` SPS/PPS and NAL metadata; no signed URLs or tokens are captured.
Test the failing small streamer first after launching PTwitch so the limited
capture slots are used on the TS failure. This is a diagnostic build and should
not be treated as a playback fix. Remove the capture files after diagnosis.

## Build 12 network initialization diagnostics (current source)

The received Build 11 app logs both end at `request begin` after Wi-Fi reports
connected. Neither contains `network worker initialized`, a queued request, or
an HTTP/TLS result. Validation logs are empty and the crash records remain at
their initial `armed` marker. This does not show a failed certificate check:
the request has not yet been queued. The synchronous
`fetch_background_transport_initialize()` call between those log points is the
remaining unobserved boundary.

Build 12 adds always-flushed stage markers around curl global initialization,
TLS session-cache import, budget stack reservation, curl multi creation, event
creation, and worker-thread start. No trust roots or TLS verification rules are
changed. The frontend/package checks pass; a PSP build and a new device capture
are required to identify which stage stalls.

## Build 11 stall tolerance and 360p preference (previous source)

The received build 10 log confirms fMP4 playback at 284x160 for 495 seconds.
During playback, the source repeatedly starved; the final report shows the HLS
media byte count unchanged at 12,649,858 bytes while one transfer remained
active and the buffer was empty. The request then failed with
`background response made no progress for six seconds`. Playlist refreshes had
zero failures. This identifies a stalled media response, not a Twitch denial or
decoder failure. The captured log ends at the playback failure, before recording
the subsequent transport-cleanup result.

Build 11 raises the background transport's no-progress grace to 12 seconds while
preserving the 30-second per-HLS-request deadline. The blocking transport's
six-second watchdog is unchanged. Rendition selection now tries the highest
compatible resolution up to 360p, preferring fMP4 among same-resolution
candidates; firmware startup refusal still retries lower qualities. Core tests
cover 160p versus 360p and 240p versus 360p selection. These changes need a fresh
PSP cross-build and sustained device test; host checks cannot verify Wi-Fi
stall recovery or 360p firmware decoding.

## Build 10 fMP4 preference (previous source)

The received build 9 logs show Twitch GraphQL, master-playlist, and media-playlist
requests returning HTTP 200. The selected MPEG-TS rendition then failed on its
first AVC access unit with PSP firmware error `0x80628002`; transport cleanup
reported `retired=1 active=0`. A later current log ends at `request begin` and
does not contain a response or worker-initialization completion, so it cannot
establish whether that attempt was still in progress or stuck.

The resolver now retains the first compatible MPEG-TS candidate but continues
through at most eight candidates looking for fMP4, selecting the first fMP4
candidate if available. TS remains a fallback when no fMP4 candidate is found.
This aims to use the container that previously reached hardware playback; it
does not prove the TS decoder refusal is universally container-caused. The
frontend passes a clang syntax-only check. A fresh PSP build/install and device
test are still required; no fix for the incomplete latest network-init attempt
is claimed.

## Build 9 live buffering and reconnect (previous source)

The received build 8 log proves audio/video playback: first frame in 4448 ms,
284x160 Main profile, mode 4 / ME type 3, followed by buffering at clock 6015666
us. The UI continued to report playing with the old frame. A SELECT reconnect
remained in opening; returning to the menu and reopening reached another IDR.

The bounded HLS sample queue previously defined the entire network horizon,
even with complete fMP4 fragments resident. Its proactive pump stops near 24
descriptors, which can fall below the two-second buffering resume target.
Full validated-fragment duration and fully received speculative successors now
contribute to the network horizon. Partial successors do not. A completed
transport handle retained for sample ownership is no longer counted as a live
network operation. Decode queues and all byte ceilings remain bounded.
HLS resume targets are capped at the normal target instead of growing to an
unreachable repeated-stall target. When all network work has retired, a shorter
resident tail caps the target so consuming it can release the next fragment.
Other media retain the existing policy. A dedicated test checks the old repeat
policy waits on a 1.5-second completed tail and the HLS cap permits resume.

The successor can come from a pending live snapshot while the current fragment
is backpressured. Speculative identity is tracked by media sequence and adopted
only after the snapshot advances; stale bytes are discarded. This allows
playlist refresh/refill to continue while the player's clock is held.

The host integration harness pauses consumption for 200 delivery visits. It
checks at least four seconds resident with no pending data request, then calls
the actual PSP buffer policy and verifies that it permits resume. It separately
checks a one-byte transport whose partial successor must not count as a full
fragment, and a live refresh adding the next fragment while consumption is held.
All packets subsequently drain without leaks. The unchanged upstream media
state suite, including randomized lifecycle transitions and dormant/reclaim
checks, also runs and passes in the host DLL.

The frontend's retained-frame test no longer resets its buffering timeout.
The runtime now uses `psp_media_reclaim_hidden_pipeline` so destroying a hidden
retained player completes its state-machine ownership transition before an
immediate reconnect. HLS delivery counters are logged during playback as well
as opening. The runtime facade change and sustained playback/reconnect still
require a device test; the host state suite does not emulate firmware teardown.
The paused-consumer assertion fails when resident-fragment accounting is disabled
and passes with it enabled. Routine raw AVC capture is now compile-gated off
(`PTWITCH_AVC_SNAPSHOT_ENABLED`) to avoid opening-time Memory Stick writes during
normal playback. Existing capture files are left untouched.

## Build 8 timeout cleanup and transport reuse (previous source)

The copied build 7 log records successful token/master/preflight downloads,
then `VIDEO OPEN TIMED OUT IN resolve` after about 45 seconds. It contains no
subsequent contacting attempts, so it cannot establish the later worker state
or identify which HLS resource stalled. Resolution/decoder errors are not
reported in this run.

The frontend previously left a failed stream attached while displaying its
error. It now exits the playback pump, closes the pipeline, cancels all
standalone transport leases and waits up to three seconds for retirement before
displaying an idle error/retry screen. Metadata connection failures use the same
retirement step before a retry. Media priority is cleared; no worker is killed
or reused while it still owns operations. A failed retirement blocks new work
until it completes and gives a restart message. The next request uses the
transport's existing force-fresh-connection option to avoid a stale connection.

HLS opening reports playlist/media bytes, segment progress and active operations.
Init downloads now count as active HLS requests. fMP4 successor prefetch waits
for the first fragment's track metadata, reducing competing startup requests.
The host transport harness asserts that no successor starts while the first
fragment request is still active, in addition to existing payload, timing and
cancellation checks. All integration cases pass with this admission rule.

These checks do not emulate PSP curl/worker retirement. Actual teardown after
timeout, subsequent successful requests and the original opening stall still
require hardware testing. The change fixes the visible lifecycle gap without
claiming to diagnose an underlying Wi-Fi/TLS/CDN failure.

## Build 7 HLS codec handoff (previous source)

The build 6 device log now confirms fragmented MP4, profile 77, a real startup
IDR and successful HTTP 200 responses before `HLS carries unsupported AVC
syntax`. The earlier inference that a missing marker might indicate TS was
not established; this new evidence identifies the current failing stage.

Inspection found `psp_media_open.c` unconditionally calling the Annex-B codec
router and parameter-set extractor for every HLS source. fMP4 returns avcC,
which that router correctly rejects. The opening path now dispatches by
`packet_format` through `ptwitch_hls_codec.h` for decoder routing and codec
flags, and reports video/mp4 for native MP4 packets. TS retains its Annex-B
route. The host harness runs this exact shared routing helper after real HLS
priming, including Baseline and Main/constraint-flag fixtures, and demonstrates
that the old Annex-B route refuses those same valid avcC configurations.

Separate request failures show HTTP 0 and no body, not an HTTP denial from
Twitch. The source now preserves transport error text/timeout state and permits
one retry for pre-response failures. Back remains cancellable; actual HTTP
denials, malformed responses and worker-admission failures are not retried.
PSP network reliability, the firmware's next-stage result and sustained playback
remain unverified. This fixes the demonstrated opening-format mismatch, not
a proven universal explanation of the earlier 0x80628002 hardware failures.

## Build 6 TS startup and first-AU evidence (previous source)

The new device log confirms build 5 selected 284x160, 230000 bps, HTTP 200 at
each request, and still received 0x80628002 before a video frame. There is no
fMP4 startup marker despite that marker being present in the compiled EBOOT.
That suggests the rendition may have used TS; it does not prove its container.
Build 6 explicitly logs the container at preflight.

The TS callback now drops leading dependent video pictures and retains AAC
metadata while discarding leading audio until an actual video IDR. The same
300-skipped-picture bound applies as in fMP4. The test harness feeds real TS
PAT/PMT/PES packets through the real TS/HLS parsers. A normal two-segment fixture
delivers four video samples; a fixture with a leading P picture delivers three
and starts on the following IDR. Existing MP4/refresh/cancellation tests pass.
The TS leading-picture assertion fails with the new TS gate disabled (four
samples instead of three) and passes when enabled. Frontend syntax/API,
core selector, package-version and identity checks pass. The PSP cross-build
attempt is still blocked at WSL path conversion, before compilation.

Before the first raw-NAL firmware submission, a diagnostic hook saves at most
two access units as Annex-B H.264 with their SPS/PPS and a separate metadata
file. It validates all lengths, bounds writes to 512 KiB per AU, and records
decoder mode/boot, dimensions, NAL prefix and NAL types. No signed URL, playback
token or account data is included. The portable host test verifies exact output
bytes, including all NALs, and metadata. This is an opening-only diagnostic and
adds Memory Stick writes; it is not a throughput qualification build.

The diagnostic files have not yet been obtained from hardware. They are intended
to distinguish a bad/unsupported bitstream from a firmware-bridge problem;
neither is established yet. Build 6 is not claimed to resolve the device error.

## Build 5 decoder startup correction (previous source)

The received diagnostics-1 device log records HTTP 200 for token/master/media
requests and 640x360 at 630000 bps, followed by first-picture firmware refusal
`PSP AVC sceMpegAvcDecode failed: 0x80628002`. This confirms delivery progressed
to decode; it does not distinguish missing reference pictures from an
unsupported bitstream or firmware program. The engine policy documents
first-prime failures of its wide decoder path on real devices.

The selector now prefers renditions within 480x272; 360p remains available
when no smaller compatible rendition exists. The frontend closes and retries
a lower available quality after an initial AVC refusal. The fMP4 adapter now
waits for actual NAL type 5 (IDR), drops leading dependent video/audio, and
rebases the initial timeline to that entry point. Container sync flags alone
cannot prime a fresh decoder. A finite source without an IDR fails explicitly;
live startup also has a 300 skipped-video-sample ceiling. A startup diagnostic
records IDR size, skipped picture count, geometry, profile and NAL prefix.

The host integration test includes a leading non-IDR incorrectly marked sync,
followed by an actual IDR not marked sync. It checks that the former is dropped,
the latter becomes the first video at DTS zero, and A/V delivery continues.
The core selector tests verify the new 240p preference, while 360p-only input
continues to select 360p. Frontend syntax/API and package marker/identity checks
pass. Firmware playback and automatic quality retry still require a PSP test.
Disabling only the IDR startup gate makes the leading-picture regression fail
(360 samples delivered instead of 357); restoring it passes. The build 5 PSP
cross-build attempt remains blocked at WSL path conversion (exit -1).

## Build 4 live fragmented MP4 (historical)

Diagnostic revision 1: the received device log confirms build 4 and successful
Wi-Fi setup but contains no request/decoder messages; its validation log is
empty. Engine printf output now also goes to the flushed, query-redacted main
log. Request initialization/enqueue and periodic playback status are recorded.
This change adds evidence collection, not a confirmed fix for the reported
opening stall. The frontend syntax/API check and package-version checks pass.

Added an HLS adapter around the engine's existing fragmented MP4 demuxer.
It retains initialization maps through both playlist parsers and oversized
live-window compaction, obtains the init resource, and parses bounded media
fragments in memory. It retains original per-track tfdt origins, normalizes
the broadcast clock once per discontinuity, and preserves separate DTS/PTS
and AAC/video skew. Codec configuration is retained separately from each
fragment's demuxer and compared before changes can reach the firmware path.
An optional second fragment buffer overlaps downloads with playback. Each
sample pump yields after eight descriptors and waits for queue space.

`scripts/test-hls.py` compiles the real HLS, MP4, H.264 admission, TS and URL
sources as a Windows DLL using clang/lld and Windows UCRT. The harness replaces
only network delivery and the browser budget allocator with isolated bounded
test implementations. It does not emulate the PSP backend or decode pixels.
The constructed muxed fixtures contain 70 AVC and 110 AAC samples per fragment,
large tfdt values (10 hours), a 20 ms audio offset and nonzero video PTS offsets.

Passed cases include muxed/video-only delivery; one-byte transport and playlist
boundaries; descriptor backpressure; unchanged init reuse; live refresh;
oversized live manifest compaction and advancing beyond stale prefetch;
memory refusal cleanup; malformed init/fragment rejection; decode-clock
regression; and continued rejection of byte-range/encrypted HLS. Peak accounted
allocation with prefetch in the isolated host fixture is about 5.3 MiB. This
is not a measurement of total PSP memory. All cases return zero owned bytes
and zero active test requests after teardown.
The first muxed integration case fails against the original pinned HLS
source with the exact device error, "fragmented or byte-range HLS is
unsupported", and passes against the adapter. Changed init URIs with stable
codec metadata are refetched; cancellation at init, fragment and primed
playback stages is checked for request and allocation cleanup.

The updated Twitch C core tests pass. The frontend passes clang syntax/API
checking against the pinned engine headers and Android libc with the prior
minimal PSP declarations. The PSP cross-build attempt failed at WSL path
conversion (exit -1), before compilation. Build 4 executable size, PSP ABI,
live Twitch delivery, first frame, audio/video sync, sustained throughput and
device memory use remain unverified. Historical build notes below do not
qualify build 4. Rebuild/install before device testing.

## Build 3 container fallback

The device reported "fragmented or byte-range HLS is unsupported". The
resolver now inspects each media playlist and tries up to eight distinct
compatible renditions in quality order, skipping initialization maps and
byte ranges. If none works, it reports no supported MPEG-TS stream. This
does not implement fragmented MP4 decoding. Container checks and excluded
rendition selection have new C fixtures in `native/tests/core_test.c`.
The full core C test suite passed using Unity's clang/lld toolchain, including
the new container and rendition-exclusion cases.

The cross-build attempt in this session failed at WSL path conversion
(exit -1), before compilation. Build 3 still requires compilation and
device validation. Existing install archives predate this source change.

## Build 2 network correction

The user's saved `native/test-results/psp-build.log` records a successful
build-1 Allegrex ELF/PRX cross-build and install archive, with executable text
1,303,932 / 4,480,000 bytes. That supersedes the initial source-only build
status below. The first reported device failure was "Network worker
unavailable". A copied `ptwitch-device.log` contains only three startup lines
and does not record network setup or request rejection.

Source inspection established that direct worker calls require explicit
initialization; the previous fixed-response API also rejects POST and custom
headers. Build 2 initializes with the runtime budget, uses the engine's
POST-capable streaming enqueue, drains at most 16 KiB per UI iteration, and
retires completed requests with the actual consumed-byte count. Admission
failures now record a diagnostic code without logging tokens. The packager
refuses stale build-1 executables.

The modified frontend passes the same clang syntax/engine-API check. The
build-2 PSP cross-build and on-device requests/playback still need testing.
No successful live Twitch request or decoded frame is claimed here.

## Passed

- The real `native/app/twitch.c` compiled with clang 14 targeting Windows x64,
  with `-Wall -Wextra -Werror`, then linked with lld as a freestanding DLL.
- Executed the actual C tests via P/Invoke. Checks cover channel and URL
  normalization, injection/overlength rejection, request construction, strict
  bounded JSON, escaped tokens, duplicate fields, null/denied/malformed
  responses, every truncated prefix of the token fixture, percent encoding,
  CDN hostname boundaries, 360p/240p preference and lower-bandwidth tie-break,
  incompatible codec/profile/geometry/framerate rejection, incomplete masters,
  and response byte ceilings.
- Ran 20,000 deterministic hostile byte inputs through the compiled C token
  and master-playlist parsers. Guard bytes around both output structures
  remained intact; no crash occurred. This is a bounded stress check, not a
  substitute for sanitizers.
- The new frontend passed a clang syntax-only check against the actual
  pinned Tilefinch/KOMI headers and PTube's existing Lexbor dependency headers.
  This used Android libc headers and minimal PSP syscall declarations solely
  to check C syntax and engine API calls. It does not qualify the actual PSP
  ABI, SDK, linking or execution.

These tests use constructed fixtures, not successful live Twitch responses.
They verify parsing policy and bounds, not Twitch acceptance or decoding.

## Blocked / unverified

- The PSP cross-build could not start: `wsl.exe -d Ubuntu ...` returned
  `Wsl/E_ACCESSDENIED` in the restricted execution session.
- Shell HTTPS access to upstream sources returned `fetch failed`. Protocol
  references were inspected using web retrieval, but no live token/master or
  stream was successfully fetched through the app or a network probe here.
- No PTwitch EBOOT was built or run. No installable archive is provided by
  this source-only session. The source is not claimed to compile on PSP yet.
- Tilefinch's full optimized host suite was not run in this session. PTube's
  earlier results do not count as PTwitch validation.
- PSP RAM/CPU load, HLS/MPEG-TS decode, first frame, A/V sync, long playback,
  240p/360p quality, HOME/Back during TLS/opening, reconnect, Wi-Fi loss, ads,
  discontinuities and actual power behavior require target hardware.

## Next checks after cross-build

1. Run `scripts/build-native.ps1` with normal WSL/Docker access. Save the full
   compile/link/size-check output, and correct any PSP compiler diagnostics.
2. Verify the package title is PTwitch Native and the app directory is
   PTWITCH_NATIVE. The packager also checks the module identity and MEMSIZE=1.
3. Test a live public channel that offers Main/Baseline H.264 + AAC-LC at
   360p/30 or 240p. Watch at least ten minutes with audio.
4. Test an offline channel, a source-only HD channel, and a refused playback
   request. They should return to the menu without a hang.
5. Press Back during token resolution, playlist fetch and initial buffering;
   press HOME during both network setup and playback.
6. SELECT reconnect should re-resolve access and return to the live edge.
   Disconnect/reconnect the hotspot and check the failure surface.
7. Save/remove favorites, restart the app, and check persistence, power modes
   and independent PTube files. Preserve both PTwitch logs when reporting.
