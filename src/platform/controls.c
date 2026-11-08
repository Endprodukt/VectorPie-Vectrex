#include "platform/controls.h"

#include <vectorpie/vp_controls.h>
#include <vectorpie/vp_paths.h>

#include <SDL.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/* the functions, by their names in controls.ini, and their standards (the cabinet's advmame.rc
 * actions). (Named apart from the actions: a function of an action's own name would hide it.) */
const char *const VX_FN_NAME[VX_FN_COUNT] = {
    "pad1_up", "pad1_down", "pad1_left", "pad1_right", "pad1_b1", "pad1_b2", "pad1_b3", "pad1_b4",
    "pad2_up", "pad2_down", "pad2_left", "pad2_right", "pad2_b1", "pad2_b2", "pad2_b3", "pad2_b4",
};
static const char *const FN_STANDARD[VX_FN_COUNT] = {
    "p1_up", "p1_down", "p1_left", "p1_right", "p1_button1", "p1_button2", "p1_button3", "p1_button4",
    "p2_up", "p2_down", "p2_left", "p2_right", "p2_button1", "p2_button2", "p2_button3", "p2_button4",
};

const char *vx_controls_path(const char *game_id, char *buf, int size)
{
    if (game_id) snprintf(buf, size, "%s/controls/%s.ini", vp_save_dir(), game_id);
    else snprintf(buf, size, "%s/controls.ini", vp_save_dir());
    return buf;
}

void vx_controls_init(vx_controls *c, vp_input *in, const char *game_id)
{
    memset(c, 0, sizeof *c);
    for (int f = 0; f < VX_FN_COUNT; f++) vp_input_define(in, VX_FN_NAME[f], FN_STANDARD[f]);
    char path[1024];
    vp_input_load(in, vx_controls_path(NULL, path, sizeof path));     /* (none yet: the standards) */
    vp_input_load(in, vx_controls_path(game_id, path, sizeof path));
}

float vx_ramp_step(float phase, int dir, double dt, const vx_config *cfg)
{
    float target = (float)dir;
    if (phase == target) return phase;
    /* away from the middle (or through it): the attack; back towards it: the release */
    int outward = dir != 0 && (phase == 0.0f || (phase > 0.0f) == (dir > 0));
    float t = outward ? cfg->dig_attack : cfg->dig_release;
    float step = t > 0.0005f ? (float)(dt / t) : 2.0f;
    if (dir != 0 && !outward) {
        /* pushed the other way: back to the middle at the release's speed, then out */
        float r = cfg->dig_release > 0.0005f ? (float)(dt / cfg->dig_release) : 2.0f;
        phase += phase > 0.0f ? -r : r;
        if ((phase > 0.0f) == (dir > 0) || phase == 0.0f) phase = 0.0f;
        return phase;
    }
    if (phase < target) { phase += step; if (phase > target) phase = target; }
    else { phase -= step; if (phase < target) phase = target; }
    return phase;
}

float vx_ramp_out(float phase, const vx_config *cfg)
{
    float a = fabsf(phase);
    float k = cfg->dig_curve > 0.05f ? cfg->dig_curve : 0.05f;
    float v = powf(a, k) * cfg->dig_max;
    return phase < 0.0f ? -v : v;
}

static int to_port(float v)
{
    int i = (int)lrintf(v * 127.0f);
    return i < -128 ? -128 : i > 127 ? 127 : i;
}

void vx_controls_read(vx_controls *c, vp_input *in, const vx_config *cfg, double dt,
                      vx_pad *p1, vx_pad *p2)
{
    vp_input_set_deadzone(in, cfg->deadzone);
    vx_pad *pad[2] = { p1, p2 };
    char fn[32];
    for (int p = 0; p < 2; p++) {
        int held[4];        /* up, down, left, right */
        for (int w = 0; w < 4; w++) held[w] = vp_input_held(in, VX_FN_NAME[p * 8 + w]);
        int dx = held[3] - held[2], dy = held[0] - held[1];   /* y: up positive, as the Vectrex reads it */
        c->phase[p][0] = vx_ramp_step(c->phase[p][0], dx, dt, cfg);
        c->phase[p][1] = vx_ramp_step(c->phase[p][1], dy, dt, cfg);
        float x = vx_ramp_out(c->phase[p][0], cfg), y = vx_ramp_out(c->phase[p][1], cfg);
        /* an analog stick past its dead zone goes straight through (the SDK's up is negative) */
        snprintf(fn, sizeof fn, "p%d_stickx", p + 1);
        float ax = vp_input_axis(in, fn);
        snprintf(fn, sizeof fn, "p%d_sticky", p + 1);
        float ay = -vp_input_axis(in, fn);
        if (ax != 0.0f) x = ax;
        if (ay != 0.0f) y = ay;
        pad[p]->x = to_port(x);
        pad[p]->y = to_port(y);
        pad[p]->buttons = 0;
        for (int b = 0; b < 4; b++)
            if (vp_input_held(in, VX_FN_NAME[p * 8 + 4 + b])) pad[p]->buttons |= 1u << b;
    }
    c->menu = vp_input_key_pressed(in, SDL_SCANCODE_TAB) || vp_input_key_pressed(in, SDL_SCANCODE_F2)
           || vp_input_pressed(in, "ui_configure") || vp_input_pressed(in, "service");
}
