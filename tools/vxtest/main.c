/* vxtest - runs the core through vx_core.h without a screen, for checking the library:
 *
 *   vxtest BIOS CART FRAMES [--skip-bios] [--peek ADDR N] [--svg OUT.svg] [--tags OUT.csv]
 *          [--input "F0-F1:MASK[:X;Y],..."] [--hum] [--tags-at "F,F,..."]
 *
 * prints the frames' cycles (pacing), lines and sound, optionally RAM bytes after the run, and the
 * last frame as SVG (canvas units, y down; lines coloured by their tag's X high byte) and as CSV
 * (each line with its tag: pc, x, u). --input holds player 1's buttons (MASK, bit 0..3 = 1..4) and
 * optionally its stick (X;Y) from frame F0 to F1. With --tags-at, --tags is a prefix: PREFIX_F.csv after
 * each frame F listed. The CSV holds each line's tag: pc, x, u and the caller. --hum: the core's mains hum wobble on (off by default, as
 * in the program). */
#include "core/vx_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char (*e6809_read8)(unsigned address);

/* the last frame's lines with their tags, as CSV */
static int write_tags(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 1; }
    const vx_line *l;
    int n = vx_lines(&l);
    fprintf(f, "i,x0,y0,x1,y1,z,pc,x,u,caller,t0,rgb,piece,part,stroke\n");
    for (int i = 0; i < n; i++)
        fprintf(f, "%d,%ld,%ld,%ld,%ld,%d,%04X,%04X,%04X,%04X,%u,%d,%u,%u,%u\n", i, l[i].x0, l[i].y0, l[i].x1, l[i].y1, l[i].z,
                l[i].tag.pc, l[i].tag.x, l[i].tag.u, l[i].tag.caller, l[i].t0, l[i].rgb, l[i].piece, l[i].part, l[i].stroke);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: vxtest BIOS CART FRAMES [--skip-bios] [--peek ADDR N] [--svg OUT]\n");
        return 2;
    }
    int skip = 0, peek_n = 0;
    unsigned peek_a = 0;
    const char *svg = NULL, *tags = NULL, *input = "", *tags_at = NULL;
    int hum = 0;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--tags") && i + 1 < argc) { tags = argv[++i]; continue; }
        if (!strcmp(argv[i], "--tags-at") && i + 1 < argc) { tags_at = argv[++i]; continue; }
        if (!strcmp(argv[i], "--input") && i + 1 < argc) { input = argv[++i]; continue; }
        if (!strcmp(argv[i], "--hum")) { hum = 1; continue; }
        if (!strcmp(argv[i], "--skip-bios")) skip = 1;
        else if (!strcmp(argv[i], "--peek") && i + 2 < argc) { peek_a = (unsigned)strtol(argv[i + 1], 0, 16); peek_n = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--svg") && i + 1 < argc) svg = argv[++i];
    }
    char err[256];
    if (vx_open(argv[1], argv[2], err, sizeof err)) { fprintf(stderr, "vxtest: %s\n", err); return 1; }
    vx_set_mains_hum(hum);
    vx_power_on(skip);
    long frames = atol(argv[3]);
    unsigned long long cyc = 0, audio = 0, lines = 0;
    unsigned long cmin = ~0ul, cmax = 0;
    vx_pad p = { 0, 0, 0 }, p2 = { 0, 0, 0 };
    for (long f = 0; f < frames; f++) {
        p.buttons = 0; p.x = p.y = 0;
        for (const char *q = input; *q;) {          /* "F0-F1:MASK[:X,Y]" items, comma separated */
            long f0 = 0, f1 = 0; unsigned m = 0; int x = 0, y = 0, k = 0;
            if (sscanf(q, "%ld-%ld:%u:%d;%d%n", &f0, &f1, &m, &x, &y, &k) < 5) {
                x = y = 0; k = 0;
                sscanf(q, "%ld-%ld:%u%n", &f0, &f1, &m, &k);
            }
            if (k > 0 && f >= f0 && f <= f1) { p.buttons |= m; if (x || y) { p.x = x; p.y = y; } }
            q += k > 0 ? k : 1;
            while (*q == ',' || *q == ' ') q++;
        }
        vx_set_input(&p, &p2);
        unsigned long c = vx_run_frame();
        if (tags && tags_at) {                      /* --tags-at "F,F,...": PREFIX_F.csv at those frames */
            const char *q = tags_at;
            while (*q) {
                char *e;
                long at = strtol(q, &e, 10);
                if (e == q) break;
                if (at == f + 1) {
                    char path[1024];
                    snprintf(path, sizeof path, "%s_%ld.csv", tags, at);
                    write_tags(path);
                }
                q = e;
                while (*q == ',' || *q == ' ') q++;
            }
        }
        cyc += c;
        if (c < cmin) cmin = c;
        if (c > cmax) cmax = c;
        const vx_line *l;
        lines += vx_lines(&l);
        int n, ch;
        vx_audio(&n, &ch);
        audio += n;
    }
    printf("frames %ld  cycles %llu (%.1f s of Vectrex time)  per frame avg %.0f min %lu max %lu\n",
           frames, cyc, cyc / (double)VX_CPU_HZ, cyc / (double)frames, cmin, cmax);
    printf("lines per frame avg %.0f  sound %llu samples (%.2f s)\n", lines / (double)frames, audio,
           audio / (double)VX_AUDIO_RATE);
    if (peek_n) {
        printf("peek %04X:", peek_a);
        for (int i = 0; i < peek_n; i++) printf(" %02X", e6809_read8((peek_a + i) & 0xffff));
        printf("\n");
    }
    if (svg) {
        FILE *f = fopen(svg, "w");
        if (!f) { perror(svg); return 1; }
        const vx_line *l;
        int n = vx_lines(&l);
        fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 %d %d'><rect width='100%%' height='100%%' fill='black'/>\n",
                VX_CANVAS_W, VX_CANVAS_H);
        static const char *const PAL[8] = { "#ffffff", "#ff5050", "#50ff50", "#5080ff", "#ffff50", "#ff50ff", "#50ffff", "#ffa040" };
        for (int i = 0; i < n; i++)
            fprintf(f, "<line x1='%ld' y1='%ld' x2='%ld' y2='%ld' stroke='%s' stroke-width='140' stroke-linecap='round'/>\n",
                    l[i].x0, l[i].y0, l[i].x1, l[i].y1, PAL[(l[i].tag.x >> 8) & 7]);
        fprintf(f, "</svg>\n");
        fclose(f);
    }
    if (tags && !tags_at && write_tags(tags)) return 1;
    vx_close();
    return 0;
}
