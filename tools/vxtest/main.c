/* vxtest - runs the core through vx_core.h without a screen, for checking the library:
 *
 *   vxtest BIOS CART FRAMES [--skip-bios] [--peek ADDR N] [--svg OUT.svg]
 *
 * prints the frames' cycles (pacing), lines and sound, optionally RAM bytes after the run, and the
 * last frame as SVG (canvas units, y down). */
#include "core/vx_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char (*e6809_read8)(unsigned address);

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: vxtest BIOS CART FRAMES [--skip-bios] [--peek ADDR N] [--svg OUT]\n");
        return 2;
    }
    int skip = 0, peek_n = 0;
    unsigned peek_a = 0;
    const char *svg = NULL;
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--skip-bios")) skip = 1;
        else if (!strcmp(argv[i], "--peek") && i + 2 < argc) { peek_a = (unsigned)strtol(argv[i + 1], 0, 16); peek_n = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(argv[i], "--svg") && i + 1 < argc) svg = argv[++i];
    }
    char err[256];
    if (vx_open(argv[1], argv[2], err, sizeof err)) { fprintf(stderr, "vxtest: %s\n", err); return 1; }
    vx_power_on(skip);
    long frames = atol(argv[3]);
    unsigned long long cyc = 0, audio = 0, lines = 0;
    unsigned long cmin = ~0ul, cmax = 0;
    vx_pad p = { 0, 0, 0 };
    for (long f = 0; f < frames; f++) {
        vx_set_input(&p, &p);
        unsigned long c = vx_run_frame();
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
        for (int i = 0; i < n; i++) {
            int v = 80 + l[i].z * 175 / 127;
            fprintf(f, "<line x1='%ld' y1='%ld' x2='%ld' y2='%ld' stroke='rgb(%d,%d,%d)' stroke-width='120' stroke-linecap='round'/>\n",
                    l[i].x0, l[i].y0, l[i].x1, l[i].y1, v, v, v);
        }
        fprintf(f, "</svg>\n");
        fclose(f);
    }
    vx_close();
    return 0;
}
