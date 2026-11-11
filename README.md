# PTwitch Native

Watch Twitch livestreams directly on your PSP over Wi-Fi. Enter a channel name, watch its stream, and save your favorite channels. No PC or relay server is needed while watching.

## Installation

You need a PSP running **custom firmware (CFW)** and a working saved Wi-Fi connection. PTwitch is designed for 64 MB models (PSP-2000/3000); testing has focused on PSP-3000.

1. Extract the **PTwitch-native-psp.zip** install package.
2. Connect your PSP to your computer using USB mode.
3. Copy the extracted `PSP` folder to the root of your Memory Stick, merging folders when prompted.
4. Check that the app is at `PSP/GAME/PTWITCH_NATIVE/EBOOT.PBP`. Keep the included fonts, certificates, and other files with it.
5. Safely disconnect the PSP and launch **PTwitch Native** from **Game → Memory Stick**.
6. Connect to Wi-Fi, choose **Watch a Twitch channel**, and enter a channel name.

The GitHub source-code ZIP is not the PSP install package.

## Controls

- **Confirm / Back:** follow your PSP's system button setting.
- **Triangle:** save or remove a favorite.
- **SELECT:** reconnect to the stream.
- **START:** exit from the main menu.

## Limitations

- **Experimental:** buffering, disconnects, and decoder errors can still occur.
- Streams must offer a compatible **H.264 video / AAC audio** quality at **360p or lower, up to 30 fps**. Having a 160p or 360p option does not guarantee playback.
- High-resolution source-only streams cannot be converted on the PSP.
- A stable Wi-Fi connection is required. Playback has a delay behind the live broadcast.
- No Twitch account login or chat; subscriber-only or otherwise restricted streams may be unavailable.
- Twitch changes can affect compatibility, and some channels may not work.

## Credits

Based on [KOMI Tube](https://github.com/kouminiku-9900/komi-tube) and [Tilefinch](https://github.com/stjanovitz/tilefinch). Upstream licenses and notices are retained. See [provenance and credits](native/UPSTREAM.md).

PTwitch is an unofficial project and is not affiliated with Twitch or Sony.
