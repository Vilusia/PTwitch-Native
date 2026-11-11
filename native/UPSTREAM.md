# PTwitch provenance

PTwitch adapts the user's native PTube frontend/runtime. Shared decoding,
HTTPS, live HLS, MPEG-TS demux, rendering and networking remain from:

- KOMI Tube: https://github.com/kouminiku-9900/komi-tube
- Pinned commit: `47bf5677a5254dfc37d8716233e7b1861c51698d`
- Copyright (c) 2026 komi-tube contributors; MIT.
- Vendored Tilefinch: https://github.com/stjanovitz/tilefinch
- Recorded upstream engine base: `5a77720f31bf62007249ec458109da50ae7a5a05`
- Copyright (c) 2026 Steven Janovitz; MIT plus separate dependency licenses.

The exact KOMI source snapshot copied from PTube is
`native/upstream-source.tar`, SHA-256:
`bdd47190f155725df6f8473a4df406b2d34c514a513339cb1ab2d0c06bca7591`.

`native/app/ui.c`, `ui.h`, `osk.c`, `osk.h`, and
`native/common/komi_runtime.c` retain their PTube/KOMI notices. PTwitch adds a
new Twitch frontend/parser, purple accent, its own save/log filenames, query
redaction in runtime formatted logs, and separate packaging/build scripts.
The retained PTube guard and power helpers preserve its bounds/clock policy.
`patch-upstream.py` retains PTube's bounded-memmove scheduler error-overlap
correction. Build 4 additionally installs `native/engine/media_hls.c` and its
`ptwitch_hls_fmp4.inc` adapter, and exposes the MP4 parser's per-track `tfdt`
origin so live delivery retains a shared A/V clock. The HLS copy is adapted
from the pinned MIT-licensed Tilefinch source and retains the same notices.
The adapter reuses the existing MP4 parser with tighter fragment table limits,
page-budget allocations and firmware codec admission. The executable size
ratchet, network authority checks and codec/profile limits are unchanged.

Twitch's anonymous playback endpoint/query constants were checked against
Streamlink's public Twitch plugin on 2026-11-11:
https://github.com/streamlink/streamlink/blob/master/src/streamlink/plugins/twitch.py
and its compatibility documentation:
https://streamlink.github.io/cli/plugins/twitch.html

PTwitch does not include or run Streamlink. The small protocol client is
implemented independently in C. The Client-ID used with Twitch's public
web playback API is a public client identifier, not a user credential.
Anonymous playback uses an unofficial interface that can change or refuse
native clients. No browser-integrity bypass or third-party proxy is bundled.

Keep all upstream licenses and notices when distributing builds. The install
packager includes the engine's complete required notice manifest and font
licenses. The source ZIP includes the exact source archive with upstream
license files and relinkable frontend/build sources.
