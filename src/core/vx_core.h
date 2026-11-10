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

/* What drew a line: the 6809's registers when it ended. X is the vector list being drawn by the BIOS's
 * Draw_VL* routines (it points into the list, just past the vector), PC the routine, U a further list or
 * object pointer some games use. All 0 for a UVMC2 / PiTrex game (the 6809 does not draw). */
typedef struct vx_tag {
    uint16_t pc, x, u;
    uint16_t caller;    /* the game's own code that called the drawing: the first return address on the
                         * stack that is not in the BIOS ($E000-$FFFF); 0 if none in the first 8 words */
} vx_tag;

typedef struct vx_line {
    long x0, y0, x1, y1;    /* canvas units, y down */
    unsigned char z;        /* brightness 0..127 */
    vx_tag tag;
    unsigned t0;            /* the vector's start (console cycles into the frame): a long vector comes as
                             * several lines, all with the same t0 */
    unsigned char rgb;      /* the 3D Imager's filter in front of the eye: red 1, green 2, blue 4, +8 the
                             * other eye; 0 without the goggles */
    uint16_t piece;         /* the piece of the picture it belongs to: what the game draws for one object in one
                             * go (vx_core.c number_pieces) */
    uint16_t part;          /* its stretch of the piece with one X (a list's vector, a table's entry), from 0 */
    uint16_t stroke;        /* its vector in the part, from 0 (a long vector's lines share one) */
} vx_line;

/* What is plugged into controller port 2. */
typedef enum {
    VX_P2_JOYSTICK = 0,
    VX_P2_SPINNER,          /* Atari driving controller style: a Gray code on buttons 1 and 2 */
    VX_P2_TRACKBALL,        /* Atari style: per axis a direction and a clock line (X: buttons 1/2, Y: 3/4) */
    VX_P2_ATARIVOX,         /* speech: the VecVox (SpeakJet) or the VecVoice (SP0256), as the game talks
                             * (an AtariVox+ either way; its EEPROM is not there) */
    VX_P2_VECVOX,           /* (earlier versions' settings: now the same as VX_P2_ATARIVOX) */
    VX_P2_VECVOICE,         /* (the same) */
    VX_P2_COUNT
} vx_port2;

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

void vx_set_input(const vx_pad *p1, const vx_pad *p2);
/* One game frame (until the game's Wait_Recal, or its own frame end). Returns the console cycles it
 * took, for pacing (1.5 million a second). */
unsigned long vx_run_frame(void);

/* The last frame's lines. */
int  vx_lines(const vx_line **lines);
/* The last frame's sound at VX_AUDIO_RATE: *frames samples per channel, *channels 1 or 2
 * (interleaved). Two channels: the console and a second source (a UVMC2's audio jack, a speech
 * module), to be mixed. A speech module's channel is at half the console's scale (psg.c): twice it
 * sounds as the module does. */
const int16_t *vx_audio(int *frames, int *channels);

/* At the end: a flash cartridge's state (high scores) to its .sav. */
void vx_close(void);

/* Port 2. The device: speech modules are the core's (they need the SP0256-AL2 ROM, vx_load_speech_rom,
 * else they stay silent); a spinner or a trackball is the joystick port with its button lines driven by
 * the hook. The hook is called every 500 console cycles while a frame runs (cycle: the console's cycle
 * count) and returns port 2's buttons (bit 0..3 = buttons 1..4 pressed, i.e. the line low); NULL: the
 * buttons of vx_set_input. */
void vx_set_port2_device(vx_port2 d);
void vx_set_port2_hook(unsigned (*hook)(void *user, unsigned long long cycle), void *user);
int  vx_load_speech_rom(const char *path);     /* 0, or -1 (no file, or not the SP0256-AL2) */

/* A byte of the console's RAM ($C800-$CBFF; 0 elsewhere): the colour profiles' game state. */
uint8_t vx_peek_ram(uint16_t addr);
/* A byte into it (tools: vxcolor's poke / hold, e.g. a game's lives kept, to see all of it). */
void vx_poke_ram(uint16_t addr, uint8_t val);

/* The core's mains hum wobble on the lines (a model of a real Vectrex's own): off for a vector monitor,
 * which has its own. */
void vx_set_mains_hum(int on);

#endif
