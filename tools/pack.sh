#!/bin/sh
# pack.sh - the VectorPie package (.vpk) of the Pi build: the program, its settings, the game names,
# the BIOS. Run from the project's folder, in Git Bash, after the Pi build. Writes
# build/vpk/vectrex-<version>.vpk (bump version in package/game.ini first).
# (As Major Havoc PE Enhanced's tools/pack.sh.)
set -e
S=build/vpk/stage/vectrex
rm -rf build/vpk/stage
mkdir -p $S/system $S/colors
cp build/pi/vectrex build/pi/vxcolor $S/
cp package/game.ini package/vectrex.ini package/names.ini package/run.sh $S/
cp package/system/bios.bin package/system/SP0256-AL2.txt $S/system/
[ -f package/system/sp0256-al2.bin ] && cp package/system/sp0256-al2.bin $S/system/
cp package/colors/*.ini $S/colors/
[ -f package/marquee.png ] && cp package/marquee.png $S/
sh external/VectorPie/sdk/tools/vp-pack/vp-pack.sh $S build/vpk
