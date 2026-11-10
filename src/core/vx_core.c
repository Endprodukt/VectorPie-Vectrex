/* vx_core.c - the Vectrex core as a library (vx_core.h).
 *
 * Upstream's harness, vtest.c (with live.c and bot.c), holds the frame loop, the line capture and the
 * sound: it is compiled here as part of this file, its main() renamed, and this file's functions call
 * its own (run_frame, skip_boot, apply_input's rules), so nothing in third_party/ changes. vx_open
 * repeats what vtest.c's main does before a run (loading the BIOS and the cartridge, its board).
 *
 * Two things are added around the harness, still without changing it:
 *   - each line's tag (the 6809's PC, X and U when the line ends: X is the vector list being drawn):
 *     vtest.c's vt_line is renamed, and this file's vt_line (the one vecx.c calls) tags the line it adds;
 *   - a frame run in pieces of 500 cycles with a call between them (vx_set_port2_hook: a spinner's or a
 *     trackball's lines change while the frame runs), a copy of vtest.c's run_frame. */
#define main vx_vtest_main_unused
#define vt_line vx_vt_line_upstream
#include "vtest.c"
#undef vt_line
#undef main

#include "vx_core.h"

static vx_cart_kind g_kind = VX_CART_ROM;
static vx_line g_out[MAXL];
static int g_nout;

/* ---------- the lines' tags ---------- */
static vx_tag g_tag_cur[MAXL], g_tag_last[MAXL];
static int g_tag_n;             /* tags recorded for cur[0 .. g_tag_n) */

/* cur was handed to last (a frame ended) since the last line: its tags too */
static void tags_follow(void)
{
    if (ncur < g_tag_n) {
        memcpy(g_tag_last, g_tag_cur, sizeof(vx_tag) * (size_t)g_tag_n);
        g_tag_n = ncur;
    }
}

/* a: a return address, i.e. just past a call (JSR, BSR, LBSR)? Words on the stack that are a game's data
 * (Spike keeps some there) are not. a is in the cartridge, reading it has no side effects. */
static int is_return(unsigned a)
{
    unsigned b2 = e6809_read8(a - 2), b3 = e6809_read8(a - 3), b4 = e6809_read8(a - 4);
    return b3 == 0xBD || b3 == 0x17 ||             /* JSR extended, LBSR */
           b2 == 0x8D || b2 == 0x9D ||             /* BSR, JSR direct */
           b2 == 0xAD || b3 == 0xAD || b4 == 0xAD; /* JSR indexed (no, 8-bit, 16-bit offset) */
}

void vt_line(long x0, long y0, long x1, long y1, unsigned char c, unsigned cyc, unsigned t0)
{
    tags_follow();
    int i = ncur;
    vx_vt_line_upstream(x0, y0, x1, y1, c, cyc, t0);
    if (ncur > i) {
        vx_tag *t = &g_tag_cur[i];
        t->pc = (uint16_t)(e6809_get_pc() & 0xffff);
        t->x = (uint16_t)(e6809_get_x() & 0xffff);
        t->u = (uint16_t)(e6809_get_u() & 0xffff);
        /* the caller: return addresses on the stack (S in RAM), the first outside the BIOS */
        t->caller = 0;
        unsigned s = e6809_get_s() & 0xffff;
        for (int k = 0; k < 16 && s >= 0xC800 && s + 1 <= 0xCBFF; k++, s++) {
            unsigned a = (unsigned)e6809_read8(s) << 8 | e6809_read8(s + 1);
            if (a >= 0x0010 && a < 0x8000 && is_return(a)) { t->caller = (uint16_t)a; break; }   /* (into the cartridge) */
        }
    }
    g_tag_n = ncur;
}

/* ---------- port 2: lines a device drives while the frame runs ---------- */
static unsigned g_p1_buttons, g_p2_buttons;
static unsigned (*g_p2_hook)(void *user, unsigned long long cycle);
static void *g_p2_user;

static void apply_buttons(void)
{
    snd_regs[14] = 0xff & ~((g_p1_buttons & 15) | ((g_p2_buttons & 15) << 4));
}

void vx_set_port2_hook(unsigned (*hook)(void *user, unsigned long long cycle), void *user)
{
    g_p2_hook = hook;
    g_p2_user = user;
}

/* vtest.c's run_frame, in pieces with the port 2 hook between them (its debugging traces left out) */
static void run_frame_hooked(void)
{
    extern unsigned long long alg_cycles;
    frame_done = 0;
    long budget = frame_budget;
    { extern int vt_uvm2, vt_pitrex;
      if (!vt_uvm2 && !vt_pitrex && last_recal && total_cycles - last_recal < 100000) budget = 70000;
      if ((vt_uvm2 || vt_pitrex) && last_busframe && total_cycles - last_busframe < 100000) budget = 70000; }
    while (!frame_done && budget > 0) {
        if (g_p2_hook) { g_p2_buttons = g_p2_hook(g_p2_user, alg_cycles) & 15; apply_buttons(); }
        emu_chunk(500);
        budget -= 500;
    }
    if (!frame_done) {                  /* no Wait_Recal: show what was drawn and start a new frame */
        extern unsigned long long vt_frame_t0;
        memcpy(last, cur, sizeof(line_t) * ncur); nlast = ncur; ncur = 0; beam_swap();
        vt_frame_t0 = alg_cycles;
        { extern unsigned long long vt_piece_t0; vt_piece_t0 = alg_cycles; }
    }
    tags_follow();
}

/* ---------- port 2's device, speech ---------- */
int vx_load_speech_rom(const char *path)
{
    extern int sp_load(const char *);
    return sp_load(path) == 0 ? 0 : -1;
}

