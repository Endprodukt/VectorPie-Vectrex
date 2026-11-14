/* vxcolor - colouring Vectrex games from the command line: the colour editor for a program (an AI such as
 * Claude, a script) instead of a person. Everything the Colour Studio and the cabinet's editor do, as
 * commands; it answers in plain text and writes pictures (PNG, with the figures' numbers) to look at.
 * The guide: docs/AI_GUIDE.md. `vxcolor help` prints the commands.
 *
 *   vxcolor games
 *   vxcolor GAME "COMMAND; COMMAND; ..." [more commands as further arguments, or @FILE: one per line]
 *
 * One run plays the game from the start (the core is deterministic: the same commands give the same
 * frame), does the commands and ends. Changes to the profile are kept only by `save` (the game's own
 * profile: save/colors/<id>[.k].ini, as the Studio and the cabinet's editor write it, never ORIGINAL).
 * Coordinates are percent of the Vectrex's picture: x 0 left .. 100 right, y 0 top .. 100 bottom, the
 * same as in the PNG `shot` writes. */
#include "core/vx_core.h"
#include "platform/coloredit.h"
#include "platform/config.h"
#include "platform/roms.h"
#include "render/colors.h"
#include "render/draw.h"

#include <vectorpie/vp_paths.h>

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0755)
#endif

static const char *HELP =
"vxcolor - colour a Vectrex game from the command line (guide: docs/AI_GUIDE.md)\n"
"\n"
"  vxcolor games                         the games (id, name, file, own profiles)\n"
"  vxcolor GAME \"CMD; CMD; ...\" [@FILE]   GAME: a file, or a game's id / name\n"
"\n"
"Coordinates: percent of the picture, x 0 left..100 right, y 0 top..100 bottom (as in `shot`).\n"
"Each run starts the game afresh; only `save` keeps the colours.\n"
"\n"
"Playing\n"
"  run N [stick X,Y] [buttons 1234]      N frames (50 a second); stick -1..1 each way (y 1 = up);\n"
"                                        buttons: the digits of the buttons held (e.g. 4, 13)\n"
"  frame                                 frame number, lines lit, figures\n"
"Looking\n"
"  shot FILE.png [labels] [width W]      the picture with the colours; labels: each figure's number\n"
"  figures                               every figure (lines connected): number, lines, box, colour, rules\n"
"  lines FIGURE|all                      the lines of one figure (all: every lit line): index, ends, piece,\n"
"                                        what drew it (routine pc, list x, object u, caller), rule\n"
"  uncoloured                            figures with lines no rule colours\n"
"  palette                               the colour names\n"
"Picking (what the colour goes to)\n"
"  what smart|kind|shape|line|object|group|code|routine  how a pick becomes rules (default smart;\n"
"                                        kind: all like the line, the Studio's Same kind)\n"
"  pick figure N[,N...]                  whole figures      pick at X Y   the line there (smart: its figure)\n"
"  pick line I[,I...]                    single lines       pick box X0 Y0 X1 Y1   the lines inside\n"
"  pick piece N                          a piece (pieces: see `pieces`)\n"
"  pick list ADDR                        every piece drawn from the vector list at ADDR (hex, as in\n"
"                                        a rule's name shape_0E5E): one kind of object, wherever it is\n"
"  pick none                             nothing picked (shot then shows no glow)\n"
"  add figure N | add line I | add at X Y | add box X0 Y0 X1 Y1     more to the pick\n"
"  picked                                what is picked, the rules it would make, the lines they colour\n"
"  pieces                                the picture's pieces (one go of the game's drawing)\n"
"Colouring\n"
"  colour NAME [xGAIN]                   the pick gets it (NAME from `palette`, _ for spaces, or RRGGBB:\n"
"                                        the nearest palette colour); GAIN 0.5..3 brightness\n"
"  uncolour                              the rules colouring the pick removed\n"
"  rules                                 the profile's rules (first match wins)\n"
"  delete rule N                         one rule removed\n"
"  recolour rule N NAME [xGAIN]          one rule's colour changed (its lines stay the same)\n"
"Game state (RAM $C800-$CBFF)\n"
"  ram ADDR [N]                          N bytes from ADDR (hex)\n"
"  poke ADDR VAL                         one byte written (hex)\n"
"  hold ADDR VAL | hold off              a byte kept at VAL during every run (e.g. lives: never dying)\n"
"  snap | changed                        remember the RAM | the bytes changed since (old -> new)\n"
"  find VAL                              the addresses holding VAL now (hex)\n"
"  colour NAME ... when ADDR=VAL         the rules only while the byte is VAL (also ADDR!=VAL,\n"
"                                        ADDR&MASK=VAL): e.g. a colour per level; several when: all\n"
"                                        must hold\n"
"  colour NAME ... xrange LO-HI          the rules for any vector list in LO-HI (hex): a gauge or an\n"
"                                        object whose list address changes\n"
"  colour NAME ... urange LO-HI          the rules for any object (U) in LO-HI (hex): sprites drawn row\n"
"                                        by row from tables, one range per kind (Berzerk Ultimate)\n"
"  colour NAME ... length LO-HI          the rules only for lines LO to HI long (% of the picture's\n"
"                                        height, e.g. 3-100): walls and dots drawn by the same code\n"
"  colour NAME ... area X0-X1 Y0-Y1      the rules only for lines wholly inside the box (% of the\n"
"                                        picture, as `pick at`): fixed scenery in one place\n"
"Profiles\n"
"  profiles                              the game's profiles, the one shown, where `save` writes\n"
"  profile off|original|N|new            show another one (N: own PROFILE N); new: save as a new one\n"
"  save                                  write the profile (own; the game then uses it)\n"
"  send [use]                            (PC) to the cabinet over SSH (cabinet.ini / studio.ini: host,\n"
"                                        user, key; password: environment VX_CABINET_PASSWORD); use: the\n"
"                                        cabinet's game then uses it\n";

/* ------------------------------------------------------------------------------------------------ */

static vx_config g_cfg;
static vx_colors g_colors;
static vx_coloredit g_ed;
static int g_ed_ok, g_setting, g_new_profile;
static char g_id[64], g_title[96], g_cart[1024];
static long g_frame;
static int g_fig[VX_CE_MAXLINES], g_nfig = -1;
static struct { uint16_t addr; uint8_t val; } g_hold[16];   /* hold: bytes kept during runs */
static int g_nhold;
static uint8_t g_snap[0x400];                   /* snap: the RAM remembered */

/* one frame of the game: the holds, the input */
static void step(const vx_pad *a)
{
    vx_pad b = { 0, 0, 0 };
    for (int k = 0; k < g_nhold; k++) vx_poke_ram(g_hold[k].addr, g_hold[k].val);
    vx_set_input(a, &b); vx_run_frame(); vx_colors_frame(&g_colors); g_frame++;
}

static int g_have_snap;

static void load_config(vx_config *c, const char *id)
{
    char p[1100];
    vx_config_defaults(c);
    vx_config_load(c, vp_asset_path("vectrex.ini", p, sizeof p));
    snprintf(p, sizeof p, "%s/vectrex.ini", vp_save_dir());
    vx_config_load(c, p);
    if (id && *id) { snprintf(p, sizeof p, "%s/games/%s.ini", vp_save_dir(), id); vx_config_load(c, p); }
}

static int lines(const vx_line **l) { return vx_lines(l); }

