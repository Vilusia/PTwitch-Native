"""Run real engine C code on Windows with isolated transport/budget seams.

Uses Unity's LLVM and the Windows Universal CRT. No SDK/WSL required.
Constructed MP4 fixtures test container handling, not hardware decoding.
"""
from pathlib import Path
import ctypes as C
import os
import re
import struct as S
import subprocess as P

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ROOT / 'upstream/komi-tube/vendor/tilefinch'
OUT = ROOT / 'native/test-results/hls-host'
OUT.mkdir(parents=True, exist_ok=True)
TOOLS = Path(os.environ.get('PTWITCH_LLVM', r'C:\Program Files\Unity\Hub\Editor\2022.3.22f1-x86_64\Editor\Data\PlaybackEngines\AndroidPlayer\NDK\toolchains\llvm\prebuilt\windows-x86_64\bin'))

headers = {
    'stdatomic.h': '''#pragma once
#include <stdbool.h>
typedef _Atomic(int) atomic_int;
typedef _Atomic(unsigned) atomic_uint;
typedef _Atomic(bool) atomic_bool;
typedef enum { memory_order_relaxed=0, memory_order_consume=1, memory_order_acquire=2,
memory_order_release=3, memory_order_acq_rel=4, memory_order_seq_cst=5 } memory_order;
#define atomic_load_explicit(p,o) __c11_atomic_load((p),(o))
#define atomic_store_explicit(p,v,o) __c11_atomic_store((p),(v),(o))
''',
    'stdio.h': '''#pragma once
#include <stddef.h>
#include <stdarg.h>
typedef struct HostFile FILE;
int snprintf(char *, size_t, const char *, ...);
int vsnprintf(char *, size_t, const char *, va_list);
int printf(const char *, ...);
FILE *fopen(const char *,const char *);int fclose(FILE *);
size_t fwrite(const void *,size_t,size_t,FILE *);
int fputs(const char *,FILE *);int fprintf(FILE *,const char *,...);
int puts(const char *);FILE *__acrt_iob_func(unsigned);
#define stderr (__acrt_iob_func(2))
''',
    'string.h': '''#pragma once
#include <stddef.h>
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
void *memchr(const void *, int, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, size_t);
char *strchr(const char *, int);
char *strrchr(const char *, int);
char *strstr(const char *, const char *);
size_t strcspn(const char *, const char *);
''',
    'stdlib.h': '''#pragma once
#include <stddef.h>
void *malloc(size_t);void *calloc(size_t,size_t);void *realloc(void *,size_t);void free(void *);
void abort(void);
''',
    'ctype.h': '''#pragma once
int tolower(int);int isalpha(int);int isalnum(int);int isdigit(int);int isspace(int);int isxdigit(int);
''',
}
for name, content in headers.items():
    (OUT / name).write_text(content)
