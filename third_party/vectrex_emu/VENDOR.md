# Vectrex_EMU core (vendored)

- Source: https://github.com/hugonl31/Vectrex_EMU, `src/core/`, v1.6.1, commit `d96557ea3c377b2f4d8430910279c6b40d30de07` (2026-11-07)
- License: GPL-3.0 (`LICENSE.md`)
- **Unchanged.** Our glue is `src/core/vx_core.c`, which includes `vtest.c`. Changes we need in the core itself should be kept small and listed here, so that a newer upstream can be merged.

## Our changes
- 2026-11-09, `core/speakjet.c`, `core/vecx.c`: port 2 device 7 (`vt_set_analog(27, 7)`, `VT_SPEECHDEV=auto`) = a speech
  module that is the VecVox or the VecVoice as the game talks to it (an AtariVox+): `sp_auto`, `speech_put`; the
  choice in `speech_byte` (a byte from 64 up: the VecVox; three below 64 first: the VecVoice, until one from 64 up
  comes), the ready line open while it is not known (`speech_ready`), `sp_start ("auto")`, `p2_set` takes 7.