/* canvas (y down) <-> percent of the picture */
static double pct_x(double cx) { return cx * 100.0 / VX_CANVAS_W; }
static double pct_y(double cy) { return cy * 100.0 / VX_CANVAS_H; }
static void to_board(double px, double py, int *bx, int *by)
{
    float x, y;
    vx_map(&g_cfg, px / 100.0 * VX_CANVAS_W, py / 100.0 * VX_CANVAS_H, &x, &y);
    *bx = (int)lrintf(x); *by = (int)lrintf(y);
}

static void figures_cut(void)
{
    const vx_line *l; int n = lines(&l);
    g_nfig = vx_ce_figures(l, n, g_fig);
}

static int editor(void)                         /* the editor on the frame now (after a run: cut again) */
{
    if (g_ed_ok) return 1;
    const vx_line *l; int n = lines(&l);
    int scope = g_ed.open ? g_ed.scope : VX_CE_SMART, color = g_ed.open ? g_ed.color : 0;
    float gain = g_ed.open ? g_ed.gain : 1.0f;
    if (vx_coloredit_open(&g_ed, l, n) != 0) {
        printf("error: this game cannot be coloured (it draws with its own processor: UVMC2 / PiTrex)\n");
        return 0;
    }
    g_ed.scope = scope; g_ed.color = color; g_ed.gain = gain > 0 ? gain : 1.0f;
    g_ed.panel = 1;
    g_ed_ok = 1;
    if (g_nfig < 0) figures_cut();
    return 1;
}

static const char *SCOPES[VX_CE_SCOPES] = { "shape", "line", "object", "group", "code", "routine", "smart", "kind" };

static void rgb_name(uint8_t r, uint8_t g, uint8_t b, char *out, size_t n)   /* "RRGGBB NAME" (nearest) */
{
    int best = 0; long bd = 1L << 30;
    for (int i = 0; i < vx_ce_palette_count(); i++) {
        uint8_t pr, pg, pb; vx_ce_palette(i, &pr, &pg, &pb);
        long d = (long)(pr - r) * (pr - r) + (long)(pg - g) * (pg - g) + (long)(pb - b) * (pb - b);
        if (d < bd) { bd = d; best = i; }
    }
    uint8_t pr, pg, pb;
    const char *nm = vx_ce_palette(best, &pr, &pg, &pb);
    snprintf(out, n, "%02X%02X%02X %s%s", r, g, b, bd ? "~" : "", nm);
    for (char *c = out; *c; c++) if (*c == ' ' && c > out + 6) *c = '_';
}

static int palette_index(const char *s)        /* a name (any case, _ or space) or RRGGBB (nearest) */
{
    char want[48]; size_t k = 0;
    for (; *s && k < sizeof want - 1; s++) want[k++] = (char)(*s == '_' ? ' ' : toupper((unsigned char)*s));
    want[k] = 0;
    for (int i = 0; i < vx_ce_palette_count(); i++) {
        uint8_t r, g, b;
        if (!strcmp(vx_ce_palette(i, &r, &g, &b), want)) return i;
    }
    if (strlen(want) == 6 && strspn(want, "0123456789ABCDEF") == 6) {
        long v = strtol(want, NULL, 16);
        int R = (int)(v >> 16) & 255, G = (int)(v >> 8) & 255, B = (int)v & 255, best = -1; long bd = 1L << 30;
        for (int i = 0; i < vx_ce_palette_count(); i++) {
            uint8_t r, g, b; vx_ce_palette(i, &r, &g, &b);
            long d = (long)(r - R) * (r - R) + (long)(g - G) * (g - G) + (long)(b - B) * (b - B);
            if (d < bd) { bd = d; best = i; }
        }
        return best;
    }
    return -1;
}

/* ------------------------------------------------------------------------------------------------ */
/* the picture as a PNG (no library: stored deflate blocks) */

static uint32_t crc_tab[256];
static uint32_t crc(uint32_t c, const unsigned char *p, size_t n)
{
    if (!crc_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t k = i; for (int j = 0; j < 8; j++) k = k & 1 ? 0xEDB88320u ^ (k >> 1) : k >> 1; crc_tab[i] = k; }
    c ^= 0xffffffffu;
    while (n--) c = crc_tab[(c ^ *p++) & 255] ^ (c >> 8);
    return c ^ 0xffffffffu;
}
static void be32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static void chunk(FILE *f, const char *type, const unsigned char *d, uint32_t n)
{
    unsigned char h[8]; be32(h, n); memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f); if (n) fwrite(d, 1, n, f);
    uint32_t c = crc(crc(0, (const unsigned char *)type, 4), d, n);
    unsigned char t[4]; be32(t, c); fwrite(t, 1, 4, f);
}
static int write_png(const char *path, const unsigned char *rgb, int w, int h)
{
    size_t raw_n = (size_t)h * (size_t)(w * 3 + 1);
    unsigned char *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) { raw[(size_t)y * (w * 3 + 1)] = 0; memcpy(raw + (size_t)y * (w * 3 + 1) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3); }
    size_t blocks = raw_n / 65535 + 1, z_n = 2 + raw_n + blocks * 5 + 4;
    unsigned char *z = malloc(z_n), *q = z;
    *q++ = 0x78; *q++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < raw_n || off == 0;) {
        size_t len = raw_n - off > 65535 ? 65535 : raw_n - off;
        *q++ = off + len >= raw_n ? 1 : 0;
        *q++ = (unsigned char)len; *q++ = (unsigned char)(len >> 8); *q++ = (unsigned char)~len; *q++ = (unsigned char)(~len >> 8);
        memcpy(q, raw + off, len); q += len;
        for (size_t i = 0; i < len; i++) { a = (a + raw[off + i]) % 65521; b = (b + a) % 65521; }
        off += len;
        if (!len) break;
    }
    be32(q, (b << 16) | a); q += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -1; }
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    unsigned char ih[13]; be32(ih, (uint32_t)w); be32(ih + 4, (uint32_t)h); ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
    chunk(f, "IHDR", ih, 13);
    chunk(f, "IDAT", z, (uint32_t)(q - z));
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
    return 0;
}

static unsigned char *g_img; static int g_w, g_h;
static void px(int x, int y, int r, int g, int b)
{
    if (x < 0 || y < 0 || x >= g_w || y >= g_h) return;
    unsigned char *p = g_img + ((size_t)y * g_w + x) * 3;
    if (r > p[0]) p[0] = (unsigned char)r;
    if (g > p[1]) p[1] = (unsigned char)g;
    if (b > p[2]) p[2] = (unsigned char)b;
}
static void line_px(double x0, double y0, double x1, double y1, int r, int g, int b, int thick)
{
    double dx = x1 - x0, dy = y1 - y0, len = sqrt(dx * dx + dy * dy);
    int steps = (int)(len * 2) + 1;
    for (int i = 0; i <= steps; i++) {
        double t = (double)i / steps, x = x0 + dx * t, y = y0 + dy * t;
        for (int oy = -thick / 2; oy <= thick / 2; oy++) for (int ox = -thick / 2; ox <= thick / 2; ox++) px((int)lrint(x) + ox, (int)lrint(y) + oy, r, g, b);
    }
}
static const unsigned char DIGIT[10][5] = { { 7, 5, 5, 5, 7 }, { 2, 6, 2, 2, 7 }, { 7, 1, 7, 4, 7 }, { 7, 1, 7, 1, 7 }, { 5, 5, 7, 1, 1 },
                                            { 7, 4, 7, 1, 7 }, { 7, 4, 7, 5, 7 }, { 7, 1, 1, 1, 1 }, { 7, 5, 7, 5, 7 }, { 7, 5, 7, 1, 7 } };
