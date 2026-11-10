# Vectorblade's colour profile: the scripts that made it

`package/colors/vectorblade.ini` is generated. Run from a scratch folder (the scripts write their pictures and JSON next to
themselves), with `build/pc/roms/vectorblade.bin` (Malban's V1.13) and its calibrated `vectorblade.bin.sav`:

1. `python scan.py` - plays every scene of `scenes.py` with `vxcolor` (immortal: `hold C8B5 33`, level via `hold C823`) and
   dumps `pieces`, `uncoloured` and a shot at each `MARK` into `scan.json` (rename it `scan_main.json`).
   `VB_ANIM=1` (frame by frame, -> `scan_anim.json`) and `VB_BOSS=1` (the four bosses, -> `scan_boss.json`) are the other sets.
2. `python vbtypes.py` - the stroke tables' blocks (`types.json`, sheets `types_N.png`).
3. `python detail.py` - per frame of each enemy, which table entries lie near the figure's centre (`detail.json`).
4. `python mkprof.py` - writes the profile from `vbrules.py` (by table address `u`) and the detail rules.

`entries.py [BLOCK..]` numbers the entries of a frame in a picture; `scsheet.py` puts scene shots into a sheet.
