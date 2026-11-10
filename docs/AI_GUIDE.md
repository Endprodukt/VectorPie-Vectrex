# Colouring Vectrex games: the guide for an AI

This is everything an AI assistant (Claude or another) needs to colour Vectrex games for the VectorPie cabinet's emulator. The tool is **`vxcolor`**: the colour editor as commands, with answers in plain text and pictures (PNG) to look at.

## Where it is

| Place | Program | Games, profiles |
|---|---|---|
| This PC (development) | `build/pc/vxcolor.exe` | `build/pc/roms/`, `build/pc/save/colors/` |
| PC companion | `Vectrex Companion/vxcolor.exe` | its `roms\`, `save\colors\` |
| The cabinet (over SSH) | `/persistent/games/vectrex/vxcolor` | the cabinet's own games and profiles |

On the cabinet, run it there over SSH: `ssh -i ~/.ssh/id_ed25519_vectorpie pi@192.168.0.122 '/persistent/games/vectrex/vxcolor pole "..."'`. A profile saved there is used by the cabinet at once, so no `send` is needed. `shot` then writes the PNG on the cabinet; fetch it with `scp` to look at it.

`vxcolor help` prints every command.

## How it works

- `vxcolor GAME "COMMAND; COMMAND; ..."`: one run starts the game, plays it as told, does the commands and ends. The core is deterministic, so the same `run` commands always reach the same frame. Plan each step as a fresh run that repeats the `run` part.
- **Only `save` keeps colours.** A run without `save` changes nothing on disk. After `save` the game uses that profile, and the next run starts with it.
- **Coordinates are percent of the Vectrex's picture**: x from 0 (left) to 100 (right), y from 0 (top) to 100 (bottom). These are the same as in the PNGs. Values just outside 0–100 occur, because some lines run off the screen.
- `GAME` is a file, or a game's id or name from `vxcolor games`.

## The workflow

```
vxcolor games                                             # which games, which have profiles
vxcolor pole "run 100; run 1700 buttons 4; frame; shot a.png labels; figures"
#   look at a.png: every figure has its number in its middle; `figures` lists them (box, lines, rules)
#   here (checked): figure 0 is the player's car, figures 10-13 the billboard
vxcolor pole "run 100; run 1700 buttons 4; profile off; pick figure 0; colour cyan; pick figure 10,11,12,13; colour yellow; shot b.png"
#   look at b.png (the far billboard is yellow too: a shape rule colours every copy); then the same with save
vxcolor pole "run 100; run 1700 buttons 4; profile off; pick figure 0; colour cyan; pick figure 10,11,12,13; colour yellow; save"
vxcolor pole "run 600; shot c.png; uncoloured"            # other scenes: the rules work everywhere
vxcolor pole "send use"                                   # (PC) to the cabinet, and use it there
```
(`profile off` starts from no colours; leave it out to build on the profile the game uses, e.g. ORIGINAL.)

```

