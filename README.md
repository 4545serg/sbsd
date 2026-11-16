# PROJECT NOISE DSI

Native Nintendo DS / DSi homebrew port of Project Noise. This is a separate target; the original 3DS source is not modified.

## Current implementation

- Native `libnds` + `libfat` `.nds` application structure.
- Dual-screen 256x192 interface with Project Noise branding.
- SD card initialization and music folder browsing (`/Music`, falling back to SD root).
- MP3 file discovery and folder navigation.
- Persistent favorites (`/project_noise.fav`).
- Persistent shuffle, repeat and volume preferences (`/project_noise.cfg`).
- Favorites page, settings page and about page.
- D-pad, A/B/X/Y, L/R, START/SELECT and touchscreen navigation.
- Custom 32x32 application icon.

## Important audio status

MP3 decoding is implemented with the project's existing decoder choice, **minimp3 (CC0)**, compiled without SIMD, on the ARM9. Decoded stereo PCM is downsampled to the Nintendo DS/DSi audio stream ceiling of 32768 Hz and passed to **Maxmod's manual PCM streaming API** (`mmStreamOpen` / `mmStreamUpdate`). The stream is 16-bit stereo and uses a 4096-frame buffer. `audio_update()` must be called continuously from the main loop. This is a first integration and still needs a real devkitARM build plus testing on DSi hardware; ARM9 decode speed, stream underruns, and file compatibility have not been verified here. The decoder currently uses fixed-point linear resampling to 32 kHz; MP3 decoding remains CPU-intensive on DS-class hardware, so high-bitrate files may need testing and tuning. Maxmod itself does not decode MP3; minimp3 performs the decoding.

## Build environment

Install devkitPro with the Nintendo DS development packages (`nds-dev`, including devkitARM, libnds, libfat and Maxmod), then run:

```sh
make
```

The Makefile downloads the CC0 `minimp3.h` header automatically on the first build (requires `curl`). Maxmod is initialized with an empty soundbank configuration and FIFO channel 7, following the devkitPro DS streaming example.

Expected output: `project_noise_dsi.nds`.

The current execution environment did not include devkitPro/devkitARM, so a real `.nds` build could not be performed here. Do not treat this source archive as hardware-tested.

## SD card

Create `/Music/` on the SD card and put `.mp3` files (and optional subfolders) inside it. The current browser also falls back to the SD root if `/Music/` does not exist. MP3 is the supported playback format in this build.

Favorites and preferences are stored at the SD card root.

## Controls

| Control | Action |
|---|---|
| D-pad Up/Down | Move through the library |
| A | Open folder / select track |
| B | Go back / leave Favorites or Settings |
| X | Favorite / unfavorite selected track |
| Y | Pause/resume playback (when browsing); return to Library from About |
| SELECT | Open Favorites |
| START | Open Settings; save and exit Settings |
| L | Toggle Shuffle |
| R | Toggle Repeat |
| Settings: Up/Down/Left/Right | Adjust playback volume |
| Settings: A / X | Toggle Shuffle / Repeat |
| Touch list item | Select item |
| Touch lower footer | Settings / About |

## Porting notes

The source uses `libnds` and `libfat`; it does not include 3DS-only `libctru`, `citro2d`, `citro3d` or `ndsp` APIs. The application icon is derived from the supplied Project Noise cover art.
