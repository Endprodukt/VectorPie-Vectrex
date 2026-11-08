#include "render/draw.h"

#include <math.h>

void vx_map(const vx_config *cfg, double cx, double cy, float *x, float *y)
{
    const double A = (double)VX_CANVAS_W / VX_CANVAS_H;                      /* the picture: standing */
    const double M = cfg->monitor == VX_MONITOR_VERTICAL ? 3.0 / 4.0 : 4.0 / 3.0;   /* the tube */
    /* the parts of the board's square (0..4095 both ways, stretched to the tube) the picture takes */
    double fw, fh;
    if (A < M) { fh = 1.0; fw = A / M; }
    else { fw = 1.0; fh = M / A; }
    fw *= cfg->size * cfg->size_x;
    fh *= cfg->size * cfg->size_y;
    double u = cx / VX_CANVAS_W - 0.5, v = 0.5 - cy / VX_CANVAS_H;          /* (the canvas: y down) */
    if (cfg->flip_x) u = -u;
    if (cfg->flip_y) v = -v;
    *x = (float)(2048.0 + u * fw * 4096.0 + cfg->pos_x);
    *y = (float)(2048.0 + v * fh * 4096.0 + cfg->pos_y);
}

void vx_draw_lines(vp_dvg *d, const vx_config *cfg, const vx_line *l, int n)
{
    int last_level = -1;
    for (int i = 0; i < n; i++) {
        if (l[i].z == 0) continue;
        /* the Vectrex's brightness (0..127) as the colour's level: the board has no intensity of its own */
        double lv = cfg->brightness * pow(l[i].z / 127.0, cfg->gamma);
        if (lv > 1.0) lv = 1.0;
        int level = (int)(lv * 255.0 + 0.5);
        if (level <= 0) continue;
        if (level != last_level) {
            vp_color(d, cfg->color_r * level / 255, cfg->color_g * level / 255, cfg->color_b * level / 255);
            last_level = level;
        }
        float x0, y0, x1, y1;
        vx_map(cfg, (double)l[i].x0, (double)l[i].y0, &x0, &y0);
        vx_map(cfg, (double)l[i].x1, (double)l[i].y1, &x1, &y1);
        vp_moveto(d, (int)lrintf(x0), (int)lrintf(y0));   /* (free where the beam already is: the SDK sends a move only where needed) */
        vp_lineto(d, (int)lrintf(x1), (int)lrintf(y1));   /* (to the same point: a dot) */
    }
}