static void label(int x, int y, int v)          /* a number, 3x5 digits scaled 2, on a dark box */
{
    char s[12]; snprintf(s, sizeof s, "%d", v);
    int w = (int)strlen(s) * 8 + 2;
    for (int yy = y - 1; yy < y + 11; yy++) for (int xx = x - 1; xx < x + w; xx++)
        if (xx >= 0 && yy >= 0 && xx < g_w && yy < g_h) { unsigned char *p = g_img + ((size_t)yy * g_w + xx) * 3; p[0] = 40; p[1] = 40; p[2] = 70; }
    for (int k = 0; s[k]; k++) for (int ry = 0; ry < 5; ry++) for (int rx = 0; rx < 3; rx++)
        if (DIGIT[s[k] - '0'][ry] & (4 >> rx))
            for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++) px(x + k * 8 + rx * 2 + sx, y + ry * 2 + sy, 255, 255, 120);
}

static void shot(const char *path, int labels, int width)
{
    const vx_line *l; int n = lines(&l);
    g_w = width; g_h = (int)lrint((double)width * VX_CANVAS_H / VX_CANVAS_W);
    g_img = calloc((size_t)g_w * g_h * 3, 1);
    vx_config view = g_cfg; view.color_profile = 1;
    double sx = (double)g_w / VX_CANVAS_W, sy = (double)g_h / VX_CANVAS_H;
    int picked = g_ed_ok && (g_ed.nmsel || g_ed.ncand);
    for (int i = 0; i < n; i++) {
        uint8_t r, g, b;
        if (!vx_line_color(&view, &g_colors, &l[i], &r, &g, &b)) continue;
        int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (m && m < 150) { r = (uint8_t)(r * 150 / m); g = (uint8_t)(g * 150 / m); b = (uint8_t)(b * 150 / m); }   /* (dim lines seen) */
        int pk = picked && ((g_ed.nmsel && i < VX_CE_MAXLINES && g_ed.msel[i]) || (!g_ed.nmsel && vx_ce_matches(&g_ed, &l[i])));
        if (pk) line_px(l[i].x0 * sx, l[i].y0 * sy, l[i].x1 * sx, l[i].y1 * sy, 120, 110, 30, 5);   /* (picked: a glow) */
        line_px(l[i].x0 * sx, l[i].y0 * sy, l[i].x1 * sx, l[i].y1 * sy, r, g, b, width >= 1000 ? 2 : 1);
    }
    if (labels) {
        if (g_nfig < 0) figures_cut();
        for (int f = 0; f < g_nfig; f++) {
            double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;  /* (the number in the middle of its box) */
            for (int i = 0; i < n && i < VX_CE_MAXLINES; i++) if (g_fig[i] == f) {
                x0 = fmin(x0, fmin((double)l[i].x0, (double)l[i].x1)); x1 = fmax(x1, fmax((double)l[i].x0, (double)l[i].x1));
                y0 = fmin(y0, fmin((double)l[i].y0, (double)l[i].y1)); y1 = fmax(y1, fmax((double)l[i].y0, (double)l[i].y1));
            }
            int lx = (int)((x0 + x1) / 2 * sx) - 4, ly = (int)((y0 + y1) / 2 * sy) - 5;   /* (kept in the picture) */
            if (lx < 2) lx = 2;
            if (lx > g_w - 30) lx = g_w - 30;
            if (ly < 2) ly = 2;
            if (ly > g_h - 14) ly = g_h - 14;
            label(lx, ly, f);
        }
    }
    if (write_png(path, g_img, g_w, g_h) == 0) printf("shot: %s (%dx%d%s)\n", path, g_w, g_h, labels ? ", figure numbers" : "");
    else printf("error: cannot write %s\n", path);
    free(g_img);
}

/* ------------------------------------------------------------------------------------------------ */

static void list_figures(int only_uncoloured)
{
    const vx_line *l; int n = lines(&l);
    if (g_nfig < 0) figures_cut();
    int shown = 0;
    for (int f = 0; f < g_nfig; f++) {
        double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
        int cnt = 0, free_ = 0, first = -1;
        const vx_rule *rules[4] = { 0 }; int nr = 0, more = 0;
        for (int i = 0; i < n && i < VX_CE_MAXLINES; i++) {
            if (g_fig[i] != f) continue;
            if (first < 0) first = i;
            cnt++;
            double xs[2] = { (double)l[i].x0, (double)l[i].x1 }, ys[2] = { (double)l[i].y0, (double)l[i].y1 };
            for (int k = 0; k < 2; k++) { if (xs[k] < x0) x0 = xs[k]; if (xs[k] > x1) x1 = xs[k]; if (ys[k] < y0) y0 = ys[k]; if (ys[k] > y1) y1 = ys[k]; }
            const vx_rule *u = vx_colors_find(&g_colors, &l[i]);
            if (!u) { free_++; continue; }
            int seen = 0;
            for (int k = 0; k < nr; k++) seen |= rules[k] == u;
            if (!seen) { if (nr < 4) rules[nr++] = u; else more = 1; }
        }
        if (only_uncoloured && !free_) continue;
        shown++;
        printf("figure %d: %d line%s, box %.0f,%.0f-%.0f,%.0f, centre %.0f,%.0f, piece %d", f, cnt, cnt == 1 ? "" : "s",
               pct_x(x0), pct_y(y0), pct_x(x1), pct_y(y1), pct_x((x0 + x1) / 2), pct_y((y0 + y1) / 2), first >= 0 ? (int)l[first].piece : -1);
        if (free_) printf(", %d without a rule", free_);
        for (int k = 0; k < nr; k++) { char c[48]; rgb_name(rules[k]->r, rules[k]->g, rules[k]->b, c, sizeof c); printf("%s %s %s", k ? "," : ", rules:", rules[k]->name, c); }
        if (more) printf(", ...");
        printf("\n");
    }
    if (only_uncoloured && !shown) printf("every line has a rule\n");
}

/* The lines of figure f, or (f < 0) every lit line: where it is and what drew it */
static void list_lines(int f)
{
    const vx_line *l; int n = lines(&l);
    if (g_nfig < 0) figures_cut();
    for (int i = 0; i < n && i < VX_CE_MAXLINES; i++) {
        if (f >= 0 ? g_fig[i] != f : !l[i].z) continue;
        const vx_rule *u = vx_colors_find(&g_colors, &l[i]);
        printf("line %d: %.2f,%.2f -> %.2f,%.2f, brightness %d, piece %u part %u stroke %u, figure %d, pc %04X x %04X u %04X caller %04X, %s\n",
               i, pct_x(l[i].x0), pct_y(l[i].y0), pct_x(l[i].x1), pct_y(l[i].y1), l[i].z, l[i].piece, l[i].part, l[i].stroke, g_fig[i],
               l[i].tag.pc, l[i].tag.x, l[i].tag.u, l[i].tag.caller, u ? u->name : "no rule");
    }
}

