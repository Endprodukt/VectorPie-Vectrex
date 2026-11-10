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

void vx_draw_frame(vp_dvg *d, const vx_config *cfg)
{
    static const double P[5][2] = { { 0, 0 }, { VX_CANVAS_W, 0 }, { VX_CANVAS_W, VX_CANVAS_H }, { 0, VX_CANVAS_H }, { 0, 0 } };
    float x, y;
    vp_color(d, 70, 90, 140);
    for (int i = 0; i < 5; i++) {
        vx_map(cfg, P[i][0], P[i][1], &x, &y);
        if (i == 0) vp_moveto(d, (int)lrintf(x), (int)lrintf(y));
        else vp_lineto(d, (int)lrintf(x), (int)lrintf(y));
    }
    const double cx = VX_CANVAS_W / 2.0, cy = VX_CANVAS_H / 2.0, k = VX_CANVAS_W / 20.0;
    vx_map(cfg, cx - k, cy, &x, &y); vp_moveto(d, (int)lrintf(x), (int)lrintf(y));
    vx_map(cfg, cx + k, cy, &x, &y); vp_lineto(d, (int)lrintf(x), (int)lrintf(y));
    vx_map(cfg, cx, cy - k, &x, &y); vp_moveto(d, (int)lrintf(x), (int)lrintf(y));
    vx_map(cfg, cx, cy + k, &x, &y); vp_lineto(d, (int)lrintf(x), (int)lrintf(y));
}

int vx_line_color(const vx_config *cfg, const vx_colors *colors, const vx_line *l, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (l->z == 0) return 0;
    /* the Vectrex's brightness (0..127) as the colour's level: the board has no intensity of its own */
    double lv = cfg->brightness * pow(l->z / 127.0, cfg->gamma);
    uint8_t cr = (uint8_t)cfg->color_r, cg = (uint8_t)cfg->color_g, cb = (uint8_t)cfg->color_b;
    if (cfg->color_mode == VX_COLOR_BRIGHTNESS) {   /* by the Vectrex's brightness: the band's colour */
        int rgb = vx_band_color(cfg, vx_band_of(l->z));
        cr = (uint8_t)(rgb >> 16); cg = (uint8_t)(rgb >> 8); cb = (uint8_t)rgb;
    }
    const vx_rule *u = colors && cfg->color_profile ? vx_colors_find(colors, l) : NULL;
    if (u) { cr = u->r; cg = u->g; cb = u->b; lv *= u->gain; }
    if (lv > 1.0) lv = 1.0;
    int level = (int)(lv * 255.0 + 0.5);
    if (level <= 0) return 0;
    *r = (uint8_t)(cr * level / 255);
    *g = (uint8_t)(cg * level / 255);
    *b = (uint8_t)(cb * level / 255);
    return 1;
}

void vx_draw_lines(vp_dvg *d, const vx_config *cfg, const vx_colors *colors, const vx_line *l, int n)
{
    uint32_t last = 0xffffffffu;
    for (int i = 0; i < n; i++) {
        uint8_t r, g, b;
        if (!vx_line_color(cfg, colors, &l[i], &r, &g, &b)) continue;
        uint32_t rgb = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
        if (rgb != last) { vp_color(d, r, g, b); last = rgb; }
        float x0, y0, x1, y1;
        vx_map(cfg, (double)l[i].x0, (double)l[i].y0, &x0, &y0);
        vx_map(cfg, (double)l[i].x1, (double)l[i].y1, &x1, &y1);
        vp_moveto(d, (int)lrintf(x0), (int)lrintf(y0));   /* (free where the beam already is: the SDK sends a move only where needed) */
        vp_lineto(d, (int)lrintf(x1), (int)lrintf(y1));   /* (to the same point: a dot) */
    }
}
