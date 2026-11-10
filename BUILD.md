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

## To the cabinet

    sh tools/deploy.sh

Uses the SSH key `~/.ssh/id_ed25519_vectorpie` for `pi@192.168.0.122`. It installs to `/persistent/games/vectrex`, starts `run.sh` (which stops the VectorPie menu and brings it back afterwards) and shows `run.log`. If another game has the board, it only installs.

## vxcolor (colouring from the command line)

Built with the PC and the Pi builds (`build/pc/vxcolor.exe`, `build/pi/vxcolor`). For an AI or a script: `docs/AI_GUIDE.md`, `vxcolor help`.

## Vectrex Colour Studio (Windows)

Built with the PC build (target `vxstudio`, `build/pc/Vectrex Colour Studio.exe`, needs `SDL2.dll`). Dear ImGui is in `third_party/imgui`. Development options: `--game PATH`, `--shot FILE.bmp N` (the window as a picture at frame N, then quit), `--demo-box X0 Y0 X1 Y1`, `--demo-clicks "X,Y;X,Y"` (the first a click, the others Shift+clicks), `--demo-back N` (frames back first), `--demo-part A B` (game parts remembered at frames A and B, the first byte used), `--demo-scope S --demo-colour I` (picks before the shot), environment `VX_STUDIO_SHOTS=DIR` (a picture after every left click).

## PC companion

    sh tools/companion.sh make     # build/companion/Vectrex Companion/ and a .zip (after the PC build)
    sh tools/companion.sh pull     # the cabinet's cartridges into its roms/, the cabinet's own profiles
    sh tools/companion.sh push     # its own profiles to the cabinet (one there that differs: kept in old-<date>/)

The emulator for Windows in a window (`Vectrex Companion.bat` starts `vectrex.exe --windowed`), next to the Colour Studio, which is the colour editor. It needs only `SDL2.dll`. Profiles go by the cartridge's file name, so it should have the cabinet's cartridges (`pull`).

## Package

    sh tools/pack.sh

writes `build/vpk/vectrex-<version>.vpk` (program, `game.ini`, `vectrex.ini`, `names.ini`, `run.sh`, BIOS). Bump `version` in `package/game.ini` first.

## Tools

`vxtest BIOS CART FRAMES [--skip-bios] [--peek ADDR N] [--svg OUT.svg]` runs the core through `vx_core.h` without a screen. It prints the cycles per frame, the lines and the sound, plus RAM bytes if `--peek` is given, and writes the last frame as SVG.

- CPU timing test: `vxtest package/system/bios.bin upstream/Vectrex_EMU/testcart/cputime_test_64k.bin 700 --peek C880 32` must print `02 0B 1E 04 05 06 07 08 05 05 08 07 0D 08 06 05 3C 3A 04 00 00 40 40 00 80 80 00 00 00 00 04 04` (the real Vectrex, `testcart/README.md`).

`vxtest` also takes `--tags OUT.csv` (each line with the 6809's PC, X and U), `--input "F0-F1:MASK[:X;Y],..."` (player 1's buttons and stick) and `--hum`. Its SVG colours lines by the high byte of X, which helps find the addresses for a colour profile.

`vxshot BIOS CART FRAMES OUT.svg [--skip-bios] [--id GAME] [--profile FILE] [--port2 NAME] [--input ...] [--spin "F0-F1:COUNTS"] [--peek ADDR]` renders a game the way the board gets it, colour profile included, with an optional spinner or trackball on port 2. `--pieces` lists the pieces the colour editor sees. `--edit "PIECE:SCOPE:COLOUR[*GAIN],..."` (SCOPE: shape, object, group or routine) does what a person does in the Colour Studio, and `--save FILE` writes the profile so made, e.g. `package/colors/hyper.ini`.

`vxunit DIR [BIOS]` checks how the settings are layered (a game's own, all games) and the digital ramp. It writes its test files into DIR and must end with `all passed`. With the BIOS (`package/system/bios.bin`) it also runs Mine Storm and checks the colour editor on its picture.

`vectrex --minestorm --windowed --menu PAGE --frames 300` opens the options menu on a page (0 main, 1/2 controls, 3 stick, 4 picture, 5 machine, 6 sound).

`vectrex --frames N --svg OUT.svg` stops after N frames sent and writes the last one the way the board sees it. That shows the picture settings without a tube. Convert to PNG with `magick OUT.svg OUT.png`.