static void list_rules(void)
{
    const vx_line *l; int n = lines(&l);
    for (int k = 0; k < g_colors.n; k++) {
        const vx_rule *r = &g_colors.rule[k];
        int hit = 0;
        for (int i = 0; i < n; i++) hit += l[i].z && vx_colors_find(&g_colors, &l[i]) == r;
        char c[48]; rgb_name(r->r, r->g, r->b, c, sizeof c);
        printf("rule %d: %s, %s%s, %d line%s now\n", k, r->name, c, r->gain != 1.0f ? (r->gain > 1 ? " brighter" : " dimmer") : "", hit, hit == 1 ? "" : "s");
    }
    if (!g_colors.n) printf("no rules\n");
}

static void show_picked(void)
{
    if (!editor()) return;
    const vx_line *l; int n = lines(&l);
    int would = 0;
    for (int i = 0; i < n; i++) would += l[i].z && vx_ce_matches(&g_ed, &l[i]);
    printf("picked: what %s, %s, %d rule%s would colour %d line%s:", SCOPES[g_ed.scope],
           g_ed.nmsel ? "lines picked" : "piece picked", g_ed.ncand, g_ed.ncand == 1 ? "" : "s", would, would == 1 ? "" : "s");
    if (g_ed.nmsel) printf(" %d line%s picked", g_ed.nmsel, g_ed.nmsel == 1 ? "" : "s");
    for (int k = 0; k < g_ed.ncand && k < 12; k++) printf("%s%s", k ? ", " : " ", g_ed.cands[k].name);
    printf("\n");
}

static void list_profiles(void)
{
    char p[1100];
    int used = vx_colors_setting_used(g_id, g_setting);
    printf("profiles of %s:", g_id);
    if (vx_colors_profile_path(g_id, 0, p, sizeof p) == 0) printf(" ORIGINAL%s", vx_colors_file_fits(p) ? "" : "(other_version)");
    for (int k = 1; k <= vx_colors_own_count(g_id); k++)
        if (vx_colors_profile_path(g_id, k, p, sizeof p) == 0) printf(" PROFILE_%d%s", k, vx_colors_file_fits(p) ? "" : "(other_version)");
    int nw = 0, t = vx_coloredit_target(g_id, g_setting, g_new_profile, &nw);
    printf("\nshown: %s (%d rules); save writes PROFILE_%d%s\n", used == 0 ? "OFF" : used == 1 ? "ORIGINAL" : "own", g_colors.n, t, nw ? " (new)" : "");
    if (used >= 2) printf("shown is PROFILE_%d\n", used - 1);
}

static void save_profile(void)
{
    char p[1100], d[1100];
    int nw = 0, t = vx_coloredit_target(g_id, g_setting, g_new_profile, &nw);
    snprintf(d, sizeof d, "%s", vp_save_dir()); MKDIR(d);
    snprintf(d, sizeof d, "%s/colors", vp_save_dir()); MKDIR(d);
    vx_colors_profile_path(g_id, t, p, sizeof p);
    if (vx_colors_save(&g_colors, p, g_title) != 0) { printf("error: cannot write %s\n", p); return; }
    /* the game uses it from now on (its own settings, as the Studio does) */
    vx_config all, game;
    load_config(&all, NULL);
    load_config(&game, g_id);
    game.color_profile = t + 1;
    snprintf(d, sizeof d, "%s/games", vp_save_dir()); MKDIR(d);
    snprintf(d, sizeof d, "%s/games/%s.ini", vp_save_dir(), g_id);
    vx_config_save_diff(&game, &all, d);
    g_setting = t + 1; g_new_profile = 0;
    printf("saved: PROFILE_%d%s, %d rules, %s (the game uses it now)\n", t, nw ? " (new)" : "", g_colors.n, p);
}

