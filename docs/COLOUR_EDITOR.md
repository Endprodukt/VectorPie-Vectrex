# Colour Editor

Colour any Vectrex game. No technical knowledge is needed.

Games are coloured **on a PC** with the **Vectrex Colour Studio** (below), like in a paint program. The cabinet shows the profiles made there and has two simple colour modes of its own. It has no editor.

## On the cabinet

**Tab / F2 → PICTURE**:

| Line | What it does |
|---|---|
| **COLOUR PROFILE** | The game's colours: **OFF** (the game's own look), **ORIGINAL** (the profile that comes with the emulator), **PROFILE 1, 2, 3 …** (your own, made in the Studio) |
| **COLOUR MODE** | Without a profile: **ONE COLOUR** (the whole picture in the colour of the COLOUR line) or **BY BRIGHTNESS** (a colour for each of the game's four brightness levels) |
| **BRIGHTNESS COLOURS** | With BY BRIGHTNESS: a ready set of four colours. The four lines below it change each colour by itself |

## Profiles

Each game can have several colour profiles: OFF, ORIGINAL and your own (PROFILE 1, 2, 3 …).

The Studio never overwrites ORIGINAL:

- On **ORIGINAL** or **OFF**, it saves a **new** profile, starting from the colours you see.
- On one of **your own** profiles, it changes that profile.

## What a colour can count for

The Studio starts with **Same kind**, which is right most of the time. The other choices:

| Choice | Colours |
|---|---|
| Same kind | Everything that looks like it: one coin makes all coins, one wall all walls drawn with it, one enemy all enemies of its kind |
| Whole figure | Everything connected to it: the whole figure. A shape picked whole is coloured everywhere, other lines one by one |
| Shape | This shape wherever it appears (e.g. every mine of this kind) |
| Line | Single lines of the shape |
| Object | Everything belonging to this object (e.g. the ship and its explosion) |
| Group | This kind of object nearby (e.g. mines, but not shots) |
| Code | Everything one part of the game draws (good for games without shapes) |
| Routine | Everything drawn the same way (e.g. all text, all dots) |

Not sure which to choose? Try each one and watch which lines glow.

## Vectrex Colour Studio (PC)

A program for Windows to colour games like in a paint program. Hover any control for help.

1. Start **Vectrex Colour Studio.exe**.
2. **Cabinet → Connect…**: enter the cabinet's address, user and password. The password is not saved. The **Cabinet** tab then lists its games. A game you click there is fetched with its profiles.
3. Play to the scene you want: arrows, **A S D F** for buttons 1–4, or a gamepad. Press **Space** to pause, or click into the picture. **Settings → Controls…** puts every game control and Play/pause, frame back and frame on onto the keys and gamepad buttons you want (click a field, press the key or button).
4. **Click a line**, then **click a colour**. Done. **Same kind** is on at the start: everything that looks like the line gets the colour (one coin: all coins; one wall: the walls; one enemy: all of its kind). Before you click a colour, what will change glows light blue. **Shift+click** adds more lines (their kinds too). You can also **drag a box** around lines.
5. **Whole figure** (next to Same kind) colours everything that touches the line instead. The other modes (Shape, Line, Object, Group, Code, Routine) are under **Advanced modes**, for when you need exactly one kind of rule. **Ctrl+Z** undoes.
6. **Save** (Ctrl+S), then **Cabinet → Send profile and use it there**.

**Which games have colours:** in both game lists a green dot marks a game with an ORIGINAL colour profile, a green ring one with only own profiles; **+N** counts the own profiles. Hover a game for the details.

**Where a game is** (while connected): on the right of each game, **both**, **PC only** or **cabinet only**. A game counts as the same when its checksum is (another file name does not matter), else when the file name is.

**Copying games:** right-click a game: **Copy to the cabinet** (This PC tab) or **Copy to this PC** (Cabinet tab). The buttons on top of each tab, and the Cabinet menu, copy all the missing ones. Games go to the cabinet's `/persistent/games/rom/vectrex` and to this PC's `roms` folder (with the cabinet's own profiles of them). A file of the same name is never overwritten.

**Cabinet → Sync colour profiles…** (or the button in the Cabinet tab) puts this PC's profiles on the cabinet: every ORIGINAL (one that is different there is kept in `colors/old-DATE` first) and your own profiles the cabinet has not got. Own profiles on the cabinet with other rules stay as they are; the message names them.

**Colour by game part** (the ground per level, a different colour in each section):

1. Play to one part of the game, e.g. level 1, and click **Remember part A**.
2. Play on to another part and click **Remember part B**.
3. The Studio lists the bytes of the game's memory that tell the two parts apart. Click the first one, or the next if colours later change at the wrong moment. The Studio keeps it for this game.
4. Tick **Only in this part of the game**. Colour the ground, then go to the next part and colour it again there.

**Change a rule's colour:** click the rule in the **Rules** list, then click a colour. **Done** or Esc goes back to normal colouring.

**Advanced → Any vector list in a range:** for something drawn with a changing list address, e.g. a gauge.

**Advanced options → Any object in a range:** for figures a game draws stroke by stroke from tables (Berzerk Ultimate): every picture of one kind lies in one range of the game's memory. Give the range (hex), then click a colour.

**Advanced → Only where it is now:** for fixed scenery drawn by the same code as the rest (Clean Sweep: the vaults). The colour counts only inside the place the picked lines take up now.

**Advanced → Only lines of a length:** for things the game draws with the same code that differ only in length (Clean Sweep: the money dashes and the walls). With **Length from the picked lines** (on by default) click one line in the picture, or drag a box over a few, then click a colour: it counts for every line about as long as those. Off: drag the two numbers (or double-click to type), in % of the picture's height.

The picture shows clear lines. While something is picked, everything else is dimmed and the picked lines stand out thicker in their own colour. **View → Monitor glow** adds the glow of a vector monitor. **Use the cabinet's picture settings** shows the cabinet's brightness and colours. **Show uncoloured lines** shows in magenta what has no colour yet.

| Key | Does |
|---|---|
| Space | Play or pause |
| , / . | One frame back / on (the last 250 frames are kept) |
| F4 … F9, F10, F11 | WHAT, Whole figure, Same kind |
| Delete | No colour, or remove the rule under the mouse |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+S | Save |
| Mouse wheel, right drag | Zoom, move the picture |

*The editor does not work with UVMC2 or PiTrex games.*
