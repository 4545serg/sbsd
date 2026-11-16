# Third-party notices

The DSi port currently uses the following external libraries supplied by the devkitPro Nintendo DS development environment:

- **libnds** — Nintendo DS hardware and system library. https://github.com/devkitPro/libnds
- **libfat** — FAT filesystem support for Nintendo DS homebrew, supplied through devkitPro's DS development packages.

The original Project Noise 3DS project references minimp3 (CC0), stb_image (MIT) and dr_flac (public domain / MIT-style dedication) for its 3DS implementation. Those decoders are not currently compiled into this DSi port. Any future decoder integration must preserve the upstream license notices.

- **Maxmod** — Nintendo DS PCM streaming backend; its upstream license notice is included in the library source distributed by devkitPro. It does not decode MP3. https://github.com/devkitPro/maxmod
- **minimp3** — MP3 decoder, CC0/public domain dedication. Downloaded by the Makefile from https://github.com/lieff/minimp3