#ifdef _WIN32
static void send_profile(int use)
{
    char p[1100];
    int used = vx_colors_setting_used(g_id, g_setting);
    if (used < 2 || vx_colors_profile_path(g_id, used - 1, p, sizeof p) != 0) { printf("error: save first (ORIGINAL is on the cabinet already)\n"); return; }
    char host[128] = "192.168.0.122", user[64] = "pi", key[512] = "";
    const char *home = getenv("USERPROFILE");
    if (home) snprintf(key, sizeof key, "%s\\.ssh\\id_ed25519_vectorpie", home);
    char ini[1100]; snprintf(ini, sizeof ini, "%s/studio.ini", vp_package_dir());
    FILE *f = fopen(ini, "r");
    if (f) {
        char ln[700];
        while (fgets(ln, sizeof ln, f)) {
            char *e = strchr(ln, '='); if (!e) continue; *e++ = 0; e[strcspn(e, "\r\n")] = 0;
            if (!strcmp(ln, "host")) snprintf(host, sizeof host, "%s", e);
            else if (!strcmp(ln, "user")) snprintf(user, sizeof user, "%s", e);
            else if (!strcmp(ln, "key")) snprintf(key, sizeof key, "%s", e);
        }
        fclose(f);
    }
    const char *pw = getenv("VX_CABINET_PASSWORD");
    if (pw) {                                   /* (the Studio answers ssh's password question) */
        char ask[1100]; snprintf(ask, sizeof ask, "%s/Vectrex Colour Studio.exe", vp_package_dir());
        _putenv_s("SSH_ASKPASS", ask); _putenv_s("SSH_ASKPASS_REQUIRE", "force");
        _putenv_s("VX_STUDIO_ASKPASS", "1"); _putenv_s("VX_STUDIO_PASS", pw);
    }
    const char *b = strrchr(p, '/'); const char *b2 = strrchr(p, '\\'); if (b2 > b) b = b2;
    const char *file = b ? b + 1 : p;
    char remote[1600];
    snprintf(remote, sizeof remote, "d=/persistent/games/vectrex/save/colors; f=%s; s=old-$(date +%%Y%%m%%d-%%H%%M%%S); mkdir -p $d && cat > $d/.incoming && "
             "if [ -f $d/$f ] && ! cmp -s $d/.incoming $d/$f; then mkdir -p $d/$s && cp -p $d/$f $d/$s/; fi; mv -f $d/.incoming $d/$f", file);
    if (use) {
        size_t k = strlen(remote);
        snprintf(remote + k, sizeof remote - k, "; g=/persistent/games/vectrex/save/games/%s.ini; mkdir -p /persistent/games/vectrex/save/games; "
                 "if grep -q '^color_profile' $g 2>/dev/null; then sed -i 's/^color_profile.*/color_profile = %d/' $g; else echo 'color_profile = %d' >> $g; fi",
                 g_id, used, used);
    }
    char cmd[3000];
    snprintf(cmd, sizeof cmd, "\"%s\\System32\\OpenSSH\\ssh.exe\" -o ConnectTimeout=8 -o StrictHostKeyChecking=accept-new %s%s%s %s@%s \"%s; echo SENT\"",
             getenv("SystemRoot") ? getenv("SystemRoot") : "C:\\Windows", key[0] ? "-i \"" : "", key, key[0] ? "\"" : "", user, host, remote);
    FILE *in = fopen(p, "rb");
    char wrapped[3100];                         /* (cmd.exe /c keeps the inner quotes only so) */
    snprintf(wrapped, sizeof wrapped, "\"%s\"", cmd);
    FILE *pp = _popen(wrapped, "wb");
    if (!pp || !in) { printf("error: cannot run ssh\n"); if (in) fclose(in); return; }
    char buf[8192]; size_t k;
    while ((k = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, k, pp);
    fclose(in);
    int rc = _pclose(pp);
    printf(rc == 0 ? "sent: %s to %s@%s%s\n" : "error: not sent (%s to %s@%s%s): is the cabinet on, user / password / key right?\n",
           file, user, host, use ? ", the game uses it there" : "");
}
#endif

/* ------------------------------------------------------------------------------------------------ */

static int fig_pick(const char *list, int add)  /* "3,7": the lines of these figures into the pick */
{
    if (!editor()) return 0;
    const vx_line *l; int n = lines(&l);
    if (!add) { memset(g_ed.msel, 0, sizeof g_ed.msel); g_ed.nmsel = 0; }
    int first = -1;
    for (const char *q = list; q && *q;) {
        int f = atoi(q);
        for (int i = 0; i < n && i < VX_CE_MAXLINES; i++)
            if (g_fig[i] == f && !g_ed.msel[i]) { g_ed.msel[i] = 1; g_ed.nmsel++; if (first < 0) first = i; }
        q = strchr(q, ','); if (q) q++;
    }
    if (first >= 0) {
        for (int p = 0; p < g_ed.npieces; p++) if (first >= g_ed.piece[p].first && first <= g_ed.piece[p].last) g_ed.sel = p;
        g_ed.line = first;
    }
    vx_ce_refresh(&g_ed, l, n);
    return 1;
}

static int line_pick(const char *list, int add)
{
    if (!editor()) return 0;
    const vx_line *l; int n = lines(&l);
    if (!add) { memset(g_ed.msel, 0, sizeof g_ed.msel); g_ed.nmsel = 0; }
    for (const char *q = list; q && *q;) {
        int i = atoi(q);
        if (i >= 0 && i < n && i < VX_CE_MAXLINES && l[i].z && !g_ed.msel[i]) {
            g_ed.msel[i] = 1; g_ed.nmsel++; g_ed.line = i;
            for (int p = 0; p < g_ed.npieces; p++) if (i >= g_ed.piece[p].first && i <= g_ed.piece[p].last) g_ed.sel = p;
        }
        q = strchr(q, ','); if (q) q++;
    }
    vx_ce_refresh(&g_ed, l, n);
    return 1;
}

/* ---- the commands ------------------------------------------------------------------------------
 * A command's words (a command, a number, a file's path): w[0] the command, nw how many there are.
 * Each cmd_* answers on stdout and returns 0, or 1 after an "error: ..." line. */
typedef char words[8][1024];

static void clear_picked(void)                  /* no lines picked (the rules being made stay) */
{
    memset(g_ed.msel, 0, sizeof g_ed.msel);
    g_ed.nmsel = 0;
}

static int lit_lines(const vx_line *l, int n)
{
    int lit = 0;
    for (int i = 0; i < n; i++) lit += l[i].z != 0;
    return lit;
}

/* "xGAIN" / "*GAIN" (not a word as xrange) */
static int is_gain(const char *s) { return (s[0] == 'x' || s[0] == '*') && (isdigit((unsigned char)s[1]) || s[1] == '.'); }

/* run FRAMES [stick X,Y] [buttons 124] */
static int cmd_run(words w, int nw)
{
    long frames = atol(w[1]);
    vx_pad a = { 0, 0, 0 };
    for (int k = 2; k + 1 < nw; k += 2) {
        if (!strcmp(w[k], "stick")) {
            double x = 0, y = 0;
            sscanf(w[k + 1], "%lf,%lf", &x, &y);
            a.x = (int)lrint(x * 127); a.y = (int)lrint(y * 127);
        } else if (!strcmp(w[k], "buttons")) {
            for (const char *d = w[k + 1]; *d; d++) if (*d >= '1' && *d <= '4') a.buttons |= 1u << (*d - '1');
        }
    }
    for (long f = 0; f < frames; f++) step(&a);
    g_ed_ok = 0; g_nfig = -1;
    const vx_line *l; int n = lines(&l);
    printf("frame %ld: %d lines lit\n", g_frame, lit_lines(l, n));
    return 0;
}

/* The console's RAM: ram, poke, hold, snap, changed, find. -1: not one of these commands. */
static int cmd_memory(words w, int nw)
{
    if (!strcmp(w[0], "ram")) {
        unsigned a0 = (unsigned)strtoul(w[1], NULL, 16), k = nw > 2 ? (unsigned)strtoul(w[2], NULL, 10) : 16;
        if (k > 256) k = 256;
        for (unsigned i = 0; i < k; i++) {
            if (i % 16 == 0) printf("%s%04X:", i ? "\n" : "", a0 + i);
            printf(" %02X", vx_peek_ram((uint16_t)(a0 + i)));
        }
        printf("\n");
        return 0;
    }
    if (!strcmp(w[0], "poke")) {
        unsigned a0 = (unsigned)strtoul(w[1], NULL, 16), v = (unsigned)strtoul(w[2], NULL, 16);
        vx_poke_ram((uint16_t)a0, (uint8_t)v);
        printf("poke %04X = %02X\n", a0, v & 255);
        return 0;
    }
    if (!strcmp(w[0], "hold")) {
        if (!strcmp(w[1], "off")) { g_nhold = 0; printf("hold: none\n"); return 0; }
        if (g_nhold >= 16) { printf("error: 16 holds at most\n"); return 1; }
        g_hold[g_nhold].addr = (uint16_t)strtoul(w[1], NULL, 16);
        g_hold[g_nhold].val = (uint8_t)strtoul(w[2], NULL, 16);
        printf("hold %04X = %02X during runs\n", g_hold[g_nhold].addr, g_hold[g_nhold].val);
        g_nhold++;
        return 0;
    }
    if (!strcmp(w[0], "snap")) {
        for (int i = 0; i < 0x400; i++) g_snap[i] = vx_peek_ram((uint16_t)(0xC800 + i));
        g_have_snap = 1;
        printf("snap: the RAM at frame %ld\n", g_frame);
        return 0;
    }
    if (!strcmp(w[0], "changed")) {
        if (!g_have_snap) { printf("error: snap first\n"); return 1; }
        int k = 0;
        for (int i = 0; i < 0x400; i++) {
            uint8_t v = vx_peek_ram((uint16_t)(0xC800 + i));
            if (v != g_snap[i]) { printf("%s%04X %02X->%02X", k % 8 ? "  " : (k ? "\n" : ""), 0xC800 + i, g_snap[i], v); k++; }
        }
        printf("%s%d bytes changed since the snap\n", k ? "\n" : "", k);
        return 0;
    }
    if (!strcmp(w[0], "find")) {
        unsigned v = (unsigned)strtoul(w[1], NULL, 16);
        int k = 0;
        for (int i = 0; i < 0x400; i++)
            if (vx_peek_ram((uint16_t)(0xC800 + i)) == v) printf("%s%04X", k++ % 16 ? " " : (k > 1 ? "\n" : ""), 0xC800 + i);
        printf("%s%d addresses hold %02X\n", k ? "\n" : "", k, v & 255);
        return 0;
    }
    return -1;
}

/* pick list ADDR: every piece drawn from the vector list at ADDR (hex) */
static int pick_list(const char *arg, int add, const vx_line *l, int n)
{
    unsigned A = (unsigned)strtoul(arg, NULL, 16);
    if (!add) clear_picked();
    int first = -1;
    for (int p = 0; p < g_ed.npieces; p++) {
        /* the list starting there (a shape rule's name: the list's start, the pieces' X 3 bytes on, one vector
           further); not pieces whose X is no list (a game's own drawing: wide, odd ranges) */
        if (g_ed.piece[p].x_lo < A || g_ed.piece[p].x_lo > A + 3 || g_ed.piece[p].x_hi - g_ed.piece[p].x_lo > 0x200) continue;
        for (int i = g_ed.piece[p].first; i <= g_ed.piece[p].last && i < VX_CE_MAXLINES; i++)
            if (l[i].z && !g_ed.msel[i]) { g_ed.msel[i] = 1; g_ed.nmsel++; if (first < 0) { first = i; g_ed.sel = p; } }
    }
    if (first < 0) {                            /* (nothing picked then: a colour after it colours nothing) */
        if (!add) g_ed.ncand = 0;
        printf("error: no piece is drawn from a list at %04X in this frame\n", A);
        return 1;
    }
    g_ed.line = first;
    vx_ce_refresh(&g_ed, l, n);
    return 0;
}

/* pick at X Y: the line nearest the point (within 4 % of the picture), then picked as a click on it */
static int pick_at(const char *sx, const char *sy, int add, const vx_line *l, int n)
{
    double px = atof(sx) / 100.0 * VX_CANVAS_W, py = atof(sy) / 100.0 * VX_CANVAS_H, bd = 0.04 * VX_CANVAS_W;
    int best = -1;
    bd *= bd;
    for (int i = 0; i < n; i++) {
        if (!l[i].z) continue;
        double dx = (double)(l[i].x1 - l[i].x0), dy = (double)(l[i].y1 - l[i].y0), len = dx * dx + dy * dy;
        double t = len > 0 ? ((px - l[i].x0) * dx + (py - l[i].y0) * dy) / len : 0.0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        double ex = l[i].x0 + t * dx - px, ey = l[i].y0 + t * dy - py, d = ex * ex + ey * ey;
        if (d < bd) { bd = d; best = i; }
    }
    if (!add || best < 0) {
        if (!add) { clear_picked(); g_ed.ncand = 0; }
        if (best < 0) {
            printf("error: no line within 4%% of %s,%s (look at a shot, or use pick figure)\n", sx, sy);
            return 1;
        }
    }
    printf("nearest line %d (figure %d), %.1f%% away\n", best, best < VX_CE_MAXLINES ? g_fig[best] : -1, sqrt(bd) * 100.0 / VX_CANVAS_W);
    float x0, y0, x1, y1;
    vx_map(&g_cfg, (double)l[best].x0, (double)l[best].y0, &x0, &y0);
    vx_map(&g_cfg, (double)l[best].x1, (double)l[best].y1, &x1, &y1);
    int bx = (int)lrintf((x0 + x1) / 2), by = (int)lrintf((y0 + y1) / 2);
    vx_ce_mouse_pick(&g_ed, &g_colors, &g_cfg, l, n, bx, by, bx, by, 0, add && (g_ed.nmsel || g_ed.ncand));
    g_ed.panel = 1;
    return 0;
}

/* pick / add figure N | line I | list ADDR | piece N | at X Y | box X0 Y0 X1 Y1 | none */
static int cmd_pick(words w, const vx_line *l, int n)
{
    int add = !strcmp(w[0], "add");
    if (!editor()) return 1;
    if (!strcmp(w[1], "none")) {                /* nothing picked (a shot without the picked lines' glow) */
        clear_picked(); g_ed.ncand = 0;
        printf("picked: nothing\n");
        return 0;
    }
    if (!strcmp(w[1], "figure")) fig_pick(w[2], add);
    else if (!strcmp(w[1], "line")) line_pick(w[2], add);
    else if (!strcmp(w[1], "list")) { if (pick_list(w[2], add, l, n) != 0) return 1; }
    else if (!strcmp(w[1], "piece") && !add) {
        int p = atoi(w[2]);
        if (p < 0 || p >= g_ed.npieces) { printf("error: no piece %d\n", p); return 1; }
        clear_picked();
        g_ed.sel = p; g_ed.line = g_ed.piece[p].first;
        vx_ce_refresh(&g_ed, l, n);
    }
    else if (!strcmp(w[1], "at")) { if (pick_at(w[2], w[3], add, l, n) != 0) return 1; }
    else if (!strcmp(w[1], "box")) {
        int ax, ay, bx, by;
        to_board(atof(w[2]), atof(w[3]), &ax, &ay); to_board(atof(w[4]), atof(w[5]), &bx, &by);
        vx_ce_mouse_pick(&g_ed, &g_colors, &g_cfg, l, n, ax, ay, bx, by, 1, add);
        g_ed.panel = 1;
    }
    else { printf("error: pick figure N | line I | piece N | at X Y | box X0 Y0 X1 Y1\n"); return 1; }
    show_picked();
    return 0;
}

/* ---- colour's options: more conditions on the rules being made (g_ed.cands) ---- */

/* One condition more on every rule, its name ending in `suffix` (a name of its own per state / range).
 * 0, or 1 (a rule has no room). */
static int cands_add_cond(const vx_cond *c, const char *suffix)
{
    for (int k = 0; k < g_ed.ncand; k++) {
        vx_rule *r = &g_ed.cands[k];
        if (r->ncond >= VX_RULE_CONDS) { printf("error: rule %s has no room for a condition\n", r->name); return 1; }
        r->cond[r->ncond++] = *c;
        size_t len = strlen(r->name);
        snprintf(r->name + len, sizeof r->name - len, "%s", suffix);
    }
    return 0;
}

/* Every rule's range of `kind` (VX_C_X the vector list, VX_C_U the object) set to "LO-HI" (hex): the
 * condition it has, else one more. */
static void cands_set_range(int kind, const char *arg, const char *suffix_fmt)
{
    unsigned lo = (unsigned)strtoul(arg, NULL, 16), hi = lo;
    const char *dash = strchr(arg, '-');
    if (dash) hi = (unsigned)strtoul(dash + 1, NULL, 16);
    for (int c = 0; c < g_ed.ncand; c++) {
        vx_rule *r = &g_ed.cands[c];
        int have = 0;
        for (int q = 0; q < r->ncond; q++)
            if (r->cond[q].kind == kind) { r->cond[q].lo = (uint16_t)lo; r->cond[q].hi = (uint16_t)hi; have = 1; }
        if (!have && r->ncond < VX_RULE_CONDS) {
            vx_cond *q = &r->cond[r->ncond++];
            memset(q, 0, sizeof *q);
            q->kind = kind; q->lo = (uint16_t)lo; q->hi = (uint16_t)hi;
        }
        if (suffix_fmt) {
            size_t len = strlen(r->name);
            snprintf(r->name + len, sizeof r->name - len, suffix_fmt, lo);
        }
    }
}

enum { OPT_WHEN, OPT_XRANGE, OPT_URANGE, OPT_LENGTH, OPT_AREA, OPT_COUNT };
static const char *const OPT_NAME[OPT_COUNT] = { "when", "xrange", "urange", "length", "area" };

/* One option of colour with its argument(s). 0, or 1 after an error. */
static int colour_option(int opt, const char *a, const char *b)
{
    vx_cond c;
    char suffix[48];
    memset(&c, 0, sizeof c);
    switch (opt) {
    case OPT_WHEN: {                            /* "when ADDR[&MASK]=VAL" / "!=VAL": only in that game state */
        const char *amp = strchr(a, '&'), *eq = strchr(a, '=');
        unsigned addr = (unsigned)strtoul(a, NULL, 16), mask = 0xff;
        if (amp && (!eq || amp < eq)) mask = (unsigned)strtoul(amp + 1, NULL, 16);
        if (!eq || addr < 0xC800 || addr > 0xCBFF) { printf("error: when ADDR=VAL (ADDR in C800-CBFF, hex)\n"); return 1; }
        unsigned val = (unsigned)strtoul(eq + 1, NULL, 16);
        c.kind = VX_C_RAM; c.addr = (uint16_t)addr; c.mask = (uint8_t)mask; c.val = (uint8_t)val; c.neg = strstr(a, "!=") != NULL;
        snprintf(suffix, sizeof suffix, "_%X", val & 255);
        return cands_add_cond(&c, suffix);
    }
    case OPT_XRANGE:                            /* "xrange LO-HI": e.g. a game's gauge drawn with a changing X */
        cands_set_range(VX_C_X, a, NULL);
        return 0;
    case OPT_URANGE:                            /* "urange LO-HI": the rules' object widened to the range */
        cands_set_range(VX_C_U, a, "_u%04X");
        return 0;
    case OPT_LENGTH: {                          /* "length LO[-HI]": only lines of that length (% of the height) */
        double lo = atof(a), hi = 1000;
        const char *dash = strchr(a, '-');
        if (dash) hi = atof(dash + 1);
        long clo = (long)(lo * VX_CANVAS_H / 100), chi = (long)(hi * VX_CANVAS_H / 100);
        if (chi > 65535) chi = 65535;
        c.kind = VX_C_LENGTH; c.lo = (uint16_t)clo; c.hi = (uint16_t)chi;
        snprintf(suffix, sizeof suffix, "_len%d", (int)lo);
        return cands_add_cond(&c, suffix);
    }
    default: {                                  /* "area X0-X1 Y0-Y1": only lines inside the box (% of the picture) */
        const char *dx = strchr(a, '-'), *dy = strchr(b, '-');
        if (!dx || !dy) { printf("error: area X0-X1 Y0-Y1 (%% of the picture)\n"); return 1; }
        c.kind = VX_C_AREA;
        c.lo = (uint16_t)(long)(atof(a) * VX_CANVAS_W / 100); c.hi = (uint16_t)(long)(atof(dx + 1) * VX_CANVAS_W / 100);
        c.lo2 = (uint16_t)(long)(atof(b) * VX_CANVAS_H / 100); c.hi2 = (uint16_t)(long)(atof(dy + 1) * VX_CANVAS_H / 100);
        snprintf(suffix, sizeof suffix, "_at%d_%d", (int)atof(a), (int)atof(b));
        return cands_add_cond(&c, suffix);
    }
    }
}

/* colour NAME [xGAIN] [when ADDR=VAL]... [xrange LO-HI] [urange LO-HI] [length LO-HI] [area X0-X1 Y0-Y1] */
static int cmd_colour(words w, int nw, const vx_line *l, int n)
{
    if (!editor()) return 1;
    if (!g_ed.nmsel && !g_ed.ncand) { printf("error: pick something first\n"); return 1; }
    int idx = palette_index(w[1]);
    if (idx < 0) { printf("error: no colour %s (see palette)\n", w[1]); return 1; }
    float gain = nw > 2 && is_gain(w[2]) ? (float)atof(w[2] + 1) : 1.0f;
    g_ed.color = idx; g_ed.gain = gain;
    vx_ce_refresh(&g_ed, l, n);
    /* (the options in this order whatever theirs is: the rules' conditions and names stay the same) */
    const char *when = NULL;
    for (int opt = 0; opt < OPT_COUNT; opt++)
        for (int k = 2; k + 1 < nw; k++) {
            if (strcmp(w[k], OPT_NAME[opt])) continue;
            if (opt == OPT_AREA && k + 2 >= nw) continue;
            if (opt == OPT_WHEN && !when) when = w[k + 1];
            if (colour_option(opt, w[k + 1], opt == OPT_AREA ? w[k + 2] : "") != 0) return 1;
        }
    if (when) g_ed.cand = g_ed.cands[0];
    if (vx_ce_take(&g_ed, &g_colors) != 0) { printf("error: the profile is full (%d rules)\n", VX_COLOR_RULES); return 1; }
    uint8_t r, g, b;
    printf("coloured: %s x%.1f, %d rule%s%s%s\n", vx_ce_palette(idx, &r, &g, &b), gain, g_ed.ncand, g_ed.ncand == 1 ? "" : "s",
           when ? " when " : "", when ? when : "");
    return 0;
}

/* recolour rule N NAME [xGAIN]: only its colour */
static int cmd_recolour(words w, int nw)
{
    int k = atoi(w[2]);
    if (strcmp(w[1], "rule") || k < 0 || k >= g_colors.n) { printf("error: recolour rule N NAME [xGAIN] (see rules)\n"); return 1; }
    int idx = palette_index(w[3]);
    if (idx < 0) { printf("error: no colour %s (see palette)\n", w[3]); return 1; }
    uint8_t r, g, b;
    const char *nm = vx_ce_palette(idx, &r, &g, &b);
    vx_rule *u = &g_colors.rule[k];
    u->r = r; u->g = g; u->b = b;
    if (nw > 4 && is_gain(w[4])) {
        float gn = (float)atof(w[4] + 1);
        u->gain = gn < 0.5f ? 0.5f : gn > 3.0f ? 3.0f : gn;
    }
    printf("recoloured rule %d (%s): %s x%.1f\n", k, u->name, nm, u->gain);
    return 0;
}

/* One command (a line of a script, or a part between ; of the command line). */
static int command(char *c)
{
    while (isspace((unsigned char)*c)) c++;
    char *e = c + strlen(c);
    while (e > c && isspace((unsigned char)e[-1])) *--e = 0;
    if (!*c || *c == '#') return 0;
    printf("> %s\n", c);
    static words w;
    memset(w, 0, sizeof w);
    int nw = sscanf(c, "%1023s %1023s %1023s %1023s %1023s %1023s %1023s %1023s", w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7]);
    const vx_line *l; int n = lines(&l);
    const char *cmd = w[0];

    /* the game */
    if (!strcmp(cmd, "help")) { fputs(HELP, stdout); return 0; }
    if (!strcmp(cmd, "run")) return cmd_run(w, nw);
    int mem = cmd_memory(w, nw);
    if (mem >= 0) return mem;

    /* the picture */
    if (!strcmp(cmd, "frame")) {
        if (g_nfig < 0) figures_cut();
        printf("frame %ld: %d lines lit, %d figures, game %s (%s)\n", g_frame, lit_lines(l, n), g_nfig, g_id, g_title);
        return 0;
    }
    if (!strcmp(cmd, "shot")) {
        int labels = 0, width = 700;
        for (int k = 2; k < nw; k++) {
            if (!strcmp(w[k], "labels")) labels = 1;
            if (!strcmp(w[k], "width") && k + 1 < nw) width = atoi(w[++k]);
        }
        if (width < 200) width = 200;
        if (width > 3000) width = 3000;
        shot(w[1], labels, width);
        return 0;
    }
    if (!strcmp(cmd, "figures")) { list_figures(0); return 0; }
    if (!strcmp(cmd, "uncoloured") || !strcmp(cmd, "uncolored")) { list_figures(1); return 0; }
    if (!strcmp(cmd, "lines")) { list_lines(!strcmp(w[1], "all") ? -1 : atoi(w[1])); return 0; }
    if (!strcmp(cmd, "palette")) {
        for (int i = 0; i < vx_ce_palette_count(); i++) {
            uint8_t r, g, b;
            const char *nm = vx_ce_palette(i, &r, &g, &b);
            printf("%-14s %02X%02X%02X\n", nm, r, g, b);
        }
        return 0;
    }
    if (!strcmp(cmd, "pieces")) {
        if (!editor()) return 1;
        for (int p = 0; p < g_ed.npieces; p++) {
            const vx_piece *s = &g_ed.piece[p];
            printf("piece %d: lines %d-%d, centre %.0f,%.0f, list %04X-%04X, code %04X, object %04X, %d part%s%s\n", p, s->first, s->last,
                   pct_x(s->cx), pct_y(s->cy), s->x_lo, s->x_hi, s->caller, s->u, s->parts, s->parts == 1 ? "" : "s", s->dots ? ", dots" : "");
        }
        return 0;
    }

    /* picking and colouring */
    if (!strcmp(cmd, "what")) {
        if (!editor()) return 1;
        for (int s = 0; s < VX_CE_SCOPES; s++) {
            if (strcmp(w[1], SCOPES[s])) continue;
            g_ed.scope = s;
            if (g_ed.nmsel || g_ed.ncand) vx_ce_refresh(&g_ed, l, n);
            printf("what: %s\n", SCOPES[s]);
            return 0;
        }
        printf("error: what smart|shape|line|object|group|code|routine\n");
        return 1;
    }
    if (!strcmp(cmd, "pick") || !strcmp(cmd, "add")) return cmd_pick(w, l, n);
    if (!strcmp(cmd, "picked")) { show_picked(); return 0; }
    if (!strcmp(cmd, "colour") || !strcmp(cmd, "color")) return cmd_colour(w, nw, l, n);
    if (!strcmp(cmd, "uncolour") || !strcmp(cmd, "uncolor")) {
        if (!editor()) return 1;
        printf("removed: %d rule(s)\n", vx_ce_remove(&g_ed, &g_colors, l, n));
        return 0;
    }

    /* the profile's rules, the profiles */
    if (!strcmp(cmd, "rules")) { list_rules(); return 0; }
    if (!strcmp(cmd, "recolour") || !strcmp(cmd, "recolor")) return cmd_recolour(w, nw);
    if (!strcmp(cmd, "delete") && !strcmp(w[1], "rule")) {
        int k = atoi(w[2]);
        if (k < 0 || k >= g_colors.n) { printf("error: no rule %d\n", k); return 1; }
        memmove(&g_colors.rule[k], &g_colors.rule[k + 1], sizeof(vx_rule) * (size_t)(g_colors.n - k - 1));
        g_colors.n--;
        printf("deleted rule %d\n", k);
        return 0;
    }
    if (!strcmp(cmd, "profiles")) { list_profiles(); return 0; }
    if (!strcmp(cmd, "profile")) {
        int s = !strcmp(w[1], "off") ? 0 : !strcmp(w[1], "original") ? 1 : !strcmp(w[1], "new") ? -1 : atoi(w[1]) + 1;
        if (s < 0) { g_new_profile = 1; printf("save writes a new profile\n"); return 0; }
        g_setting = s; g_new_profile = 0;
        vx_colors_load_setting(&g_colors, g_id, s);
        g_ed_ok = 0;
        list_profiles();
        return 0;
    }
    if (!strcmp(cmd, "save")) { save_profile(); return 0; }
    if (!strcmp(cmd, "send")) {
#ifdef _WIN32
        send_profile(nw > 1 && !strcmp(w[1], "use"));
#else
        printf("send: on the cabinet itself the profile is in place already (save)\n");
#endif
        return 0;
    }
    printf("error: unknown command %s (vxcolor help)\n", cmd);
    return 1;
}

