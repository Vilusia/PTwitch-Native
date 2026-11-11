# PTwitch Native for PSP

Experimental standalone Twitch homebrew adapted from PTube's native
KOMI Tube / Tilefinch foundation. The PSP connects directly to Twitch over
Wi-Fi. No PC, VPS, relay, Streamlink installation, or conversion server is
needed **to watch**. A development computer is still needed to compile it.

**Current status: build 21 source. Build 8 achieved PSP audio/video playback.**
Build 21 separates TS video/audio read heads over one bounded download, so
held video cannot prevent audio from advancing the presentation clock.
This targets the no-progress stall observed after several minutes of playback.
Build 19 sustains stopsigncam for over four minutes with about two seconds
buffered, then one run trips a decoder no-progress guard. Build 20 adds
playback/decoder counters to locate that stall; it is a diagnostic build.
Build 18 plays stopsigncam and recovers from buffering, but its TS queue
usually holds less than half a second. Build 19 increases bounded TS sample
capacity and readahead so a typical mixed stream can retain about two seconds.
Build 17 displayed stopsigncam at 360p, confirming TS decoding now works in
that run. Build 18 targets its subsequent buffering deadlock: batch bounded
TS packet processing and resume from a queue that is holding up delivery.
Build 17 adds network-worker counters to diagnose transfers stalled before
any media bytes arrive. It retains the build 16 packet candidate and makes no
additional playback behavior changes.
Build 16 tests a TS firmware compatibility change: remove access-unit
delimiters and SEI metadata from converted packets before the raw-NAL bridge.
Both supplied failing frames decode unchanged after this filter in software;
whether it resolves the PSP firmware refusal needs a device test.
Build 15 allows up to 15 seconds for cancelled network work to retire, logs
cleanup progress, and lets a later channel attempt resume cleanup. Requests
remain blocked while the worker still owns its memory.
Build 14 fixes HLS requests waiting indefinitely for HTTP headers after a
connection has already failed. This enables the existing bounded retry path
to process the actual error. The 360p TS decoder refusal remains unresolved;
the lower-quality fallback needs another device test.
Build 3 compiled successfully on the user's machine but could not play a
channel packaged as fragmented MP4. Build 4 adds native live fMP4 HLS delivery:
fetch an initialization map, parse each bounded fragment with the existing MP4
demuxer, and submit H.264/AAC samples to the PSP firmware path. TS playback
remains available. No server or conversion is used.

The build 4 device log confirms successful Twitch requests and a 640x360
rendition reaching firmware, but first-picture decode failed with 0x80628002.
Build 5 prefers 240p within the native-width decoder path, tries lower
available qualities after startup AVC refusal, and starts fMP4 video at an
actual AVC IDR rather than trusting container sync flags. This is a candidate
correction; device playback is not yet verified.

Build 5 device evidence shows the same refusal at 284x160, so resolution alone
does not explain it. Build 6 extends IDR startup gating to MPEG-TS and records
the actual media container. It also saves up to two first-access-unit diagnostic
captures beside the app (`ptwitch-avc-0.h264`/`.txt`, and index 1 for a retry).
These contain compressed video and parameter sets, not URLs or access tokens.
Remove the capture files after diagnosis. Playback remains unverified.

Build 6 logs identify a concrete fMP4 handoff bug: the opening state machine
always tested HLS codec configuration as Annex-B. Build 7 selects the parser
from the sample representation, so MP4 avcC uses the MP4 route and TS retains
Annex-B. MIME/profile/constraint/level reporting follows the same representation.
Requests that fail before receiving any HTTP response get one cancellable retry
and retain the underlying transport error in the log. HTTP denials are not retried.

Build 7 reached live opening but timed out in resolve. Build 8 closes failed
streams before showing their error, cancels and observes retirement of all
network operations before another attempt, and uses a fresh connection after
failure/cleanup. It will report a still-busy transport rather than queue work
behind it indefinitely. Initial fMP4 prefetch is deferred until the first
fragment has supplied track metadata. Opening logs now include HLS byte,
segment and request counters. These changes still need device validation.

Build 8 device playback started in about 4.4 seconds, then buffered at roughly
six seconds of stream time. Build 9 counts downloaded MP4 fragments beyond
the small sample queue, prefetches from a pending live snapshot while buffering,
and carries speculative data by media sequence across playlist refreshes.
HLS resume targets also fit the bounded fragment window and resident tail.
Reconnect now reclaims the hidden player through the session lifecycle API.
Host paused-consumer/resume-policy and lifecycle tests pass; these corrections
still need a sustained PSP test. Buffering that cannot recover exits after a
bounded wait rather than treating an old displayed frame as new progress.
First-access-unit capture is disabled by default now that hardware decoding
has been demonstrated; the diagnostic helper remains available to developers.

Build 9 device logs later captured a separate decoder startup refusal for a
Twitch MPEG-TS rendition (`0x80628002`) even though Twitch requests returned
HTTP 200 and transport cleanup completed. Build 10 probes the bounded set of
compatible renditions for fMP4 first, retaining the first supported TS rendition
as a fallback. This is based on earlier successful fMP4 startup and still needs
PSP validation; it does not claim to fix every firmware AVC refusal or the
separate, incomplete latest network-initialization log.