void vx_set_port2_device(vx_port2 d)
{
    /* vecx.c's port 2 change while the game runs (vt_set_analog 27): 0 the joystick, 1 the VecVox
     * (SpeakJet; the AtariVox speaks the same way), 2 the VecVoice; a spinner or a trackball is a
     * joystick port driven through the hook */
    extern void vt_set_analog(int id, double v);
    /* (every speech module: 7, the core's auto - the VecVox or the VecVoice as the game talks, an AtariVox+;
     * the VecVox and VecVoice settings of earlier versions are the same now) */
    int core = d == VX_P2_VECVOX || d == VX_P2_ATARIVOX || d == VX_P2_VECVOICE ? 7 : 0;
    /* the speech as a sound of its own (the second channel, psg.c), so that its volume can be set */
    extern int vt_speech_sep;
    vt_speech_sep = core != 0;
    vt_set_analog(27, (double)core);
}

uint8_t vx_peek_ram(uint16_t addr)
{
    if (addr < 0xC800 || addr > 0xCBFF) return 0;   /* (RAM only: a VIA read has side effects) */
    return e6809_read8(addr);
}

void vx_poke_ram(uint16_t addr, uint8_t val)
{
    if (addr < 0xC800 || addr > 0xCBFF) return;     /* (RAM only) */
    e6809_write8(addr, val);
}

void vx_set_mains_hum(int on)
{
    extern int vt_wobble_off;
    vt_wobble_off = !on;
}

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

static unsigned clamp_axis(int v) { v += 128; return (unsigned)(v < 0 ? 0 : v > 255 ? 255 : v); }

void vx_set_input(const vx_pad *p1, const vx_pad *p2)
{
    /* as live.c apply_input: the joysticks' pots, the buttons on the sound chip's port A (low active) */
    alg_jch0 = clamp_axis(p1->x);
    alg_jch1 = clamp_axis(p1->y);
    alg_jch2 = clamp_axis(p2->x);
    alg_jch3 = clamp_axis(p2->y);
    g_p1_buttons = p1->buttons & 15;
    g_p2_buttons = p2->buttons & 15;    /* (a port 2 hook drives these while the frame runs) */
    apply_buttons();
}

/* The picture's pieces, parts and strokes (vx_line piece / part / stroke), the colour editor's units:
 *   a piece  what the game draws for one object in one go: consecutive lines with the same object (U) and
 *            the same calling code, the vector list pointer (X) moving on by at most 8 bytes. (A change of
 *            BIOS routine alone does not part them: games move and draw with two routines in turn.)
 *   a part   a stretch of a piece with one X: one vector of a list (Hyperchase's lamps), or a whole entry of
 *            a game's own table (Armor Attack's buildings)
 *   a stroke one vector of a part, found from the lines themselves (a game drawing without a list, as Tour
 *            de France): the core hands a long vector over in several lines, which follow on from each
 *            other in about one direction; a new stroke where the beam turns or jumps */
static void number_pieces(void)
{
    int piece = -1, part = 0, stroke = 0;
    const vx_line *p = NULL;
    for (int i = 0; i < g_nout; i++) {
        vx_line *l = &g_out[i];
        /* text with the BIOS's font (Print_Str*: X in the font's rows, U the character): one string */
#define VX_FONT(t) ((t).x >= 0xF9D4 && (t).x < 0xFC00)
        int text = p && VX_FONT(l->tag) && VX_FONT(p->tag) && l->tag.caller == p->tag.caller &&
                   (l->tag.pc & ~0x1F) == (p->tag.pc & ~0x1F);
#undef VX_FONT
        if (!text && (!p || abs((int)l->tag.x - (int)p->tag.x) > 8 || l->tag.u != p->tag.u ||
                      l->tag.caller != p->tag.caller)) {
            piece++;
            part = stroke = 0;
        } else if (l->tag.x != p->tag.x) {
            part++;
            stroke = 0;
        } else {
            long ax = p->x1 - p->x0, ay = p->y1 - p->y0, bx = l->x1 - l->x0, by = l->y1 - l->y0;
            long jx = l->x0 - p->x1, jy = l->y0 - p->y1;
            double cr = (double)ax * by - (double)ay * bx, dt = (double)ax * bx + (double)ay * by;
            double la = sqrt((double)ax * ax + (double)ay * ay), lb = sqrt((double)bx * bx + (double)by * by);
            int on = jx * jx + jy * jy <= 60 * 60 && la > 0 && lb > 0 &&
                     dt > 0 && fabs(cr) <= 0.14 * la * lb;          /* (within about 8 degrees) */
            if (!on) stroke++;
        }
        l->piece = (uint16_t)(piece < 0 ? 0 : piece);
        l->part = (uint16_t)part;
        l->stroke = (uint16_t)stroke;
        p = l;
    }
}

unsigned long vx_run_frame(void)
{
    extern unsigned long long alg_cycles;
    unsigned long long c0 = alg_cycles;
    abuf_n = 0;
    run_frame_hooked();
    flash_save(0);
    g_nout = 0;
    int tagged = g_kind == VX_CART_ROM;     /* (a UVMC2 / PiTrex game: the 6809 does not draw) */
    for (int i = 0; i < nlast; i++) {
        vx_line *o = &g_out[g_nout++];
        o->x0 = last[i].x0; o->y0 = last[i].y0;
        o->x1 = last[i].x1; o->y1 = last[i].y1;
        o->z = last[i].c;
        o->t0 = last[i].t0;
        o->rgb = last[i].rgb;
        if (tagged) o->tag = g_tag_last[i];
        else memset(&o->tag, 0, sizeof o->tag);
    }
    number_pieces();
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
