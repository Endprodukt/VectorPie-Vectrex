# Building

Both builds are configured in `build/pc` and `build/pi`. They need CMake 3.21+ and Ninja. The VectorPie SDK is a git clone in `external/VectorPie`, the same way as for Major Havoc PE Enhanced:

    git clone https://github.com/Goldfinch2/VectorPie.git external/VectorPie

## PC (Windows, MSYS2 gcc, SDL2)

In Git Bash, MSYS2's gcc has to come first in `PATH`:

    export PATH="/c/msys64/mingw64/bin:$PATH"
    cmake -S . -B build/pc -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
    cmake --build build/pc

`build/pc/vectrex.exe [CARTRIDGE] --windowed` runs it in a window. With `-DVX_BUILD_GAME=OFF` only the core and `vxtest` are built, without SDK and SDL.

## Raspberry Pi (the cabinet): clang + lld from Windows

Uses `cmake/toolchain-pi-clang.cmake` against the cabinet's sysroot from the Tempest 2000 port:

    T=C:/Users/ocare/.local/share/retcomm/toolchains/cmake-clang-v1/latest/bin
    cmake -S . -B build/pi -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-pi-clang.cmake \
          -DVX_PI_SYSROOT="F:/Tempest 2000 Port/build/pi-sysroot" \
          -DCMAKE_C_COMPILER=$T/clang.exe -DCMAKE_CXX_COMPILER=$T/clang++.exe -DCMAKE_MAKE_PROGRAM=$T/ninja.exe
    cmake --build build/pi

## Tools

`vxtest BIOS CART FRAMES [--skip-bios] [--peek ADDR N] [--svg OUT.svg]` runs the core through `vx_core.h` without a screen. It prints the cycles per frame, the lines and the sound, plus RAM bytes if `--peek` is given, and writes the last frame as SVG.

- CPU timing test: `vxtest package/system/bios.bin upstream/Vectrex_EMU/testcart/cputime_test_64k.bin 700 --peek C880 32` must print `02 0B 1E 04 05 06 07 08 05 05 08 07 0D 08 06 05 3C 3A 04 00 00 40 40 00 80 80 00 00 00 00 04 04` (the real Vectrex, `testcart/README.md`).

`vxunit DIR` checks how the settings are layered (a game's own, all games) and the digital ramp. It writes its test files into DIR and must end with `all passed`.

`vectrex --minestorm --windowed --menu PAGE --frames 300` opens the options menu on a page (0 main, 1/2 controls, 3 stick, 4 picture, 5 machine, 6 sound).

`vectrex --frames N --svg OUT.svg` stops after N frames sent and writes the last one the way the board sees it. That shows the picture settings without a tube. Convert to PNG with `magick OUT.svg OUT.png`.
