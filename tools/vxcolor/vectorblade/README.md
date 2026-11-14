# Vectorblade's colour profile: the scripts that made it

`package/colors/vectorblade.ini` is generated. Run from a scratch folder (the scripts write their pictures and JSON next to
themselves), with `build/pc/roms/vectorblade.bin` (Malban's V1.13) and its calibrated `vectorblade.bin.sav`:

1. `python scan.py` - plays every scene of `scenes.py` with `vxcolor` (immortal: `hold C8B5 33`, level via `hold C823`) and
   dumps `pieces`, `uncoloured` and a shot at each `MARK` into `scan.json` (rename it `scan_main.json`).
   `VB_ANIM=1` (frame by frame, -> `scan_anim.json`) and `VB_BOSS=1` (the four bosses, -> `scan_boss.json`) are the other sets.
2. `python vbtypes.py` - the stroke tables' blocks (`types.json`, sheets `types_N.png`).
3. `python detail.py` - per frame of each enemy, which table entries lie near the figure's centre (`detail.json`). The split is
   found per family over all frames with the same count of entries, so every pose gets the same parts in the second colour.
   It needs `types.json`, `scan_main.json` and `scan_anim.json` next to itself.
4. `python mkprof.py` - writes the profile from `vbrules.py` (by table address `u`), `vbmenu.py` and `detail.json`. This step
   runs in place: the last `detail.json` lies in this folder.

`vbmenu.py` holds the rules of the boot screens and the desktop (logo, intro ship, icons, the man). Those lists lie in bank 0
at addresses other banks use for other figures, so each rule names the caller too. The man's head strokes in his 24 running
frames are worked out from the ROM.

`entries.py [BLOCK..]` numbers the entries of a frame in a picture; `scsheet.py` puts scene shots into a sheet.
