#!/bin/sh
# companion.sh - the PC companion: Vectrex Colour Studio (src/studio/studio.cpp: the colour editor with
# mouse, palette and the cabinet's connection) and the emulator for Windows in a window.
# Run from the project's folder, in Git Bash, after the PC build (BUILD.md).
#
#   sh tools/companion.sh make    build/companion/Vectrex Companion/ (and a .zip of it)
#   sh tools/companion.sh pull    from the cabinet: its cartridges (so the games have the same ids as
#                                 there: the profiles go by the cartridge's file name) and its own
#                                 profiles (only those the PC does not have)
#   sh tools/companion.sh push    to the cabinet: the PC's own profiles; a profile the cabinet has and
#                                 that differs is kept there in save/colors/old-<date>/ first
#
# Own profiles are save/colors/<id>.ini, <id>.2.ini ... (render/colors.h). On the cabinet a profile
# pushed is chosen in its menu (PICTURE, COLOUR PROFILE).
set -e
C="build/companion/Vectrex Companion"
K="-i $HOME/.ssh/id_ed25519_vectorpie -o ConnectTimeout=8 -o BatchMode=yes"
H=pi@192.168.0.122
D=/persistent/games/vectrex

case "$1" in
make)
    rm -rf "$C/colors"                          # (the shipped profiles as they are now; save/ and roms/ stay)
    mkdir -p "$C/system" "$C/colors" "$C/roms" "$C/save/colors"
    cp build/pc/vectrex.exe "build/pc/Vectrex Colour Studio.exe" build/pc/vxcolor.exe build/pc/SDL2.dll "$C/"
    cp docs/AI_GUIDE.md "$C/"
    cp third_party/imgui/LICENSE.txt "$C/imgui-LICENSE.txt"
    cp package/game.ini package/vectrex.ini package/names.ini "$C/"
    cp package/system/bios.bin package/system/SP0256-AL2.txt "$C/system/"
    [ -f package/system/sp0256-al2.bin ] && cp package/system/sp0256-al2.bin "$C/system/"
    cp package/colors/*.ini "$C/colors/"
    cp docs/COLOUR_EDITOR.txt "$C/"
    printf '@echo off\r\ncd /d "%%~dp0"\r\nstart "" vectrex.exe --windowed\r\n' > "$C/Vectrex Companion.bat"
    printf 'Vectrex Companion\r\n\r\nVectrex Colour Studio.exe: colour games with the mouse, connect to the cabinet and\r\nsend your profiles there (Cabinet menu). Hover any control for help.\r\nVectrex Companion.bat: the emulator itself in a window, to see a profile as on the cabinet.\r\nGames: roms\\ (or fetch them in the Studio from the cabinet). Your profiles: save\\colors\\.\r\nGuide: COLOUR_EDITOR.txt.\r\n' > "$C/README.txt"
    (cd build/companion && rm -f "Vectrex Companion.zip" && (command -v zip > /dev/null && zip -qr "Vectrex Companion.zip" "Vectrex Companion" ||
        powershell -NoProfile -Command "Compress-Archive -Force -Path 'Vectrex Companion' -DestinationPath 'Vectrex Companion.zip'"))
    echo "companion.sh: $C (and build/companion/Vectrex Companion.zip)"
    ;;
pull)
    mkdir -p "$C/roms" "$C/save/colors"
    for dir in /usr/local/share/advance/image/vectrex /persistent/games/rom/vectrex; do   # (later wins, as on the cabinet)
        ssh $K $H "[ -d $dir ]" && scp -q $K "$H:$dir/*" "$C/roms/" 2>/dev/null || true
    done
    tmp=build/companion/pulled; rm -rf $tmp; mkdir -p $tmp
    scp -q $K "$H:$D/save/colors/*.ini" $tmp/ 2>/dev/null || true
    for f in $tmp/*.ini; do
        [ -f "$f" ] || continue
        b=$(basename "$f")
        if [ -f "$C/save/colors/$b" ]; then echo "kept the PC's $b"; else cp "$f" "$C/save/colors/"; echo "pulled $b"; fi
    done
    rm -rf $tmp
    echo "companion.sh: $(ls "$C/roms" | wc -l) cartridges in $C/roms"
    ;;
push)
    stamp=$(date +%Y%m%d-%H%M%S)
    ssh $K $H "mkdir -p $D/save/colors"
    for f in "$C"/save/colors/*.ini; do
        [ -f "$f" ] || continue
        b=$(basename "$f")
        # a different profile of the same name on the cabinet: kept in old-<date>/ first
        ssh $K $H "cd $D/save/colors && if [ -f '$b' ]; then mkdir -p old-$stamp && cp -p '$b' old-$stamp/; fi"
        scp -q $K "$f" "$H:$D/save/colors/$b"
        echo "pushed $b"
    done
    ;;
*)
    echo "usage: sh tools/companion.sh make | pull | push"; exit 1 ;;
esac