1. **Reach the scene.** `run N` plays N frames (50 a second). Add `stick X,Y` (each -1..1, y 1 = up) and `buttons 1234` (the digits of the buttons held). Example: `run 100; run 1700 buttons 4` holds button 4 (Pole Position's gas) from frame 100. Take a `shot` after a few runs to see where the game is. Title screens come first, a game often starts with button 1–4.
2. **Look.** Use `shot FILE.png labels` and read the image. `figures` gives, for each figure, its number, line count, box (x0,y0–x1,y1 in %), centre, piece and the rules that colour it now. `lines N` lists one figure's lines.
3. **Pick.** The default is `what smart`. Then use one of:
   - `pick figure N[,N…]` for whole figures (the most robust way),
   - `pick at X Y` for the line nearest a point (smart: its whole figure),
   - `pick box X0 Y0 X1 Y1` for the lines inside a box,
   - `pick line I[,I…]` for single lines,
   - `add …` to add more of any of these.

   `picked` shows what is picked, which rules a colour would make and how many lines those rules colour in this frame.
4. **Colour.** Use `colour NAME [xGAIN]`, with names from `palette` (`SKY_BLUE` or `sky_blue`), or a hex `RRGGBB` for the nearest palette colour. A GAIN of 0.5–3 changes brightness, e.g. `colour red x1.5`. `uncolour` removes the rules colouring the pick.
5. **Check.** Take another `shot`. Use `uncoloured` for the figures that still have lines without a rule, and `rules` for the profile (`delete rule N` removes one). Check several scenes and frames: a rule colours every frame where it matches, including other levels, attract mode and explosions.
6. **Save.** `save` writes the game's own profile (`save/colors/<id>.ini`, `<id>.2.ini` …). It never overwrites ORIGINAL. `profiles` shows where it writes, `profile new` makes it write a new number.
7. **To the cabinet** (from a PC): `send` copies the saved profile, `send use` also selects it there. This needs the cabinet's address, user and key in `studio.ini` next to the program (the Colour Studio writes it). It uses the key `~/.ssh/id_ed25519_vectorpie`, or a password from the environment variable `VX_CABINET_PASSWORD`. A different file of the same name there is kept in `save/colors/old-<date>/` first.

## Game state: immortal, anywhere, a colour per level

Some games need more than playing: dying sends you back, or levels change the colours.

- **Read and write the RAM:** `ram ADDR N`, `poke ADDR VAL`, and `hold ADDR VAL`, which keeps a byte during every `run`.
- **Find an address:**
  - `snap`, play, then `changed` shows every byte that changed.
  - `find VAL` lists the addresses holding a value.
  - Example: the lives counted down from 04 to 02 to 01 at one address.
- **Never die, see everything:**
  - Hold the lives and the fuel.
  - Set the course position (often 16-bit) with `poke`, or `hold` its upper byte, to reach any part of the game.
- **A colour per level:** `colour NAME when ADDR=VAL`, also `ADDR&MASK=VAL` and `ADDR!=VAL`. Several `when` must all hold. Each rule gets its own name (`code_02E3_0_90`).
- **A list address that changes** (a gauge): `colour NAME xrange LO-HI` widens the rule to any vector list in the range.
- **Sprites drawn stroke by stroke from a table** (Berzerk Ultimate): `colour NAME urange LO-HI` widens the rule to any object (U) in the range. `pieces` shows each piece's object.
- **Long and short lines from one code** (walls and dots): `colour NAME length LO-HI` limits the rules to lines LO to HI long, in % of the picture's height (rule condition `length`, in canvas units).
- **Fixed scenery in one place** (a maze's vaults): `pick box …` then `colour NAME area X0-X1 Y0-Y1` (% of the picture, as `pick at`) limits the rules to lines wholly inside the box (rule condition `area`, canvas units). Only for things that never move.
- **One kind of object anywhere:** `pick list ADDR` picks every piece drawn from the list at ADDR. Use the address of a `shape_…` name, or the `list` that `pieces` shows.
- **`pick none` clears the pick,** so a `shot` shows no glow.
- **`recolour rule N NAME [xGAIN]`** changes one rule's colour only (see `rules`). Use it to rework a palette without picking again.

## WHAT: how a pick becomes rules

A **rule** colours lines by where the game's code draws them. It does not go by position, so it works wherever the object moves and in every frame. **The first matching rule wins.**

| what | the rule colours | use it for |
|---|---|---|
| `smart` (default) | Each shape picked whole gets a shape rule (all its copies). Other picked lines get one line rule each. | Almost everything: pick figures, colour them. |
| `kind` | The line's shape, narrowed to what looks like it (the Studio's default, "Same kind"): figures of another build from one list in RAM split by their objects; many small marks drawn with bigger lines (coins with walls) split from them by length. | One click per kind of thing: coins, walls, enemies. The rule names say what was narrowed (`_u…`, `_marks`, `_big`). |
| `shape` | The shape (vector list) wherever drawn. | All enemies of one kind. |
| `line` | Exactly these lines of the shape, in every copy. | Details: a roll bar, a wheel, one edge. |
| `object` | Everything drawn for that object (6809's U register). | A ship with its flame and explosion. |
| `group` | This kind of object near the picked one. | Mines but not shots. |
| `code` | Everything one place of the game's code draws. | Games drawing without shapes (e.g. track edges). |
| `routine` | Everything drawn by the same routine. | All text, all dots. |

`smart` figures are lines whose ends lie within about 1 % of each other and that belong to the same object or piece. Lines of different objects are never joined, even when they touch.

## Profiles

- **ORIGINAL** is `package/colors/<id>.ini`, shipped with the emulator and never written by the tools. Your own profiles are `save/colors/<id>.ini` (PROFILE 1), `<id>.2.ini` (PROFILE 2) and so on. The cabinet's menu chooses between them (PICTURE → COLOUR PROFILE).
- A game finds its ORIGINAL by its **checksum** first (CRC32 of the cartridge without the padding at its end; `vxcolor` prints it as `checksum …`), then by its **id: the cartridge's file name**, lower case, without extension, only `a-z 0-9 _` (`Pole Position.vec` → `pole_position`). Own profiles go by the id too, and when there are none of the id, by an own profile listing the checksum (a renamed cartridge keeps its own profiles). A profile lists its cartridges in a line `crc = 1A2B3C4D 5E6F7A8B` (several for related games: Berzerk's profile also lists Verzerk). `save` adds the game's checksum. "other_version" in `profiles` means a profile of the name made for another cartridge: its rules may miss.
- **The file format** is plain text, one rule per line: `name = CONDITIONS : RRGGBB [*GAIN]`, for example `code_0A70 = pc F420-F43F caller 0A70-0A70 : FF3C32`. Conditions are `x`, `pc`, `u`, `caller` (hex ranges), `part`, `stroke` (numbers), `dot`, `line`, `bright LO-HI`, and `ram ADDR=VAL` (a game state, e.g. a level). The first matching rule wins. vxcolor writes these files; editing them by hand is possible (`render/colors.h` has the details).

## Tips

- **Start with the big things.** Colour the player, then the enemies, then the scenery, then the text and score (`routine` or `code` for all text).
- **Prefer believable colours over loud ones** (the owner's verdict on a first, too colourful Scramble): earth, rock and concrete for scenery, metal (silver, steel grey) for machines, strong colours only for fire, lasers and the few things the player must see at once.
- **Colours should mean something,** as in the arcade original if there is one: the player in one clear colour, the enemies in contrasting ones, the background calm (greys, dark greens, blues).
- **Dim lines on the monitor:** dark palette colours show dim on a vector monitor. Use `x1.3`–`x2` for thin details that must be seen.
- **Some games draw a figure in parts on different frames,** e.g. every other frame. Look at `run N` and `run N+1`.
- **`figures` numbers change between frames.** Pick by number only in the run that listed them (same `run` commands), or by `pick at`.
- **A shape rule colours all copies.** Two flags drawn from one list get the same colour. To tell copies apart use `what line` (each line of each copy is in the same list, so this still colours all copies), or `object`/`group` when the copies are different objects.
- **A profile holds 512 rules at most.** `smart` keeps the count low (one rule per shape).
- **Games that cannot be coloured:** UVMC2 and PiTrex games (`.um2`, `.pit`), whose pictures do not come from the 6809. vxcolor says so.

## Lessons from real games

**Berzerk** (2026-11-09, `package/colors/berzerk.ini`, made with vxcolor only, after the box art):

- **Find the parts first.** Use `pieces` and `lines N`, and group the lines by piece and position: which piece is the head, the legs, the eye. One `smart` pick can hide that a figure is made of several lists.
- **Lists that change with animation:** the robot's eye is a different list for each direction it looks, so a shape rule caught only one direction. `what code` on the eye piece caught every direction: one place in the code draws all of them.
- **Order is a tool.** The eye's code also draws the walls and the player's arms. Colour the broad rule first (eyes yellow by code), then the narrower ones (walls, player by shape) afterwards. Later rules go on top, and the first match wins.
- **`group` / `object` are tied to this room's objects.** In the next room the robots were other objects and stayed white. Use `code` for things drawn by fixed code (Berzerk's robot body, legs and feet: three code rules). **Always check a second room or level.**
- **A shape shared by parts:** body and feet came from the same list, so colouring the feet re-coloured the body (the rule was replaced). Watch `coloured: … rules` and `rules`.
- **Text drawn by the BIOS without the game's code** (the title screen) has no caller. `code` then becomes `routine` and colours all BIOS text everywhere. Leave such text white, and check `rules` for a `routine_…` rule you did not mean (`delete rule N`).
- **Scenes to check:** the room at the start, a second room (`run 1200`), shots (`stick` + `buttons 4`), explosions, the "GOT YOU HUMANOID" screen after a lost life (`run 3800`), the "PLAYER" screen before a turn (`run 750; run 10 buttons 1; run 40`), the title (`run 700`). Use `uncoloured` in each one.
- **Evil Otto** (a hint from the owner): stand still and wait. With `run 750; run 10 buttons 1; run 900` he catches the player at about frame 1660. He was yellow already, because the eyes' code (`0936`) draws him too.
- **Details inside a figure** (Otto's eyes): list the figure's lines with `lines N`, group them by `part` and position (two parts at the same height left and right were the eyes), then `what line; pick line I,I,…; colour …`. Line rules colour exactly these parts of the list, wherever the figure appears.
- **`shot` shows picked lines with a yellowish glow.** Judge colours in a run without a pick.
- **`uncoloured` cannot find a thing a rule colours already.** Look at the pictures too, zoomed: `shot FILE width 3000`, then crop the place (Otto showed only like that).
- **Dim text:** the scores showed dim; `x1.4` helped.
- **The game keeps playing on its own.** Robots walk to the player and kill him after about 10 s, and the turns then alternate between player 1 and 2 (the lives' code changes from `0516` to `050E`). Shooting around (`stick` + `buttons 4`) did not keep him alive.

**Cosmic Chasm** (2026-11-09, `package/colors/chasm.ini`):

- **One list, three things:** the chamber's octagon, the enemies and the map's core use the same vector list. `code` separates them (each is drawn by its own code). Keep the shape rule (the map's core) *under* the code rules: colour it first, or in an earlier saved run.
- **Building in runs:** colour one screen, `save`; the next run starts with that profile and adds on top. Later runs' rules win where they overlap.
- **Frame numbers depend on the inputs:** when a scan with input found something at frame F, repeat the *same* inputs up to F to pick it (counting the rounds of a loop), not a shorter version.
- **Detail without parts:** the enemy explosion's rays each are one part of the list, so the centre and the rays could not be split. Colouring the straight rays yellow and the diagonal ones orange (`what line`, pick one ray of each) gave a fire star instead.
- Scan with `uncoloured` in long loops: the attract mode (no input) and play (`stick` + `buttons`), each about 9000 frames.

**Scramble** (2026-11-09, `package/colors/scramble.ini`, 36 rules):

- **Two codes draw nearly everything.** `0285` draws every object, `02E3` the ground, the fuel gauge and the score. `code` was useless here; `shape` (each object's ROM list) made the objects: `pick list` per kind.
- **Immortal:**
  - `C8FE` = lives (found with `snap` and `changed`), `C8EE` = fuel.
  - `C8EF:C8F0` = the 16-bit course position, not a level number. Its pages hold the sections: 00 mountains, cave and fireballs; 01 city; 02 walls; 03 base; 04 second maze.
  - The game resets the position to the section's start after a crash.
  - To see any place: `run 1150; hold C8FE 04; hold C8EE 40; hold C8EF <page>; poke C8F0 <pos>; run 300`.
- **Ground per section:** a grey rule first, then `when C8EF=00 when C8F0&F0=90` and so on, one rule per section and colour.
- **Gauge:** code `02E3` with `xrange 0013-00FF` and `0000-0011`. The flat ground's X is `0012` and stays out.
- **Laser shots** are dots drawn by the ground's code: `what routine` on a dot piece makes a routine rule with the `dot` condition. Without fire no dot pieces exist, so it is safe.
- **Pitfalls:**
  - Two objects drawn from one shape (a crate on its pedestal) share one rule: the later colour replaces the earlier.
  - Re-colouring a shape moves its rule to the top, above line rules of its details. Colour the details again afterwards.
  - Check with a zoomed `shot`, `rules` and `uncoloured` (all sections, with fire: `stick` + `buttons`).

**Clean Sweep** (2026-11-09, `package/colors/sweep.ini`, 17 rules):

- **Money and walls from one code:** the game draws the money dashes and the maze's vertical walls from one RAM list with one code (`0096`). Only the length tells them apart: `colour light_green length 0-2` and `colour tan length 2-100` (a dash is about 1 %, a wall stub 3 % or more).
- **`part` is useless for things that disappear:** when money is eaten, the later parts move up one number. Check with a game where things get eaten before trusting a `part` rule.
- **Find out who the player is:** move the stick and see which piece follows it. The dotted fish shape (U `C904`) is the vacuum; the V shapes are the robbers (U `C932`, `C960`, … one slot each). A first guess by the shape was wrong.
- **Vacuum and robbers:** same RAM list, same code `096B`. A `code` rule (white) for all robbers, the vacuum's `object` rule (red) on top of it.
- **Vaults and corner rooms** (yellow, the owner's wish): drawn by the wall codes; `pick box` + `area` per vault/room, one rule per code and place. The outer frame lines run past the rooms (one line each) and stay sandstone.
- **Immortal:** `hold C880 03` (the lives).
- Checked: title, select, play with lives held (GAME 1 and GAME 2, ten checks each up to about frame 11000 and 19000), game over.

**Web Wars** (2026-11-09, `package/colors/webwars.ini`, 14 rules, after the Web Warp overlay):

- **`what kind` first:** one pick per kind of thing did the hawk, the near dragon, the drones, the web's spokes and rings (all ring stages), the text.
- **`kind` cannot do "all of a family":** the 20 creatures are 20 shapes drawn by the drones' code. Use `what code` on a creature, then pick a drone again with `what kind` so its rule lies on top.
- **Tables in RAM:** each shot is its own entry, so `kind` picks one shot; `what code` takes them all (here the score shares that code).
- **One thing, several lists:** the dragon far away (dots) and near, and its head, have their own lists and codes: `what code` per part.
- **Immortal:** `hold C8BD 05`.

**Berzerk Ultimate** (2026-11-09, `package/colors/berzerk_ultimate.ini`, 55 rules):

- **Sprites from stroke tables:** robots and player are drawn one stroke at a time; U points at the stroke in a ROM table, and each animation picture has its own U range. Find the ranges: dump `pieces` over many frames and group the objects (gaps of more than 8 bytes start a new picture). Parts of a figure are fixed positions in each picture: the robot's visor = its last 3–4 strokes, the player's head and body = the first 11–12.
- **Who is the player:** check which code's figure follows the stick over several frames; a robot walking the same way fooled a first guess.
- **Text per line:** the game's own font is drawn by one code per text line, so `what code` colours each line apart (headers, items, values).
- **Several codes for one thing:** walls during a room change (`0535`, `018D`), a robot drawn by a second code (`0A94`), the MALBAN logo by four codes. Scan many games with different stick patterns.
- **BIOS text** (score, lives, the end screen) is one shape: colour the end screen with `what kind`, then pick score and lives again with `what code` so their rules lie on top.
- Do not trust a `hold` on a guessed lives byte: holding the wrong byte stopped the game.

**Vectorblade** (2026-11-09, `package/colors/vectorblade.ini`, 270 rules, generated by `tools/vxcolor/vectorblade/`):

- **Hundreds of sprite frames:** every enemy has 10-20 rotation frames, each a block of a ROM stroke table (U). Clicking was hopeless; a script dumped `pieces` over 767 scenes, grouped the U values into blocks (gaps over 6 bytes) and gave each family one `u` range rule.
- **Detail in rotating sprites:** the entry order changes between rotation frames, so "entry k" is not the same part everywhere. What works: per frame, the entries whose strokes lie near the figure's centre (from the scenes) get a second colour.
- **Reaching everything:** lives `hold C8B5 33`, a level by `hold C823 <L-1>` until the first level ends; bosses at level counter 25/51/77/103 need the shield too (`hold C864 40`).
- **`pieces` lists at most 512 pieces;** Vectorblade's help pages have more. Use `what line; pick figure N; picked` to see the code of what is left.
- **A flash cartridge's save** is read by `vxcolor` but never written.

**Profiles for related games:** a hack or an extended version can keep the original's code at the same addresses, so the original's profile works unchanged. Verzerk takes Berzerk's profile in full; Berzerk Ultimate, a rewrite, takes none of it. To test: copy the profile to `save/colors/<id>.ini`, then run `vxcolor <id> "profile 1; run …; rules"` in a game scene. Rules with `N lines now` above 0 match; then check `uncoloured`. The file size is a first hint: a rewrite is usually much bigger or smaller.

## Other tools (for reference)

- **Vectrex Colour Studio** (`Vectrex Colour Studio.exe`): the same editing for people, with mouse and palette, on Windows.
- **`vxshot`**: older scripted editing by piece numbers, `--edit "PIECE[.N]:SCOPE:COLOUR"`.
- **The cabinet** has no editor: it shows the profiles (PICTURE, COLOUR PROFILE) and has its own simple colour modes. See `docs/COLOUR_EDITOR.md`.
