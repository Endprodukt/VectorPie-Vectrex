/* vx_core.c - the Vectrex core as a library (vx_core.h).
 *
 * Upstream's harness, vtest.c (with live.c and bot.c), holds the frame loop, the line capture and the
 * sound: it is compiled here as part of this file, its main() renamed, and this file's functions call
 * its own (run_frame, skip_boot, apply_input's rules), so nothing in third_party/ changes. vx_open
 * repeats what vtest.c's main does before a run (loading the BIOS and the cartridge, its board). */
#define main vx_vtest_main_unused
#include "vtest.c"
#undef main

#include "vx_core.h"

static vx_cart_kind g_kind = VX_CART_ROM;
static vx_line g_out[MAXL];
static int g_nout;

static int ends_with(const char *s, const char *ext)
{
    size_t L = strlen(s), E = strlen(ext);
    if (L < E) return 0;
    for (size_t i = 0; i < E; i++)
        if (tolower((unsigned char)s[L - E + i]) != ext[i]) return 0;
    return 1;
}

int vx_open(const char *bios_path, const char *cart_path, char *err, size_t errsize)
{
    crc_init();
    FILE *f = fopen(bios_path, "rb");
    if (!f || fread(rom, 1, 8192, f) != 8192) {
        if (f) fclose(f);
        snprintf(err, errsize, "cannot read the BIOS %s (8192 bytes)", bios_path);
        return -1;
    }
    fclose(f);

    extern int vt_nocart;
    memset(cart, 0xff, sizeof cart);
    size_t n = 0;
    g_kind = VX_CART_ROM;
    if (cart_path && *cart_path) {
        if (ends_with(cart_path, ".um2")) {                 /* native ARM on the cartridge (uvm2_cart.c) */
            extern int uvm2_open(const char *);
            if (uvm2_open(cart_path)) { snprintf(err, errsize, "cannot load the UVMC2 game %s", cart_path); return -1; }
            g_kind = VX_CART_UVMC2;
        } else if (ends_with(cart_path, ".img") || ends_with(cart_path, ".pit")) {   /* pitrex_cart.c */
            extern int pitrex_open(const char *);
            if (pitrex_open(cart_path)) { snprintf(err, errsize, "cannot load the PiTrex program %s", cart_path); return -1; }
            g_kind = VX_CART_PITREX;
        } else {
            f = fopen(cart_path, "rb");
            if (!f) { snprintf(err, errsize, "cannot open %s", cart_path); return -1; }
            n = fread(cart, 1, sizeof cart, f);
            fclose(f);
        }
    }
    vt_nocart = (n == 0);

    /* the cartridge's board by the image's size, as vtest.c's main does with VT_CARTHW unset */
    if (n <= 32768) memcpy(cart + 32768, cart, 32768);     /* a 32K image: both halves */
    int kind = n == 524288 ? 'r' : n > 262144 ? 'm' : n > 65536 ? 'v' : 'p';
    {
        extern int vt_cart_ram; extern unsigned char vt_cram[0x2000];
        vt_cart_ram = n >= 48 && memmem_title(cart, n, "ANIMACTION");
        memset(vt_cram, 0, sizeof vt_cram);
    }
    if (kind == 'm') {
        extern int vt_multicart; vt_multicart = 1;
    } else if (kind == 'v' || kind == 'r') {               /* the flash cartridges: saved next to the ROM */
        extern int vt_vb, vt_hb512;
        if (kind == 'v') { vt_vb = 1; vt_cart_len = n > 65536 ? n : 262144; }
        else { vt_hb512 = 1; vt_cart_len = 524288; }
        static char sav[4096];
        snprintf(sav, sizeof sav, "%s.sav", cart_path);
        vt_flash_file = sav;
        FILE *fs = fopen(sav, "rb");
        if (fs) {
            unsigned char *tmp = malloc(524288);
            size_t m = tmp ? fread(tmp, 1, 524288, fs) : 0;
            fclose(fs);
            if (m == vt_cart_len) memcpy(cart, tmp, m);
            free(tmp);
        }
    }
    { extern int vt_cart_mirror; vt_cart_mirror = !memcmp(cart, cart + 32768, 32768); }

    e8910_init_headless();
    vecx_reset();
    alg_jch0 = alg_jch1 = alg_jch2 = alg_jch3 = 128;

    abuf_cap = 65536;                                      /* live.c's sound buffer: a frame's samples */
    abuf = malloc(abuf_cap);
    if (!abuf) { snprintf(err, errsize, "no memory"); return -1; }
    /* live.c's frame budget: a frame without Wait_Recal ends after this many cycles */
    frame_budget = (g_kind != VX_CART_ROM) ? 30000 : 33000;
    return 0;
}

vx_cart_kind vx_cart(void) { return g_kind; }

void vx_power_on(int skip_bios)
{
    vecx_reset();
    if (skip_bios) skip_boot();
}

void vx_reset_button(void)
{
    extern void vecx_reset_button(void);
    vecx_reset_button();
}

static unsigned clamp_axis(int v) { v += 128; return (unsigned)(v < 0 ? 0 : v > 255 ? 255 : v); }

void vx_set_input(const vx_pad *p1, const vx_pad *p2)
{
    /* as live.c apply_input: the joysticks' pots, the buttons on the sound chip's port A (low active) */
    alg_jch0 = clamp_axis(p1->x);
    alg_jch1 = clamp_axis(p1->y);
    alg_jch2 = clamp_axis(p2->x);
    alg_jch3 = clamp_axis(p2->y);
    snd_regs[14] = 0xff & ~((p1->buttons & 15) | ((p2->buttons & 15) << 4));
}

unsigned long vx_run_frame(void)
{
    extern unsigned long long alg_cycles;
    unsigned long long c0 = alg_cycles;
    abuf_n = 0;
    run_frame();
    flash_save(0);
    g_nout = 0;
    for (int i = 0; i < nlast; i++) {
        vx_line *o = &g_out[g_nout++];
        o->x0 = last[i].x0; o->y0 = last[i].y0;
        o->x1 = last[i].x1; o->y1 = last[i].y1;
        o->z = last[i].c;
    }
    return (unsigned long)(alg_cycles - c0);
}

int vx_lines(const vx_line **lines)
{
    *lines = g_out;
    return g_nout;
}

const int16_t *vx_audio(int *frames, int *channels)
{
    extern int vt_speech_sep, vt_vecvoice, vt_uvm2_cart, vt_imager;
    int two = (vt_speech_sep && vt_vecvoice) || vt_uvm2_cart || vt_imager;   /* as emu_chunk writes it */
    *channels = two ? 2 : 1;
    *frames = abuf_n / (2 * *channels);
    return (const int16_t *)abuf;
}

void vx_close(void)
{
    flash_save(1);
}
