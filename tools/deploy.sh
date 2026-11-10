#!/bin/sh
# deploy.sh - copy the Pi build to the cabinet, put the new program in place and start it there
# (package/run.sh: the VectorPie menu stops while it runs and comes back after). If another game
# has the board, the new build is only installed. Run from the project's folder, in Git Bash, after
# the Pi build (BUILD.md). Arguments go to the emulator (none: its game list).
# (As Major Havoc PE Enhanced's tools/deploy.sh.)
K="-i $HOME/.ssh/id_ed25519_vectorpie -o ConnectTimeout=8 -o BatchMode=yes"
H=pi@192.168.0.122
D=/persistent/games/vectrex
ssh $K $H "mkdir -p $D/save $D/system $D/colors" || exit 1
scp -q $K build/pi/vectrex $H:$D/vectrex.new || exit 1
scp -q $K build/pi/vxcolor $H:$D/vxcolor || exit 1     # (colouring from the command line: docs/AI_GUIDE.md)
scp -q $K package/game.ini package/vectrex.ini package/names.ini package/run.sh $H:$D/ || exit 1
scp -q $K package/system/bios.bin package/system/SP0256-AL2.txt $H:$D/system/ || exit 1
[ -f package/system/sp0256-al2.bin ] && scp -q $K package/system/sp0256-al2.bin $H:$D/system/
scp -q $K package/colors/*.ini $H:$D/colors/ || exit 1
[ -f package/marquee.png ] && scp -q $K package/marquee.png $H:$D/
# Another game on the board (started from the cabinet's menu): install only, do not start ours
# over it (two programs on one board slow both down).
other=$(ssh $K $H 'for p in /proc/[0-9]*; do ls -l $p/fd 2>/dev/null | grep -q ttyACM && tr "\000" " " < $p/cmdline && echo; done' | grep -a -v -e vector_pie_menu -e saver_ -e vectrex)
if [ -n "$other" ]; then
    ssh $K $H "cd $D && chmod 755 vectrex.new run.sh vxcolor && mv -f vectrex.new vectrex && md5sum vectrex && grep '^version' game.ini"
    echo "deploy.sh: installed, not started: another program has the board: $other"
    exit 0
fi
ssh $K $H "cd $D && pkill -INT -x vectrex; for i in 1 2 3 4 5 6; do pgrep -x vectrex > /dev/null || break; sleep 0.5; done; pkill -KILL -x vectrex; chmod 755 vectrex.new run.sh vxcolor && mv -f vectrex.new vectrex && md5sum vectrex && grep '^version' game.ini && (setsid nohup ./run.sh $* > /dev/null 2>&1 &) ; sleep 6; pgrep -x vectrex > /dev/null && echo emulator-running || echo EMULATOR-NOT-RUNNING; tail -n 8 run.log"
md5sum build/pi/vectrex
