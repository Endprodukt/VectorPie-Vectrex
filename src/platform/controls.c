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
    "menu_up", "menu_down", "menu_left", "menu_right", "menu_select", "menu_back", "menu_options",
};
static const char *const FN_STANDARD[VX_FN_COUNT] = {
    "p1_up", "p1_down", "p1_left", "p1_right", "p1_button1", "p1_button2", "p1_button3", "p1_button4",
    "p2_up", "p2_down", "p2_left", "p2_right", "p2_button1", "p2_button2", "p2_button3", "p2_button4",
    "ui_up", "ui_down", "ui_left", "ui_right", "ui_select", "ui_cancel", "ui_configure+service",
};

const char *vx_controls_path(const char *game_id, char *buf, int size)
{
    if (game_id) snprintf(buf, size, "%s/controls/%s.ini", vp_save_dir(), game_id);
    else snprintf(buf, size, "%s/controls.ini", vp_save_dir());
    return buf;
}

/* The menus' functions with the VectorPie menu's standard: each ui_ action as MAME's settings give it, and
 * where they give it nothing (advmame.rc has no line, and the SDK knows no default for the ui_ actions),
 * the VectorPie menu's own default keys for it (its README: arrows, 1 / Enter / Left Ctrl, Tab, F2). */
static void define_menu(vp_input *in)
{
    static const struct { const char *action, *keys; } PART[VX_FN_COUNT - VX_FN_MENU][2] = {
        { { "ui_up", "key_Up" } },
        { { "ui_down", "key_Down" } },
        { { "ui_left", "key_Left" } },
        { { "ui_right", "key_Right" } },
        { { "ui_select", "key_1+key_Return+key_Left_Ctrl" } },
        { { "ui_cancel", "key_Escape" } },
        { { "ui_configure", "key_Tab" }, { "service", "key_F2" } },
    };
    for (int f = VX_FN_MENU; f < VX_FN_COUNT; f++) {
        char std_[256] = "", buf[256];
        for (int k = 0; k < 2 && PART[f - VX_FN_MENU][k].action; k++) {
            const char *a = PART[f - VX_FN_MENU][k].action;
            vp_input_spelled(in, NULL, a, buf, sizeof buf);
            const char *use = strcmp(buf, "NONE") ? a : PART[f - VX_FN_MENU][k].keys;
            size_t L = strlen(std_);
            snprintf(std_ + L, sizeof std_ - L, "%s%s", L ? "+" : "", use);
        }
        vp_input_define(in, VX_FN_NAME[f], std_);
    }
}

void vx_controls_init(vx_controls *c, vp_input *in, const char *game_id)
{
    memset(c, 0, sizeof *c);
    for (int f = 0; f < VX_FN_MENU; f++) vp_input_define(in, VX_FN_NAME[f], FN_STANDARD[f]);
    define_menu(in);
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
    c->menu = vx_options_key(in);
}

void vx_fn_table_read(vx_fn_table *t, const char *path)
{
    memset(t, 0, sizeof *t);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#' || *p == ';') continue;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        char *k = p, *ke = eq;
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
        *ke = '\0';
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        char *ve = v + strlen(v);
        while (ve > v && (ve[-1] == '\n' || ve[-1] == '\r' || ve[-1] == ' ' || ve[-1] == '\t')) ve--;
        *ve = '\0';
        for (int i = 0; i < VX_FN_COUNT; i++)
            if (!strcmp(k, VX_FN_NAME[i])) snprintf(t->v[i], sizeof t->v[i], "%s", v);
    }
    fclose(f);
}

int vx_fn_table_write(const vx_fn_table *t, const char *path)
{
    int any = 0;
    for (int i = 0; i < VX_FN_COUNT; i++) any |= t->v[i][0] != '\0';
    if (!any) { remove(path); return 0; }
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < VX_FN_COUNT; i++)
        if (t->v[i][0]) fprintf(f, "%s = %s\n", VX_FN_NAME[i], t->v[i]);
    fclose(f);
    return 0;
}

/* Defines the menus' functions on `in` and loads them (every game's controls.ini). */
static void menu_controls(vp_input *in)
{
    define_menu(in);
    char path[1024];
    vp_input_load(in, vx_controls_path(NULL, path, sizeof path));     /* (none yet: the standards) */
}

vx_nav vx_menu_nav(vp_input *in)
{
    if (!vp_input_controls(in, "menu_up")) menu_controls(in);
    vx_nav n;
    /* (the arrow keys always too, whatever the menu functions have: the owner's wish) */
    int kl = vp_input_key_held(in, SDL_SCANCODE_LEFT), kr = vp_input_key_held(in, SDL_SCANCODE_RIGHT);
    int ku = vp_input_key_held(in, SDL_SCANCODE_UP), kd = vp_input_key_held(in, SDL_SCANCODE_DOWN);
    n.x = vp_input_held(in, "menu_left") || kl ? -1 : vp_input_held(in, "menu_right") || kr ? 1 : 0;
    n.y = vp_input_held(in, "menu_up") || ku ? -1 : vp_input_held(in, "menu_down") || kd ? 1 : 0;
    n.px = vp_input_pressed(in, "menu_left") || vp_input_key_pressed(in, SDL_SCANCODE_LEFT) ? -1
         : vp_input_pressed(in, "menu_right") || vp_input_key_pressed(in, SDL_SCANCODE_RIGHT) ? 1 : 0;
    n.py = vp_input_pressed(in, "menu_up") || vp_input_key_pressed(in, SDL_SCANCODE_UP) ? -1
         : vp_input_pressed(in, "menu_down") || vp_input_key_pressed(in, SDL_SCANCODE_DOWN) ? 1 : 0;
    n.fire = vp_input_pressed(in, "menu_select") || vp_input_key_pressed(in, SDL_SCANCODE_RETURN) ||
             vp_input_key_pressed(in, SDL_SCANCODE_KP_ENTER);
    n.fire_held = vp_input_held(in, "menu_select") || vp_input_key_held(in, SDL_SCANCODE_RETURN) ||
                  vp_input_key_held(in, SDL_SCANCODE_KP_ENTER);
    n.back = vp_input_pressed(in, "menu_back") || vp_input_key_pressed(in, SDL_SCANCODE_ESCAPE);
    n.close = vx_options_key(in);
    return n;
}

int vx_options_key(vp_input *in)
{
    if (!vp_input_controls(in, "menu_options")) menu_controls(in);
    return vp_input_key_pressed(in, SDL_SCANCODE_TAB) || vp_input_key_pressed(in, SDL_SCANCODE_F2) ||
           vp_input_pressed(in, "menu_options");
}
