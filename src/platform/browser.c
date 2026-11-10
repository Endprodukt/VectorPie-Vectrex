#include "platform/browser.h"
#include "platform/controls.h"

#include <vectorpie/vp_controls.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MINE_STORM "MINE STORM"

const char *vx_browser_name(const vx_rom_list *l, int e)
{
    return e <= 0 ? MINE_STORM : l->rom[e - 1].name;
}

const char *vx_browser_id(const vx_rom_list *l, int e)
{
    return e <= 0 ? "minestorm" : l->rom[e - 1].id;
}

void vx_browser_init(vx_browser *b, const vx_rom_list *l, const char *last)
{
    memset(b, 0, sizeof *b);
    if (last && *last)
        for (int e = 0; e <= l->n; e++)
            if (!strcmp(vx_browser_id(l, e), last)) { b->sel = e; break; }
}

static int first_letter(const vx_rom_list *l, int e)
{
    return (unsigned char)vx_browser_name(l, e)[0];
}

int vx_browser_update(vx_browser *b, const vx_rom_list *l, vp_input *in, double now)
{
    int count = l->n + 1;
    vx_nav nav = vx_menu_nav(in);               /* (the VectorPie menu's controls, controls.h) */
    int select_held = nav.fire_held;
    if (!b->armed) {                /* (a game just left, or the list just opened: its key may still be down) */
        if (!select_held) b->armed = 1;
        return -1;
    }
    int y = nav.y;
    int step = 0;
    if (nav.py < 0) { step = -1; b->rep_dir = -1; b->rep_at = now + 0.40; }
    else if (nav.py > 0) { step = 1; b->rep_dir = 1; b->rep_at = now + 0.40; }
    else if (y != 0 && y == b->rep_dir && now >= b->rep_at) { step = y; b->rep_at = now + 0.07; }
    else if (y == 0) b->rep_dir = 0;
    if (step) b->sel = (b->sel + step + count) % count;

    if (nav.px) {
        int dir = nav.px;
        int c0 = first_letter(l, b->sel), e = b->sel;
        for (int k = 1; k < count; k++) {
            e = (e + dir + count) % count;
            if (first_letter(l, e) != c0) break;
        }
        if (dir < 0) {              /* to the first of that letter */
            int c1 = first_letter(l, e);
            while (e > 0 && first_letter(l, e - 1) == c1) e--;
        }
        b->sel = e;
    }
    if (nav.fire) {
        b->armed = 0;
        return b->sel;
    }
    return -1;
}

/* text no wider than `w`: smaller when it would be */
static void fit_text(vp_dvg *d, const char *s, float x, float y, float h, float w)
{
    float tw = vp_text_width(s, h);
    if (tw > w) h *= w / tw;
    vp_text(d, s, x, y, h, VP_TEXT_CENTER);
}

void vx_browser_draw(const vx_browser *b, const vx_rom_list *l, vp_dvg *d, double now)
{
    vp_text_font(VP_FONT_ASTEROIDS);
    vp_color(d, 255, 220, 60);
    vp_text(d, "VECTREX", 2048, 3740, 110, VP_TEXT_CENTER);

    int count = l->n + 1;
    const float H = 130.0f, W = 3400.0f;
    for (int k = -6; k <= 6; k++) {
        int e = b->sel + k;
        if (e < 0 || e >= count) continue;
        float a = (float)(k < 0 ? -k : k);
        float h = H * (k == 0 ? 1.0f : 0.62f - 0.06f * a);
        float y = 2048.0f - (k < 0 ? -1.0f : 1.0f) * (a == 0 ? 0.0f : 210.0f + (a - 1.0f) * 150.0f - (a - 1.0f) * (a - 1.0f) * 6.0f);
        if (k == 0) {
            float p = 0.5f + 0.5f * (float)sin(now * 5.0);              /* the pulse */
            vp_color(d, 255, (uint8_t)(190 + 50 * p), (uint8_t)(40 + 40 * p));
            fit_text(d, vx_browser_name(l, e), 2048.0f, y - h * 0.5f, h, W);
        } else {
            int v = (int)(230.0f - 26.0f * a);
            vp_color(d, (uint8_t)(v * 150 / 255), (uint8_t)(v * 160 / 255), (uint8_t)v);
            fit_text(d, vx_browser_name(l, e), 2048.0f, y - h * 0.5f, h, W * (1.0f - 0.05f * a));
        }
    }

    char hint[160];
    vp_color(d, 110, 130, 255);
    if (l->n == 0) {
        vp_text(d, "NO CARTRIDGES FOUND: PUT THEM IN", 2048, 760, 60, VP_TEXT_CENTER);
        vp_text(d, l->dirs[0], 2048, 660, 55, VP_TEXT_CENTER);   /* (the first folder: on the cabinet /persistent/games/rom/vectrex) */
    }
    snprintf(hint, sizeof hint, "START: PLAY    TAB: OPTIONS    ESC: BACK    %d / %d", b->sel + 1, count);
    vp_text(d, hint, 2048, 330, 60, VP_TEXT_CENTER);
}