exports = ['__stdio_common_vsprintf', '__stdio_common_vfprintf', '__acrt_iob_func', 'puts', 'fopen', 'fclose', 'fwrite', 'fputs', 'memcpy', 'memmove', 'memset', 'memchr', 'memcmp', 'strlen', 'strcmp', 'strncmp', 'strchr', 'strrchr', 'strstr', 'strcspn', 'malloc', 'free', 'tolower', 'isalpha', 'isalnum', 'isdigit', 'isspace', 'isxdigit']
(OUT / 'ucrt.def').write_text('LIBRARY ucrtbase.dll\nEXPORTS\n' + '\n'.join(exports) + '\n')
P.run([str(TOOLS / 'ld.lld.exe'), '-flavor', 'link', '/dll', '/noentry', '/def:' + str(OUT / 'ucrt.def'), '/machine:x64', '/out:' + str(OUT / 'ucrtbase.dll'), '/implib:' + str(OUT / 'ucrt.lib'), '/force:unresolved'], check=True, stdout=P.DEVNULL, stderr=P.DEVNULL)
(OUT / 'ucrtbase.dll').unlink(missing_ok=True)  # import library only; load the real system CRT
sources = ['src/media_hls.c', 'src/media_mp4.c', 'src/media_h264_psp_compat.c', 'src/swdec/swdec_ts.c', 'src/url.c', 'src/public_suffix.c', 'src/psp_media_state.c', 'tests/test_psp_media_state.c']
objects = []
poll_source = (ENGINE / 'src/psp_media_hls.c').read_text()
(OUT / 'ptwitch_hls_poll.inc').write_text(poll_source[poll_source.index('static MediaHlsTransportPollResult psp_hls_poll('):poll_source.index('static bool psp_hls_playlist_inflater_begin(')])
app_source = (ROOT / 'native/app/main.c').read_text()
(OUT / 'ptwitch_cleanup.inc').write_text(app_source[app_source.index('static bool retire_transport(void)'):app_source.index('static int favorite_index(')])
for path in [ENGINE / p for p in sources] + [ROOT / 'native/tests/hls_host.c', ROOT / 'native/tests/hls_transport_host.c', ROOT / 'native/common/ptwitch_avc_snapshot.c']:
    if path.name == 'media_hls.c' and os.environ.get('PTWITCH_HLS_SOURCE'):
        path = Path(os.environ['PTWITCH_HLS_SOURCE'])
    obj = OUT / (path.stem + '.obj')
    extra = ['-Dmain=ptwitch_state_test'] if path.name=='test_psp_media_state.c' else []
    P.run([str(TOOLS / 'clang.exe'), '--target=x86_64-pc-windows-msvc', '-std=c11', '-O1', '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-mno-stack-arg-probe', '-ffunction-sections', '-fdata-sections', '-nostdlib', '-Wall', '-Wextra', '-Werror', *extra, '-I', str(OUT), '-I', str(ENGINE / 'include'), '-I', str(ENGINE / 'src'), '-c', str(path), '-o', str(obj)], check=True)
    objects.append(str(obj))
dll = OUT / 'hls-test.dll'
P.run([str(TOOLS / 'ld.lld.exe'), '-flavor', 'link', '/dll', '/noentry', '/nodefaultlib', '/opt:ref', '/export:hls_ts_packet_test', '/export:hls_ts_packet_prepare', '/export:hls_transport_test', '/export:hls_test', '/export:ptwitch_avc_snapshot', '/export:ptwitch_test_snapshot_dir', '/export:ptwitch_state_test', '/export:hls_repeat_resume_test', '/out:' + str(dll), *objects, str(OUT / 'ucrt.lib')], check=True)

class Seen(C.Structure):
    _fields_ = [('dts', C.c_uint64), ('pts', C.c_uint64), ('duration', C.c_uint32), ('kind', C.c_uint32), ('size', C.c_uint32), ('byte', C.c_uint32)]

class Result(C.Structure):
    _fields_ = [('count', C.c_size_t), ('peak', C.c_size_t), ('leaked', C.c_size_t), ('completed', C.c_size_t), ('active', C.c_size_t), ('initializations', C.c_size_t), ('early_prefetch', C.c_size_t), ('paused_horizon', C.c_uint64), ('paused_active', C.c_size_t), ('paused_resumable', C.c_size_t), ('steps', C.c_size_t), ('failed', C.c_int), ('error', C.c_char * 256), ('seen', Seen * 1024)]

lib = C.CDLL(str(dll))
assert lib.hls_ts_packet_test()==0, 'TS firmware packet normalization regression'
print('PASS: TS packet metadata filter, intact slices and malformed packet refusal')
transport_line = lib.hls_transport_test()
assert transport_line == 0, f'HLS transport regression failed at hls_transport_host.c:{transport_line}'
print('PASS: HLS pre-header completion, admitted bodies and bounded connection cleanup')
lib.hls_test.argtypes = [C.c_char_p, C.c_size_t, C.c_char_p, C.c_size_t, C.c_char_p, C.c_size_t, C.c_char_p, C.c_size_t, C.c_size_t, C.c_size_t, C.c_char_p, C.c_size_t, C.c_size_t, C.c_size_t, C.c_size_t, C.POINTER(Result)]
lib.hls_test.restype = C.c_int