static void commands(const char *arg)
{
    if (arg[0] == '@') {
        FILE *f = fopen(arg + 1, "r");
        if (!f) { printf("error: cannot read %s\n", arg + 1); return; }
        char ln[1024];
        while (fgets(ln, sizeof ln, f)) command(ln);
        fclose(f);
        return;
    }
    char *s = strdup(arg), *q = s;
    while (q) {
        char *semi = strchr(q, ';');
        if (semi) *semi = 0;
        command(q);
        q = semi ? semi + 1 : NULL;
    }
    free(s);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2 || !strcmp(argv[1], "help") || !strcmp(argv[1], "--help")) { fputs(HELP, stdout); return argc < 2; }
    vx_config all;
    load_config(&all, NULL);
    if (!strcmp(argv[1], "games")) {
        vx_rom_list roms;
        memset(&roms, 0, sizeof roms);
        vx_roms_scan(&roms, all.rom_dir);
        for (int i = 0; i < roms.n; i++) {
            char p[1100];
            int own = vx_colors_own_count(roms.rom[i].id), orig = vx_colors_profile_path(roms.rom[i].id, 0, p, sizeof p) == 0;
            printf("%-20s %-28s %s%s%s\n", roms.rom[i].id, roms.rom[i].name, roms.rom[i].path, orig ? "  [ORIGINAL]" : "",
                   own ? "  [own profiles]" : "");
        }
        if (!roms.n) printf("no games: put cartridges in %s/roms\n", vp_package_dir());
        vx_roms_free(&roms);
        return 0;
    }
    if (vx_rom_resolve(argv[1], all.rom_dir, g_cart, sizeof g_cart) != 0) { printf("error: no game %s (vxcolor games)\n", argv[1]); return 1; }
    vx_game_id(g_cart, g_id, sizeof g_id);
    vx_colors_game(g_cart);                     /* (its checksum: its profiles found by it, written into those saved) */
    {   /* its title: the name in the game list */
        vx_rom_list roms; memset(&roms, 0, sizeof roms);
        vx_roms_scan(&roms, all.rom_dir);
        snprintf(g_title, sizeof g_title, "%s", g_id);
        for (int i = 0; i < roms.n; i++) if (!strcmp(roms.rom[i].id, g_id)) snprintf(g_title, sizeof g_title, "%s", roms.rom[i].name);
        vx_roms_free(&roms);
    }
    load_config(&g_cfg, g_id);
    char bios[1100], err[256];
    vp_asset_path("system/bios.bin", bios, sizeof bios);
    if (vx_open(bios, g_cart, err, sizeof err) != 0) { printf("error: %s\n", err); return 1; }
    {   /* a flash cartridge (Vectorblade) reads its <rom>.sav, but a test run never writes it back: high scores
         * and settings stay the player's */
        extern const char *vt_flash_file;
        vt_flash_file = NULL;
    }
    vx_set_mains_hum(0);
    vx_power_on(g_cfg.skip_bios);
    g_setting = g_cfg.color_profile;
    vx_colors_load_setting(&g_colors, g_id, g_setting);
    printf("game %s (%s), %s, checksum %08X; profile: %s (%d rules)\n", g_id, g_title, g_cart, (unsigned)vx_colors_game_crc(),
           g_colors.n ? g_colors.file : "none", g_colors.n);
    if (g_colors.n && !vx_colors_file_fits(g_colors.file))
        printf("note: the profile is made for another version of the game (other checksum): its rules may miss\n");
    if (vx_cart() != VX_CART_ROM)
        printf("note: a UVMC2 / PiTrex game: its picture does not come from the 6809, it cannot be coloured "
               "(figures and shot still work)\n");
    for (int i = 2; i < argc; i++) commands(argv[i]);
    vx_close();
    return 0;
}