Build 10 device logs confirm fMP4 playback for over eight minutes, but show
repeated source starvation and a final media request with no body progress for
six seconds. Build 11 extends the background transport's no-progress grace to
12 seconds (the HLS request's 30-second hard deadline remains), prefers the
highest compatible rendition up to 360p, and prefers fMP4 among candidates at
the same resolution. A 360p decoder refusal retries a lower rendition. These
are targeted mitigations, not a sustained-playback qualification; PSP validation
is still required.

Build 11 device captures stopped after `request begin`, before any HTTP request
was queued. Build 12 adds always-flushed stage markers around curl global setup,
budget reservation, multi/event creation, and transport-thread startup. This is
diagnostic instrumentation only; TLS certificate and hostname verification are
unchanged.

Build 12 diagnostics localized intermittent connection hangs to before request
enqueue. Build 13 temporarily enables the bounded AVC startup capture for the
small-channel TS decoder refusal. Test a failing small streamer first after
launch: it writes up to two compressed access units and SPS/PPS metadata to
`ptwitch-avc-13-0.*` and `ptwitch-avc-13-1.*`. These contain stream video, not
URLs or tokens. Remove the captures after diagnosis.

Host integration tests pass for muxed/video-only fragments, queue backpressure,
shared audio/video clocks, playlist refresh and compaction, malformed data,
and memory refusal. Rebuild and reinstall before testing; existing ZIPs predate
build 13. WSL execution remains blocked in this restricted assistant session.

## Implemented

- Enter a Twitch channel name with the PSP keyboard.
- Save up to 32 favorites locally and remove them from the favorites screen.
- Resolve anonymous public live playback directly through Twitch.
- Select a multiplexed H.264 Baseline/Main + AAC-LC rendition, at most
  640x360, 30 fps, and 1.8 Mbps. Prefer the highest compatible resolution up to
  360p, preferring fMP4 over TS at the same resolution. Startup decoder refusal
  retries lower available qualities. The app does not transcode.
- Live HLS with MPEG-TS or H.264/AAC fragmented MP4, bounded sample queues,
  live-playlist refresh, and PSP firmware audio/video decoding. fMP4 uses a
  64 KiB init limit and 2 MiB fragment limit, with an optional budgeted 2 MiB
  buffer to download the next fragment during playback.
- Fullscreen video with a temporary channel/battery overlay.
- SELECT reconnects by obtaining fresh playback access and opening the live
  edge. Back cancels an opening/request or leaves playback. HOME exits.
- PTube's performance/balanced/battery-saver clock policy, saved separately.
- Bounded network requests, cancellation, prior-session log retention, and
  query redaction in the adapted runtime's formatted logs.

## Compatibility limits

The first target is PSP-3000 with ARK-4, matching PTube. The shared engine
requires a 64 MB PSP; PSP-1000 is not supported. Other models/CFW are not
qualified here. Use a working saved PSP Wi-Fi connection.

Some channels offer only an incompatible source rendition. PTwitch will show
an error instead of sending an HD/HEVC/AV1 stream to the PSP decoder. Ads,
discontinuities, decoder profile details, bandwidth and stream transitions
still need live hardware checks. No ad skipping is implemented.
Byte-range HLS, encrypted media, changing initialization maps within a single
playlist, and mid-playback codec changes remain unsupported. A map may change
between snapshots if the decoder configuration remains identical. Reconnect
after an incompatible stream/ad transition.

Twitch may require browser/client-integrity verification or change its
playback interfaces. This version reports refusal; it does not run a browser
challenge, bypass restrictions, or silently fall back to a helper server.
There is no guarantee every public stream can open without a companion.
Account login, subscriber-only access, chat, VODs, pause, seeking, category
browsing and global channel search are not implemented. Channel entry and
favorites are the initial navigation.

## Build on Windows

Use a normal PowerShell window with WSL Ubuntu and Docker available, as for
PTube. From `C:\Users\OuchM\Documents\PTwitch` run:

```powershell
.\scripts\build-native.ps1
```

The script extracts the included pinned source snapshot if needed, builds
with the same pinned PSPDEV container as PTube, checks the engine's unchanged
4,480,000-byte executable-text limit, and packages:

```text
dist/PTwitch-native-psp.zip
```

Building initially needs internet access for the toolchain/dependencies.
The built PSP app does not depend on the development machine.

In a Linux PSPDEV environment, extract `native/upstream-source.tar` into
`upstream/komi-tube`, then run `sh native/build.sh`.

## Install after a successful build

Extract the install archive to the memory-stick root. It installs under
`PSP/GAME/PTWITCH_NATIVE`, separately from PTube. Launch **PTwitch Native** from
**Game → Memory Stick**. Choose **Watch a Twitch channel**, enter the channel
name, and confirm. Twitch URLs in the simple form
`https://www.twitch.tv/channel` also work.

Controls follow the PSP's system confirm/back setting. In playback, confirm
shows the controls, Triangle saves/removes the channel, SELECT reconnects, and
Back returns. In Favorites, confirm watches and Triangle removes a channel.
START exits from the main menu.

If a build or device check fails, retain `ptwitch.log`,
`ptwitch-previous.log`, and `tilefinch-validation.txt` from the app folder.
Review logs before sharing: the adapted formatted logger drops signed URL
queries, but the shared engine has additional diagnostic paths.

## Development checks

`scripts/test-core.ps1` compiles and runs the actual Twitch C parser as a
freestanding Windows DLL with clang and lld. No mocked parser or translated
implementation is used. Pass `-Clang` and `-Lld` for installed tool paths.
See [docs/TESTING.md](docs/TESTING.md) for exactly what was and was not tested.

`python scripts/package-source.py` creates the standalone source ZIP,
including the pinned upstream snapshot and third-party notices. The PSP
packager refuses an executable without the PTwitch module identity.

Provenance and protocol references: [native/UPSTREAM.md](native/UPSTREAM.md).