def u32(n): return S.pack('>I', n)
def box(name, data): return u32(len(data) + 8) + name.encode() + data
def full(name, data, flags=0): return box(name, u32(flags) + data)

def baseline_sps(profile=66, constraints=0):
    bits = f'{0x67:08b}{profile:08b}{constraints:08b}{30:08b}'
    def ue(n):
        code = f'{n+1:b}'
        return '0'*(len(code)-1) + code
    bits += ue(0)*4 + ue(1) + '0' + ue(26) + ue(14) + '1' + '1' + '0' + '0' + '1'
    bits += '0' * (-len(bits) % 8)
    return int(bits, 2).to_bytes(len(bits)//8, 'big')

def initialization(audio=True, profile=66, constraints=0):
    sps = baseline_sps(profile, constraints)
    avcc = bytes([1,profile,constraints,30,255,225]) + S.pack('>H', len(sps)) + sps + b'\x01\x00\x04\x68\xce\x3c\x80'
    avc1 = bytearray(78)
    S.pack_into('>HH', avc1, 24, 432, 240)
    video = box('avc1', bytes(avc1) + box('avcC', avcc))
    # esds descriptors: AAC-LC, 48kHz stereo ASC 0x1190.
    esds = full('esds', b'\x03\x19\x00\x02\x00\x04\x11\x40\x15\x00\x00\x00' + b'\x00'*8 + b'\x05\x02\x11\x90\x06\x01\x02')
    mp4a = bytearray(28)
    S.pack_into('>H', mp4a, 16, 2)
    S.pack_into('>I', mp4a, 24, 48000 << 16)
    sound = box('mp4a', bytes(mp4a) + esds)
    def track(tid, scale, handler, entry):
        tkhd = full('tkhd', u32(0)*2 + u32(tid) + u32(0)*2)
        mdhd = full('mdhd', u32(0)*2 + u32(scale) + u32(0)*2)
        hdlr = full('hdlr', u32(0) + handler.encode() + u32(0)*3)
        stbl = full('stsd', u32(1) + entry) + full('stsz', u32(0)*2) + full('stsc', u32(0)) + full('stco', u32(0)) + full('stts', u32(0))
        return box('trak', tkhd + box('mdia', mdhd + hdlr + box('minf', box('stbl', stbl))))
    tracks = track(1, 90000, 'vide', video)
    defaults = full('trex', u32(1)+u32(1)+u32(3000)+u32(0)+u32(0))
    if audio:
        tracks += track(2, 48000, 'soun', sound)
        defaults += full('trex', u32(2)+u32(1)+u32(1024)+u32(0)+u32(0))
    return box('ftyp', b'isom' + u32(0) + b'isomiso6') + box('moov', tracks + box('mvex', defaults))

def fragment(index, audio=True, leading=False, no_idr=False):
    # 70 video + 110 audio samples exceed the ring's 64 descriptor capacity.
    # Nonzero broadcast clock plus a 20ms audio offset must survive remapping.
    def traf(tid, origin, count, duration, offset, marker, composition):
        tfhd = full('tfhd', u32(tid), 0x20000)
        tfdt = full('tfdt', S.pack('>Q', origin), 0x01000000)
        rows = b''.join(u32(duration)+u32(6)+u32(0 if i == 0 else 0x10000)+u32(composition) for i in range(count))
        trun = full('trun', u32(count)+S.pack('>i', offset)+rows, 0x000f01)
        return box('traf', tfhd+tfdt+trun), (u32(2)+bytes([0x65,marker]))*count
    def make(vo, ao):
        v, vb = traf(1, 90000*36000 + index*210000, 70, 3000, vo, 0x30+index, 1500)
        a, ab = traf(2, 48000*36000 + 960 + index*112000, 110, 1024, ao, 0x60+index, 0)
        moof = box('moof', full('mfhd', u32(index+1)) + v + (a if audio else b''))
        return moof, vb, ab
    preliminary, vb, ab = make(0, 0)
    moof, vb, ab = make(len(preliminary)+8, len(preliminary)+8+len(vb))
    if leading:
        vb = vb[:4] + b'\x41' + vb[5:]
    if no_idr:
        vb = vb.replace(u32(2)+b'\x65', u32(2)+b'\x41')
    return moof + box('mdat', vb + (ab if audio else b''))

def ts_fragment(index, leading=False, frames=2, padding_packets=0, audio=False):
    def packet(pid, payload):
        assert len(payload)<=183
        adaptation=183-len(payload)
        return bytes([0x47,0x40|(pid>>8),pid&255,0x30,adaptation]) + bytes([0])*(adaptation>0) + b'\xff'*max(0,adaptation-1) + payload
    pat=bytes.fromhex('00 b0 0d 00 01 c1 00 00 00 01 e1 00 00 00 00 00')
    pmt=bytes.fromhex('02 b0 12 00 01 c1 00 00 e1 01 f0 00 1b e1 01 f0 00 00 00 00 00')
    if audio:
        pmt=bytes.fromhex('02 b0 17 00 01 c1 00 00 e1 01 f0 00 1b e1 01 f0 00 0f e1 02 f0 00 00 00 00 00')
    def pes(pts, data, sid=0xe0):
        encoded=bytes([0x21|((pts>>29)&14),(pts>>22)&255,1|((pts>>14)&254),(pts>>7)&255,1|((pts<<1)&254)])
        return b'\x00\x00\x01'+bytes([sid])+b'\x00\x00\x80\x80\x05'+encoded+data
    prefix=b'\x00\x00\x01'
    first=prefix+baseline_sps()+prefix+b'\x68\xce\x06\xe2'+prefix+bytes([0x41 if leading else 0x65,0x80,0x11])
    result=packet(0,b'\x00'+pat)+packet(0x100,b'\x00'+pmt)
    result+=packet(0x101,pes(index*210000,first))
    audio_pts=index*210000
    for frame in range(1, frames):
        result+=packet(0x1fff,b'\x00')*padding_packets
        result+=packet(0x101,pes(index*210000+frame*3000,prefix+bytes([0x65 if leading else 0x41,0x80,0x22])))
        if audio:
            while audio_pts<index*210000+(frame+1)*3000:
                result+=packet(0x102,pes(audio_pts,bytes.fromhex('fff14c80015ffc112233'),0xc0))
                audio_pts+=1920
    return result

PLAYLIST = b'#EXTM3U\n#EXT-X-TARGETDURATION:3\n#EXT-X-MAP:URI="init.mp4"\n#EXTINF:2.333,\n0.mp4\n#EXTINF:2.333,\n1.mp4\n#EXT-X-ENDLIST\n'

def run(label, playlist=PLAYLIST, init=None, first=None, second=None, slice=16384, quota=16*1024*1024, failure=False, refresh=None, cancel_after=0, pause_steps=0):
    init = initialization() if init is None else init
    first = fragment(0) if first is None else first
    second = fragment(1) if second is None else second
    result = Result()
    lib.hls_test(playlist, len(playlist), init, len(init), first, len(first), second, len(second), slice, quota, refresh, len(refresh) if refresh else 0, cancel_after, 10, pause_steps, C.byref(result))
    assert result.leaked == 0 and result.active == 0, (label, 'leak', result.leaked, result.active)
    assert result.early_prefetch == 0, (label, 'prefetch competed with first fragment')
    assert bool(result.failed) == failure, (label, result.error.decode())
    if not failure and not cancel_after:
        assert result.completed == 2, (label, result.completed, result.count)
    print(f'PASS {label}: samples={result.count} peak={result.peak}B error={result.error.decode()}')
    return result

if __name__ == '__main__':
    assert lib.ptwitch_state_test()==0
    assert lib.hls_repeat_resume_test()==0
    print('PASS repeated buffering resumes from a complete resident tail')
    print('PASS upstream media state suite, including dormant/reclaim lifecycle')
    ts_playlist=PLAYLIST.replace(b'#EXT-X-MAP:URI="init.mp4"\n',b'')
    ts=run('TS normal startup',playlist=ts_playlist,first=ts_fragment(0),second=ts_fragment(1))
    ts_leading=run('TS skips leading dependent picture',playlist=ts_playlist,first=ts_fragment(0,True),second=ts_fragment(1))
    assert ts.count==4 and ts_leading.count==3 and ts_leading.seen[0].byte==0x22
    held_ts=run('TS paused queue resumes while segment remains active',
        playlist=ts_playlist.replace(b'2.333',b'7.000'),
        first=ts_fragment(0,frames=210),second=ts_fragment(1,frames=210),pause_steps=400)
    assert held_ts.paused_active and held_ts.paused_resumable, (held_ts.paused_active,held_ts.paused_horizon)
    reserve_clock=held_ts.seen[9].dts*1000000//90000
    assert held_ts.paused_horizon-reserve_clock>=2000000, held_ts.paused_horizon-reserve_clock
    mixed_ts=run('TS mixed 30fps/48kHz builds two-second reserve',
        playlist=ts_playlist.replace(b'2.333',b'6.000'),
        first=ts_fragment(0,frames=180,audio=True),second=ts_fragment(1,frames=180,audio=True),pause_steps=400)
    mixed_clock=mixed_ts.seen[9].dts*1000000//90000
    assert mixed_ts.paused_horizon-mixed_clock>=2000000 and mixed_ts.paused_resumable, mixed_ts.paused_horizon-mixed_clock
    paced_ts=run('TS batched packet delivery keeps pace',playlist=ts_playlist,
        first=ts_fragment(0,frames=70,padding_packets=12),second=ts_fragment(1,frames=70,padding_packets=12),
        slice=C.c_size_t(-1).value)
    assert paced_ts.count==140 and paced_ts.steps<200, paced_ts.steps
    lib.ptwitch_test_snapshot_dir.argtypes=[C.c_char_p]
    lib.ptwitch_test_snapshot_dir(str(OUT).encode())
    lib.ptwitch_avc_snapshot.argtypes=[C.c_char_p,C.c_size_t,C.c_char_p,C.c_size_t,C.c_char_p,C.c_size_t,C.c_uint,C.c_uint,C.c_uint,C.c_int,C.c_int]
    sps=baseline_sps();pps=b'\x68\xce\x06\xe2';units=u32(2)+b'\x65\x80'+u32(2)+b'\x06\x80'
    lib.ptwitch_avc_snapshot(sps,len(sps),pps,len(pps),units,len(units),4,284,160,4,3)
    start=b'\x00\x00\x00\x01'
    build = re.search(r'#define PTWITCH_BUILD (\d+)', (ROOT/'native/app/version.h').read_text()).group(1)
    assert (OUT/f'ptwitch-avc-{build}-0.h264').read_bytes()==start+sps+start+pps+start+b'\x65\x80'+start+b'\x06\x80'
    assert 'nal-types=5,6' in (OUT/f'ptwitch-avc-{build}-0.txt').read_text()
    print('PASS exact first-AU snapshot, parameter sets, all NALs and metadata')
    result = run('muxed MP4, queue backpressure, init reuse')
    run('Main profile fMP4 decoder route and constraint flags',init=initialization(profile=77,constraints=64))
    assert result.count == 360
    assert result.initializations == 1
    paused=run('paused consumer counts resident fragments',pause_steps=200)
    assert paused.paused_horizon>=4000000 and paused.paused_active==0 and paused.paused_resumable, (paused.paused_horizon,paused.paused_active,paused.paused_resumable)
    partial=run('partial successor is not counted as fully buffered',slice=1,pause_steps=200)
    assert partial.paused_horizon<3000000 and partial.paused_active>0,(partial.paused_horizon,partial.paused_active)
    video = [s for s in result.seen[:result.count] if s.kind == 1]
    audio = [s for s in result.seen[:result.count] if s.kind == 2]
    assert len(video) == 140 and len(audio) == 220
    assert (video[0].dts, video[0].pts, audio[0].dts) == (0, 1500, 1800)
    assert video[70].dts == 210000 and video[70].pts == 211500
    assert audio[110].dts == 211800
    assert [s.byte for s in video[:70]] == [0x30]*70 and [s.byte for s in video[70:]] == [0x31]*70
    led = run('leading non-IDR flagged sync is skipped', first=fragment(0, leading=True))
    assert led.count == 357
    led_video = [s for s in led.seen[:led.count] if s.kind == 1]
    assert len(led_video) == 139 and led_video[0].dts == 0 and led_video[0].pts == 1500
    run('no actual IDR despite sync flags fails cleanly', first=fragment(0, no_idr=True), second=fragment(1, no_idr=True), failure=True)
    run('one-byte transport and playlist boundaries', slice=1)
    run('video-only fMP4', init=initialization(False), first=fragment(0, False), second=fragment(1, False))
    live = PLAYLIST.split(b'#EXTINF:2.333,\n1.mp4')[0]
    refreshed = b'#EXTM3U\n#EXT-X-TARGETDURATION:3\n#EXT-X-MEDIA-SEQUENCE:1\n#EXT-X-MAP:URI="init.mp4"\n#EXTINF:2.333,\n1.mp4\n#EXT-X-ENDLIST\n'
    live_result = run('live playlist refresh, shared decode clock', playlist=live, refresh=refreshed)
    assert live_result.count == 360 and live_result.initializations == 1
    held_live=run('live refresh fetches next fragment while consumer is paused',playlist=live,refresh=refreshed,pause_steps=200)
    assert held_live.paused_horizon>=4000000 and held_live.paused_active==0 and held_live.paused_resumable,(held_live.paused_horizon,held_live.paused_active,held_live.paused_resumable)
    assert held_live.count==360
    changed_map = run('refetch changed init URI, stable codec', playlist=live, refresh=refreshed.replace(b'init.mp4', b'init.mp4?revision=2'))
    assert changed_map.initializations == 2 and changed_map.count == 360
    # An oversized live manifest must keep its map after compacting its window.
    huge = b'#EXTM3U\n#EXT-X-TARGETDURATION:3\n#EXT-X-MAP:URI="init.mp4"\n' + b'#EXTINF:2.333,\n0.mp4\n'*4000
    compacted = run('compacted live manifest retains init map', playlist=huge, refresh=refreshed.replace(b'SEQUENCE:1', b'SEQUENCE:4000'))
    assert compacted.initializations == 1
    run('memory refusal cleanup', quota=1024*1024, failure=True)
    run('optional prefetch buffer refusal', quota=4*1024*1024)
    run('MP4 buffer refusal cleanup', quota=3*1024*1024, failure=True)
    run('cancel during init transfer', slice=1, cancel_after=2)
    run('cancel during fragment transfer', slice=1, cancel_after=2000)
    run('cancel with primed samples and prefetch', cancel_after=30)
    run('truncated fragment cleanup', first=fragment(0)[:-1], failure=True)
    run('invalid init cleanup', init=b'not mp4', failure=True)
    run('decode clock regression rejection', second=fragment(0), failure=True)
    run('byte range rejection', playlist=PLAYLIST.replace(b'#EXTINF:', b'#EXT-X-BYTERANGE:5@0\n#EXTINF:', 1), failure=True)
    run('encrypted playlist rejection', playlist=PLAYLIST.replace(b'#EXT-X-MAP:', b'#EXT-X-KEY:METHOD=AES-128,URI="key"\n#EXT-X-MAP:'), failure=True)
    print('PASS: live fMP4 engine integration checks')
