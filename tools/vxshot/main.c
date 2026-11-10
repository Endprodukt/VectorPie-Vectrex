/* vxshot - a game without a screen, its picture as the board would get it, colour profile included:
 *
 *   vxshot BIOS CART FRAMES OUT.svg [--skip-bios] [--id GAME] [--profile FILE] [--port2 NAME]
 *          [--input "F0-F1:MASK[:X;Y],..."] [--spin "F0-F1:COUNTS,..."] [--peek ADDR] [--by-brightness] [--no-profile]
 *          [--pieces] [--edit "PIECE:SCOPE:COLOUR[*GAIN],..."] [--save PROFILE.ini]
 *
 * CART "" for Mine Storm. The colour profile is the game's (colors/<id>.ini next to the program, the id
 * from the cartridge's file name or --id), or --profile. --input holds player 1's buttons (bit 0..3 =
 * 1..4) and stick; --spin turns the cabinet's spinner by COUNTS a frame (port 2 a spinner or trackball,
 * through platform/port2.c as in the program). The settings are the defaults (platform/config.c).
 * --pieces lists the last frame's pieces as the colour editor sees them (platform/coloredit.h); --edit
 * does what a player does in the editor: a piece (its number; PIECE.N its N-th stroke, for WHAT line), WHAT
 * (shape, line, object, group, code, routine),
 * a palette colour (spaces as _) and a gain; --save writes the profile so made. */
#include "core/vx_core.h"
#include "platform/coloredit.h"
#include "platform/config.h"
#include "platform/port2.h"
#include "render/colors.h"
#include "render/draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char (*e6809_read8)(unsigned address);

static int has_flag(int argc, char **argv, const char *k)
{
    for (int i = 5; i < argc; i++)
        if (!strcmp(argv[i], k)) return 1;
    return 0;
}

static const char *opt(int argc, char **argv, const char *k)
{
    for (int i = 5; i + 1 < argc; i++)
        if (!strcmp(argv[i], k)) return argv[i + 1];
    return NULL;
}

/* "F0-F1:A[:X;Y]" items: A (and X, Y) of the item frame f is in, summed; 1 when one is */
static int script(const char *s, long f, unsigned *a, int *x, int *y)
{
    int any = 0;
    for (const char *q = s ? s : ""; *q;) {
        long f0 = 0, f1 = 0; int v = 0, xx = 0, yy = 0, k = 0;
        if (sscanf(q, "%ld-%ld:%d:%d;%d%n", &f0, &f1, &v, &xx, &yy, &k) < 5) {
            xx = yy = 0; k = 0;
            sscanf(q, "%ld-%ld:%d%n", &f0, &f1, &v, &k);
        }
        if (k > 0 && f >= f0 && f <= f1) { *a |= (unsigned)v; if (x) *x = xx; if (y) *y = yy; any = 1; }
        q += k > 0 ? k : 1;
        while (*q == ',' || *q == ' ') q++;
    }
    return any;
}

