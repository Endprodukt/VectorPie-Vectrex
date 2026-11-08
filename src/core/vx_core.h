/* vx_core.h - the Vectrex emulator core (third_party/vectrex_emu, GPL-3) as a library.
 *
 * The core's own files stay as they come from upstream (hugonl31/Vectrex_EMU). vx_core.c includes
 * its harness, vtest.c, and calls the harness's own functions, so a frame runs exactly as it does in
 * upstream's --live mode (the same frame ends, budgets, BIOS skip and sound).
 *
 * A frame: vx_set_input, then vx_run_frame, then read vx_lines and vx_audio.
 * Coordinates are the core's canvas: x 0..VX_CANVAS_W, y 0..VX_CANVAS_H, y DOWN (the beam may go
 * a little past the edges). */
#ifndef VX_CORE_H
#define VX_CORE_H

#include <stddef.h>
#include <stdint.h>

#define VX_CANVAS_W 35000   /* vecx.h ALG_MAX_X */
#define VX_CANVAS_H 41000   /* vecx.h ALG_MAX_Y */
#define VX_AUDIO_RATE 44100 /* psg.c PSG_RATE */
#define VX_CPU_HZ 1500000   /* vecx.h VECTREX_MHZ */

typedef enum { VX_CART_ROM, VX_CART_UVMC2, VX_CART_PITREX } vx_cart_kind;

typedef struct vx_line {
    long x0, y0, x1, y1;    /* canvas units, y down */
    unsigned char z;        /* brightness 0..127 */
} vx_line;

typedef struct vx_pad {
    int x, y;               /* -128..127, y positive = up (as the Vectrex's port reads it) */
    unsigned buttons;       /* bit 0..3 = buttons 1..4 */
} vx_pad;

/* Load the BIOS (8 KiB) and a cartridge: a ROM image, a UVMC2 game (.um2) or a PiTrex program
 * (.img, .pit). NULL or "" for an empty slot (Mine Storm). 0, or -1 with the reason in err. Once per
 * program run. */
int  vx_open(const char *bios_path, const char *cart_path, char *err, size_t errsize);
vx_cart_kind vx_cart(void);

/* Power on (a cold start). skip_bios: the BIOS's intro runs unseen and unheard until the cartridge
 * starts, so the game starts in the same state as after the intro (live.c skip_boot). */
void vx_power_on(int skip_bios);
/* The console's RESET button (RAM kept). */
void vx_reset_button(void);

void vx_set_input(const vx_pad *p1, const vx_pad *p2);
/* One game frame (until the game's Wait_Recal, or its own frame end). Returns the console cycles it
 * took, for pacing (1.5 million a second). */
unsigned long vx_run_frame(void);

/* The last frame's lines. */
int  vx_lines(const vx_line **lines);
/* The last frame's sound at VX_AUDIO_RATE: *frames samples per channel, *channels 1 or 2
 * (interleaved). Two channels: the console and a second source (a UVMC2's audio jack, a speech
 * module), to be mixed. */
const int16_t *vx_audio(int *frames, int *channels);

/* At the end: a flash cartridge's state (high scores) to its .sav. */
void vx_close(void);

#endif
