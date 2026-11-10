# Vectrex emulator for VectorPie

A cycle-accurate Vectrex emulator (core: Vectrex_EMU, `third_party/vectrex_emu`) for the owner's vector cabinet, VectorPie: a Pi 5 with USB-DVG, `pi@192.168.0.122`, SSH key `~/.ssh/id_ed25519_vectorpie`. The SDK is in `external/VectorPie/sdk`. It must stay UVMC2-compatible.

## Colouring games: read `docs/AI_GUIDE.md`

To colour a game (make or change a colour profile), use **`vxcolor`**, the colour editor as commands with text answers and PNG pictures. `docs/AI_GUIDE.md` has everything: where it is, the workflow, what each command does, profiles, tips. Quick start:

```
build/pc/vxcolor.exe games
build/pc/vxcolor.exe pole "run 100; run 1700 buttons 4; shot a.png labels; figures"
build/pc/vxcolor.exe pole "run 100; run 1700 buttons 4; pick figure 0; colour cyan; shot b.png"   # then: ...; save
```

Look at the PNGs with the Read tool. Only `save` writes; check with `shot` first.

## Documents

| File | What |
|---|---|
| `STATUS.md` | Current state, features, known limits, next steps (German) |
| `RESUME.md` | Notes for continuing the work (German) |
| `BUILD.md` | Builds (PC, Pi), deploy, package, Colour Studio, companion |
| `docs/AI_GUIDE.md` | Colouring with `vxcolor` (for an AI) |
| `docs/COLOUR_EDITOR.md` / `.txt` | Colouring for people: the Colour Studio (PC), the cabinet's colour settings |

## Builds (Git Bash)

- **PC:** `export PATH="/c/msys64/mingw64/bin:$PATH"; cmake --build build/pc`. This builds `vectrex.exe`, `Vectrex Colour Studio.exe`, `vxcolor.exe`, `vxunit.exe`, `vxshot.exe` and `vxtest.exe`.
- **Tests:** `build/pc/vxunit.exe build/pc package/system/bios.bin`. It must end with `all passed`.
- **Pi:** see `BUILD.md` (clang toolchain).
- **To the cabinet:** `sh tools/deploy.sh`. It starts the emulator there.
- **PC companion:** `sh tools/companion.sh make`.

## Rules for working here

- **Answers to the owner in German.** Everything on the screen (emulator, Studio, tools) is in English.
- **Surgical changes.** Keep working code, comments and behaviour. Reuse the existing helpers; the editor's logic lives once, in `src/platform/coloredit.c`.
- **After meaningful work:** update `STATUS.md` and `RESUME.md`, and verify with builds and `vxunit`.
- **Never send simulated keystrokes to the owner's desktop.** Do not take screenshots of the desktop or open windows on it without being asked. The Studio writes its own pictures (`--shot`, `VX_STUDIO_SHOTS`), and `vxcolor` writes PNGs.
- **Do not start programs on the cabinet unless asked,** and do not deploy unless asked. Reading there is fine: `ls`, logs, `vxcolor GAME "...; figures"` without `save`.
- **The SP0256-AL2 speech ROM stays out of the git repository.**
- **Commit or push only when asked.** End commit messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