int main(int argc, char **argv)
{
    if (argc < 5) {
        fprintf(stderr, "usage: vxshot BIOS CART FRAMES OUT.svg [--skip-bios] [--id GAME] [--profile FILE]\n"
                        "              [--port2 NAME] [--input ...] [--spin ...] [--peek ADDR N]\n");
        return 2;
    }
    vx_config cfg;
    vx_config_defaults(&cfg);
    int skip = 0;
    for (int i = 5; i < argc; i++) {
        if (!strcmp(argv[i], "--skip-bios")) skip = 1;
        if (!strcmp(argv[i], "--by-brightness")) cfg.color_mode = VX_COLOR_BRIGHTNESS;   /* (the default bands) */
        if (!strcmp(argv[i], "--no-profile")) cfg.color_profile = 0;
    }
    const char *cart = argv[2][0] ? argv[2] : NULL;
    char id[64];
    vx_game_id(opt(argc, argv, "--id") ? opt(argc, argv, "--id") : cart, id, sizeof id);
    vx_colors_game(cart);                       /* (its checksum, into the profiles it saves) */
    if (opt(argc, argv, "--port2")) {
        for (int d = 0; d < VX_P2_COUNT; d++)
            if (!strcmp(opt(argc, argv, "--port2"), vx_port2_name(d))) cfg.port2 = d;
    }

    char err[256];
    if (vx_open(argv[1], cart, err, sizeof err)) { fprintf(stderr, "vxshot: %s\n", err); return 1; }
    vx_set_mains_hum(cfg.mains_hum);
    static vx_colors colors;
    int have = opt(argc, argv, "--profile") ? vx_colors_load_file(&colors, opt(argc, argv, "--profile"))
                                             : vx_colors_load(&colors, id);
    printf("game %s, colour profile: %s (%d rules)\n", id, have == 0 ? colors.file : "none", colors.n);
    if (cfg.port2 == VX_P2_ATARIVOX || cfg.port2 == VX_P2_VECVOX || cfg.port2 == VX_P2_VECVOICE) {
        char rom[1024];                     /* (the speech ROM next to the BIOS) */
        snprintf(rom, sizeof rom, "%s", argv[1]);
        char *sl = strrchr(rom, '/');
        if (!sl) sl = strrchr(rom, '\\');
        snprintf(sl ? sl + 1 : rom, sizeof rom - (size_t)(sl ? sl + 1 - rom : 0), "sp0256-al2.bin");
        printf("speech ROM %s: %s\n", rom, vx_load_speech_rom(rom) == 0 ? "loaded" : "NOT LOADED");
        extern int vt_speech_sep;           /* (the core: the speech on a channel of its own, measured below) */
        vt_speech_sep = 1;
    }
    static vx_port2_dev p2;
    vx_port2_setup(&p2, &cfg);
    vx_power_on(skip);
    double speech = 0.0, console = 0.0;
    long speech_frames = 0;

    long frames = atol(argv[3]);
    for (long f = 0; f < frames; f++) {
        vx_pad a = { 0, 0, 0 }, b = { 0, 0, 0 };
        unsigned m = 0;
        script(opt(argc, argv, "--input"), f, &m, &a.x, &a.y);
        a.buttons = m;
        unsigned counts = 0;
        if (script(opt(argc, argv, "--spin"), f, &counts, NULL, NULL)) {
            int c = (int)counts;
            if (c > 0x7fff) c -= 0x10000;
            p2.acc_x += c * (p2.kind == VX_P2_SPINNER ? cfg.spin_sens : cfg.tb_sens);
        }
        vx_set_input(&a, &b);
        vx_run_frame();
        int nf, ch;                         /* the sound: two channels, the second the speech module's */
        const int16_t *s = vx_audio(&nf, &ch);
        double e = 0.0;
        for (int i = 0; i < nf; i++) {
            console += s[i * ch] < 0 ? -s[i * ch] : s[i * ch];
            if (ch == 2) e += s[i * 2 + 1] < 0 ? -s[i * 2 + 1] : s[i * 2 + 1];
        }
        speech += e;
        if (nf && e / nf > 50.0) speech_frames++;
    }
    const vx_line *l;
    int n = vx_lines(&l);
    vx_colors_frame(&colors);

    /* the colour editor, scripted: what a player does with the stick and the panel, on the last frame */
    static vx_piece pieces[VX_CE_PIECES];
    int np = vx_ce_pieces(l, n, pieces, VX_CE_PIECES);
    if (has_flag(argc, argv, "--pieces"))
        for (int k = 0; k < np; k++)
            printf("piece %3d: lines %d-%d, routine %04X-%04X, list %04X-%04X, object %04X, code %04X, parts %d, at %.0f%% %.0f%%%s\n", k,
                   pieces[k].first, pieces[k].last, pieces[k].pc_lo, pieces[k].pc_hi, pieces[k].x_lo, pieces[k].x_hi, pieces[k].u,
                   pieces[k].caller, pieces[k].parts, pieces[k].cx * 100.0 / VX_CANVAS_W, pieces[k].cy * 100.0 / VX_CANVAS_H,
                   pieces[k].dots ? ", dots" : "");
    for (const char *q = opt(argc, argv, "--edit"); q && *q;) {     /* "PIECE[.LINE]:SCOPE:COLOUR[*GAIN]," */
        int k = -1, len = 0, sub = 0, sublen = 0;
        char scope[16] = "", colour[24] = "";
        if (sscanf(q, "%d.%d%n", &k, &sub, &sublen) == 2) {   /* PIECE.LINE: the piece's LINE-th lit line (from 1) */
            char rest[64];
            if (sscanf(q + sublen, ":%15[a-z]:%23[A-Z_*0-9.]%n", scope, colour, &len) < 2) len = 0;
            else len += sublen;
            (void)rest;
        } else if (sscanf(q, "%d:%15[a-z]:%23[A-Z_*0-9.]%n", &k, scope, colour, &len) < 3) len = 0;
        if (len == 0 || k < 0 || k >= np) {
            fprintf(stderr, "vxshot: --edit item not understood at \"%s\"\n", q);
            break;
        }
        float gain = 1.0f;
        char *star = strchr(colour, '*');
        if (star) { gain = (float)atof(star + 1); *star = '\0'; }
        for (char *c = colour; *c; c++) if (*c == '_') *c = ' ';
        int sc = !strcmp(scope, "object") ? VX_CE_OBJECT : !strcmp(scope, "group") ? VX_CE_GROUP
               : !strcmp(scope, "routine") ? VX_CE_ROUTINE : !strcmp(scope, "line") ? VX_CE_LINE
               : !strcmp(scope, "code") ? VX_CE_CODE : VX_CE_SHAPE;
        int pi = -1;
        for (int i = 0; i < vx_ce_palette_count(); i++) {
            uint8_t r, g, bb;
            if (!strcmp(vx_ce_palette(i, &r, &g, &bb), colour)) pi = i;
        }
        uint8_t r = 255, g = 255, bb = 255;
        if (pi < 0) fprintf(stderr, "vxshot: no colour %s in the palette\n", colour);
        else vx_ce_palette(pi, &r, &g, &bb);
        vx_rule rule;
        if (sc == VX_CE_LINE) {
            int line = pieces[k].first, want = (sub ? sub : 1) - 1;   /* its sub-th part (or stroke), as the editor counts */
            int by_part = pieces[k].parts > 1;
            for (int i = pieces[k].first; i <= pieces[k].last; i++)
                if (l[i].z && (by_part ? l[i].part : l[i].stroke) == want) { line = i; break; }
            vx_ce_rule_line(l, n, line, r, g, bb, gain, &rule);
        } else vx_ce_rule(pieces, np, k, l, n, sc, r, g, bb, gain, &rule);
        if (vx_colors_put(&colors, &rule) < 0) fprintf(stderr, "vxshot: the profile is full (%d rules)\n", VX_COLOR_RULES);
        int hit = 0;
        for (int i = 0; i < n; i++) hit += l[i].z && vx_rule_matches(&rule, &l[i]);
        printf("edit: piece %d, %s, %s x%.1f -> rule %s (%d lines in this picture)\n", k, scope, colour, gain, rule.name, hit);
        q += len;
        while (*q == ',' || *q == ' ') q++;
    }
    if (opt(argc, argv, "--save")) {
        vx_colors_save(&colors, opt(argc, argv, "--save"), id);
        printf("profile written: %s (%d rules)\n", opt(argc, argv, "--save"), colors.n);
    }

    FILE *f = fopen(argv[4], "w");
    if (!f) { perror(argv[4]); return 1; }
    int W = 700, H = 820;
    fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d' viewBox='0 0 %d %d'>"
               "<rect width='100%%' height='100%%' fill='black'/>\n", W, H, VX_CANVAS_W, VX_CANVAS_H);
    int counted[8] = { 0 };
    for (int i = 0; i < n; i++) {
        uint8_t r, g, bb;
        if (!vx_line_color(&cfg, &colors, &l[i], &r, &g, &bb)) continue;
        fprintf(f, "<line x1='%ld' y1='%ld' x2='%ld' y2='%ld' stroke='rgb(%d,%d,%d)' stroke-width='130' stroke-linecap='round'/>\n",
                l[i].x0, l[i].y0, l[i].x1, l[i].y1, r, g, bb);
        uint8_t pr, pg, pb;
        int k = vx_colors_line(&colors, &l[i], &pr, &pg, &pb);
        counted[k ? 1 : 0]++;
    }
    fprintf(f, "</svg>\n");
    fclose(f);
    printf("%d lines: %d by the profile, %d in the picture's own colour\n", n, counted[1], counted[0]);
    printf("sound: console %.0f, speech %.0f (mean level), %ld frames with speech\n",
           console / (frames * 882.0), speech / (frames * 882.0), speech_frames);
    if (p2.kind != VX_P2_JOYSTICK)
        printf("port 2 %s: lines %X, steps waiting x %.1f y %.1f, spinner position %d\n", vx_port2_name(p2.kind),
               p2.buttons, p2.acc_x, p2.acc_y, p2.pos);
    const char *pk = opt(argc, argv, "--peek");
    if (pk) {
        unsigned a = (unsigned)strtol(pk, 0, 16);
        printf("peek %04X: %02X %02X %02X %02X\n", a, e6809_read8(a), e6809_read8(a + 1), e6809_read8(a + 2), e6809_read8(a + 3));
    }
    vx_close();
    return 0;
}
