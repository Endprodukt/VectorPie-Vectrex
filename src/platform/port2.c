#include "platform/port2.h"

#include <math.h>
#include <string.h>

/* spinner position 0..3 as buttons 1 (bit 0) and 2 (bit 1): 00, button 2, both, button 1 (the example's
 * tblSpin read the other way) */
static const unsigned SPIN_LINES[4] = { 0, 2, 3, 1 };

/* A device driving the buttons: player 2's own controls are not on the port. */
static int drives_buttons(const vx_port2_dev *p)
{
    return p->kind == VX_P2_SPINNER || p->kind == VX_P2_TRACKBALL;
}

void vx_port2_setup(vx_port2_dev *p, const vx_config *cfg)
{
    int kind = cfg->port2 >= 0 && cfg->port2 < VX_P2_COUNT ? cfg->port2 : VX_P2_JOYSTICK;
    if (kind != p->kind) {
        memset(p, 0, sizeof *p);
        p->kind = kind;
    }
    int steps = kind == VX_P2_SPINNER ? cfg->spin_steps : cfg->tb_steps;
    if (steps < 1) steps = 1;
    if (steps > 60) steps = 60;
    p->gap_x = p->gap_y = 30000u / (unsigned)steps;     /* (a Vectrex frame: 30000 cycles) */
    p->limit = steps;
    vx_set_port2_device((vx_port2)kind);
    vx_set_port2_hook(drives_buttons(p) ? vx_port2_hook : NULL, p);
}

static double clampd(double v, double lim) { return v > lim ? lim : v < -lim ? -lim : v; }

void vx_port2_read(vx_port2_dev *p, vp_input *in, const vx_config *cfg)
{
    if (p->kind == VX_P2_SPINNER) {
        double d = (double)vp_input_delta(in, "p1_dialx") * cfg->spin_sens;
        if (cfg->spin_reverse) d = -d;
        p->acc_x = clampd(p->acc_x + d, p->limit + 0.999);
    } else if (p->kind == VX_P2_TRACKBALL) {
        double dx = (double)vp_input_delta(in, "p1_trackballx") * cfg->tb_sens;
        double dy = -(double)vp_input_delta(in, "p1_trackbally") * cfg->tb_sens;   /* (the SDK's y grows down) */
        if (cfg->tb_reverse_x) dx = -dx;
        if (cfg->tb_reverse_y) dy = -dy;
        p->acc_x = clampd(p->acc_x + dx, p->limit + 0.999);
        p->acc_y = clampd(p->acc_y + dy, p->limit + 0.999);
    }
}

/* one step of acc due at `cycle`: -1, 0, 1. The steps waiting are spread over a frame (30000 cycles),
 * gap apart at the least: a game that reads the port only now and then still sees each line change
 * (two steps close together turn a trackball's clock line over and back unseen). */
static int take(double *acc, unsigned long long *next, unsigned gap, unsigned long long cycle)
{
    if (cycle < *next) return 0;
    int s = *acc >= 1.0 ? 1 : *acc <= -1.0 ? -1 : 0;
    if (!s) return 0;
    *acc -= s;
    double left = fabs(*acc);
    unsigned spread = (unsigned)(30000.0 / (left + 1.0));
    *next = cycle + (spread > gap ? spread : gap);
    return s;
}

unsigned vx_port2_hook(void *user, unsigned long long cycle)
{
    vx_port2_dev *p = user;
    if (p->kind == VX_P2_SPINNER) {
        int s = take(&p->acc_x, &p->next_x, p->gap_x, cycle);
        if (s) p->pos = (p->pos + s) & 3;
        p->buttons = SPIN_LINES[p->pos];
    } else if (p->kind == VX_P2_TRACKBALL) {
        int sx = take(&p->acc_x, &p->next_x, p->gap_x, cycle);
        if (sx) { p->dir_x = sx < 0; p->clk_x ^= 1; }          /* left: the direction line pressed */
        int sy = take(&p->acc_y, &p->next_y, p->gap_y, cycle);
        if (sy) { p->dir_y = sy > 0; p->clk_y ^= 1; }          /* up: the direction line pressed */
        p->buttons = (unsigned)(p->dir_x | p->clk_x << 1 | p->dir_y << 2 | p->clk_y << 3);
    } else {
        p->buttons = 0;
    }
    return p->buttons;
}
