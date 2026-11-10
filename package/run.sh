#!/bin/sh
# Stop the VectorPie menu (only one program can hold the vector board and the screen; it does not
# leave on a plain kill), run the emulator on the board, then bring the menu back. Arguments go to
# the emulator (none: its game list). (As Major Havoc PE Enhanced's package/run.sh.)
sudo -n systemctl stop getty@tty1
while pgrep -x vector_pie_menu > /dev/null; do sleep 1; done
sleep 1     # the menu is gone a moment before the screen is free
cd "$(dirname "$0")" || exit 1
chmod +x vectrex
VP_OUTPUT=dvg VP_STATS=1 ./vectrex "$@" > run.log 2>&1
sudo -n systemctl start getty@tty1
