/* menu.c - see menu.h (built as Major Havoc PE Enhanced's platform/menu.c). */
#include "platform/menu.h"

#include <vectorpie/vp_paths.h>

#include <SDL.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define vx_mkdir(p) _mkdir(p)
#else
#define vx_mkdir(p) mkdir(p, 0755)
#endif

enum {
    L_GAP = 0,
    L_PAGE_PAD1, L_PAGE_PAD2, L_PAGE_STICK, L_PAGE_PICTURE, L_PAGE_MACHINE, L_PAGE_SOUND,
    L_SCOPE, L_GAME_RESET, L_RESTART, L_EXIT, L_QUIT, L_BACK,
    L_DEFAULTS1, L_DEFAULTS2,
    L_ATTACK, L_RELEASE, L_CURVE, L_MAX, L_DEADZONE, L_TEST,
    L_MONITOR, L_SIZE, L_WIDTH, L_HEIGHT, L_POS_X, L_POS_Y, L_FLIP_X, L_FLIP_Y, L_BRIGHT, L_GAMMA,
    L_COLOR, L_BOARD_HZ,
    L_BIOS, L_UVMC2, L_NOTE_NEXT,
    L_VOLUME,
    L_FN0,                      /* + the function (controls.h VX_FN_NAME) */
    L_END = 0xff
};

enum { P_MAIN, P_PAD1, P_PAD2, P_STICK, P_PICTURE, P_MACHINE, P_SOUND, N_PAGES };
static const char *const TITLE[N_PAGES] = {
    "", "CONTROLS  PLAYER 1", "CONTROLS  PLAYER 2", "STICK", "PICTURE", "MACHINE", "SOUND",
};
static const char *const FN_LABEL[8] = {
    "STICK UP", "STICK DOWN", "STICK LEFT", "STICK RIGHT", "BUTTON 1", "BUTTON 2", "BUTTON 3", "BUTTON 4",
};

typedef struct { const char *name; int r, g, b; } color_preset;
static const color_preset COLORS[] = {
    { "WHITE", 255, 255, 255 }, { "BLUE WHITE", 200, 220, 255 }, { "GREEN", 80, 255, 120 },
    { "AMBER", 255, 180, 60 }, { "YELLOW", 255, 240, 80 }, { "RED", 255, 60, 50 },
    { "BLUE", 80, 140, 255 }, { "PURPLE", 200, 100, 255 },
};
#define NCOLORS (int)(sizeof COLORS / sizeof COLORS[0])

#define PAGE_MAX 24

static int page_lines(const vx_menu *u, int page, uint8_t *out)
{
    int n = 0;
#define ADD(l) out[n++] = (uint8_t)(l)
    switch (page) {
    case P_PAD1:
    case P_PAD2:
        for (int f = 0; f < 8; f++) ADD(L_FN0 + (page == P_PAD2 ? 8 : 0) + f);
        ADD(L_GAP); ADD(page == P_PAD2 ? L_DEFAULTS2 : L_DEFAULTS1); ADD(L_BACK);
        break;
    case P_STICK:
        ADD(L_ATTACK); ADD(L_RELEASE); ADD(L_CURVE); ADD(L_MAX); ADD(L_GAP); ADD(L_DEADZONE); ADD(L_GAP);
        ADD(L_TEST); ADD(L_BACK);
        break;
    case P_PICTURE:
        ADD(L_MONITOR); ADD(L_SIZE); ADD(L_WIDTH); ADD(L_HEIGHT); ADD(L_POS_X); ADD(L_POS_Y);
        ADD(L_FLIP_X); ADD(L_FLIP_Y); ADD(L_GAP); ADD(L_BRIGHT); ADD(L_GAMMA); ADD(L_COLOR); ADD(L_BOARD_HZ);
        ADD(L_GAP); ADD(L_BACK);
        break;
    case P_MACHINE:
        ADD(L_BIOS); if (u->uvmc2) ADD(L_UVMC2); ADD(L_NOTE_NEXT); ADD(L_GAP); ADD(L_RESTART); ADD(L_BACK);
        break;
    case P_SOUND:
        ADD(L_VOLUME); ADD(L_GAP); ADD(L_BACK);
        break;
    default:
        ADD(L_PAGE_PAD1); ADD(L_PAGE_PAD2); ADD(L_PAGE_STICK); ADD(L_PAGE_PICTURE); ADD(L_PAGE_MACHINE);
        ADD(L_PAGE_SOUND); ADD(L_GAP); ADD(L_SCOPE); ADD(L_GAME_RESET); ADD(L_GAP);
        ADD(L_RESTART); ADD(L_EXIT); ADD(L_QUIT);
        break;
    }
#undef ADD
    out[n] = L_END;
    return n;
}

static int selectable(int line) { return line != L_GAP && line != L_NOTE_NEXT; }

static int chosen_line(const uint8_t *lines, int item, int *count)
{
    int k = 0, line = L_GAP;
    for (int i = 0; lines[i] != L_END; i++) {
        if (!selectable(lines[i])) continue;
        if (k == item) line = lines[i];
        k++;
    }
    *count = k;
    return line;
}

static float stepf(float v, int d, float by, float lo, float hi)
{
    v = roundf(v / by) * by + (float)d * by;
    return v < lo ? lo : v > hi ? hi : v;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int color_index(const vx_config *c)
{
    for (int i = 0; i < NCOLORS; i++)
        if (COLORS[i].r == c->color_r && COLORS[i].g == c->color_g && COLORS[i].b == c->color_b) return i;
    return -1;
}

int vx_menu_on_picture(const vx_menu *u) { return u->open && u->page == P_PICTURE; }

void vx_menu_open(vx_menu *u, const vx_config *cfg, const char *game_id, const char *game_name, int uvmc2)
{
    int scope = u->scope;                   /* (kept while the game runs) */
    memset(u, 0, sizeof *u);
    u->scope = scope;
    u->open = 1;
    u->page = P_MAIN;
    u->at_open = *cfg;
    u->uvmc2 = uvmc2;
    snprintf(u->game, sizeof u->game, "%s", game_id);
    snprintf(u->title, sizeof u->title, "%s", game_name);
}

/* --- the menu's own controls -------------------------------------------------- */
typedef struct { int x, y, px, py, fire, back, del, close; } nav_t;

static nav_t read_nav(vp_input *in)
{
    nav_t n;
    n.x = vp_input_menu_way_x(in);
    n.y = vp_input_menu_way_y(in);
    n.px = vp_input_menu_left_pressed(in) ? -1 : vp_input_menu_right_pressed(in) ? 1 : 0;
    n.py = vp_input_menu_up(in) ? -1 : vp_input_menu_down(in) ? 1 : 0;
    n.fire = vp_input_menu_confirm(in);
    n.back = vp_input_pressed(in, "ui_cancel") || vp_input_key_pressed(in, SDL_SCANCODE_ESCAPE);
    n.del = vp_input_key_pressed(in, SDL_SCANCODE_DELETE) || vp_input_key_pressed(in, SDL_SCANCODE_KP_PERIOD);
    n.close = vp_input_key_pressed(in, SDL_SCANCODE_TAB) || vp_input_key_pressed(in, SDL_SCANCODE_F2) ||
              vp_input_pressed(in, "ui_configure") || vp_input_pressed(in, "service");
    return n;
}

int vx_menu_update(vx_menu *u, vx_config *cfg, const vx_config *all, vp_input *in, const vx_pad *p1, double now)
{
    if (u->page < 0 || u->page >= N_PAGES) u->page = P_MAIN;
    nav_t nav = read_nav(in);

    if (u->testing) {                       /* the stick test: the controls are the game's until Esc / Enter */
        u->test_pad = *p1;
        if (nav.back || vp_input_key_pressed(in, SDL_SCANCODE_RETURN)) {
            u->testing = 0;
            u->armed_x = u->armed_y = 0;
        }
        return VX_MENU_NOTHING;
    }
    if (u->cap.capturing) {
        if (vp_capture_step(&u->cap, in) == VP_CAPTURE_CHANGED) u->controls_changed = 1;
        u->dir_x = nav.x;
        u->dir_y = nav.y;
        u->repeat_at = now + 0.6;
        return VX_MENU_NOTHING;
    }
    if (nav.close) return VX_MENU_CLOSE;

    uint8_t lines[PAGE_MAX + 1];
    int n;
    page_lines(u, u->page, lines);
    int line = chosen_line(lines, u->item[u->page], &n);
    if (u->item[u->page] >= n) {
        u->item[u->page] = 0;
        line = chosen_line(lines, 0, &n);
    }

    if (nav.back) {
        vp_capture_moved(&u->cap);
        if (u->page == P_MAIN) return VX_MENU_CLOSE;
        u->page = P_MAIN;
        return VX_MENU_NOTHING;
    }

    /* a push counts once, and again and again while held; a direction held counts only once it was
     * let go after the menu opened (a stick resting off its middle would scroll on) */
    if (!nav.x) u->armed_x = 1;
    if (!nav.y) u->armed_y = 1;
    int x = u->armed_x ? nav.x : 0, y = u->armed_y ? nav.y : 0, dx = 0, dy = 0;
    if (nav.px || nav.py) {
        dx = nav.px; dy = nav.py;
        u->repeat_at = now + 0.45; u->rep_n = 0;
    } else if (x != u->dir_x || y != u->dir_y) {
        dx = x != u->dir_x ? x : 0;
        dy = y != u->dir_y ? y : 0;
        u->repeat_at = now + 0.45; u->rep_n = 0;
    } else if ((x || y) && now >= u->repeat_at) {
        dx = x; dy = y;
        u->repeat_at = now + 0.08; u->rep_n++;
    }
    int mult = u->rep_n < 10 ? 1 : 4;       /* held longer, the steps grow */
    u->dir_x = x;
    u->dir_y = y;
    int fire = nav.fire;

    if (dy) {
        u->item[u->page] = clampi(u->item[u->page] + dy, 0, n - 1);
        u->confirm = 0;
        vp_capture_moved(&u->cap);          /* the next control laid out replaces again */
        return VX_MENU_NOTHING;
    }

    if (line >= L_FN0) {                    /* a function's row: laying its controls out */
        const char *name = VX_FN_NAME[line - L_FN0];
        int left = dx < 0, right = dx > 0;
        vp_capture_filter(&u->cap, &left, &right);
        if (fire) vp_capture_begin(&u->cap, in, name);
        else if (nav.del) { vp_capture_standard(&u->cap, in, name); u->controls_changed = 1; }
        else if (left || right) { vp_capture_toggle(&u->cap, in, name); u->controls_changed = 1; }
        return VX_MENU_NOTHING;
    }

    int d = dx ? dx : (fire ? 1 : 0);
    if (!d) return VX_MENU_NOTHING;
    switch (line) {
    case L_PAGE_PAD1:    if (fire) { u->page = P_PAD1; vp_capture_moved(&u->cap); } break;
    case L_PAGE_PAD2:    if (fire) { u->page = P_PAD2; vp_capture_moved(&u->cap); } break;
    case L_PAGE_STICK:   if (fire) u->page = P_STICK; break;
    case L_PAGE_PICTURE: if (fire) u->page = P_PICTURE; break;
    case L_PAGE_MACHINE: if (fire) u->page = P_MACHINE; break;
    case L_PAGE_SOUND:   if (fire) u->page = P_SOUND; break;
    case L_BACK:         if (fire) u->page = P_MAIN; break;
    case L_EXIT:         if (fire) return VX_MENU_CLOSE; break;
    case L_QUIT:         if (fire) return VX_MENU_QUIT; break;
    case L_RESTART:      if (fire) return VX_MENU_RESTART; break;
    case L_SCOPE:        u->scope = !u->scope; break;
    case L_GAME_RESET:
        if (!fire) break;
        if (!u->confirm) { u->confirm = 1; break; }
        u->confirm = 0;
        *cfg = *all;                        /* every game's settings, and every game's controls */
        for (int f = 0; f < VX_FN_COUNT; f++) vp_input_set(in, VX_FN_NAME[f], "standard");
        { char p[1024]; vp_input_load(in, vx_controls_path(NULL, p, sizeof p)); }
        u->controls_changed = 2;            /* (2: this game's own controls file goes) */
        return VX_MENU_CHANGED;
    case L_DEFAULTS1:
    case L_DEFAULTS2:
        if (!fire) break;
        for (int f = 0; f < 8; f++) vp_input_set(in, VX_FN_NAME[(line == L_DEFAULTS2 ? 8 : 0) + f], "standard");
        u->controls_changed = 1;
        break;
    case L_TEST:         if (fire) { u->testing = 1; u->test_pad = *p1; } break;
    case L_ATTACK:       cfg->dig_attack = stepf(cfg->dig_attack, d * mult, 0.02f, 0.0f, 1.0f); return VX_MENU_CHANGED;
    case L_RELEASE:      cfg->dig_release = stepf(cfg->dig_release, d * mult, 0.02f, 0.0f, 1.0f); return VX_MENU_CHANGED;
    case L_CURVE:        cfg->dig_curve = stepf(cfg->dig_curve, d * mult, 0.05f, 0.25f, 4.0f); return VX_MENU_CHANGED;
    case L_MAX:          cfg->dig_max = stepf(cfg->dig_max, d, 0.05f, 0.1f, 1.0f); return VX_MENU_CHANGED;
    case L_DEADZONE:     cfg->deadzone = stepf(cfg->deadzone, d, 0.01f, 0.0f, 0.5f); return VX_MENU_CHANGED;
    case L_MONITOR:      cfg->monitor = !cfg->monitor; return VX_MENU_CHANGED;
    case L_SIZE:         cfg->size = stepf(cfg->size, d * mult, 0.01f, 0.5f, 1.5f); return VX_MENU_CHANGED;
    case L_WIDTH:        cfg->size_x = stepf(cfg->size_x, d * mult, 0.01f, 0.5f, 1.5f); return VX_MENU_CHANGED;
    case L_HEIGHT:       cfg->size_y = stepf(cfg->size_y, d * mult, 0.01f, 0.5f, 1.5f); return VX_MENU_CHANGED;
    case L_POS_X:        cfg->pos_x = clampi(cfg->pos_x + d * mult * 16, -1024, 1024); return VX_MENU_CHANGED;
    case L_POS_Y:        cfg->pos_y = clampi(cfg->pos_y + d * mult * 16, -1024, 1024); return VX_MENU_CHANGED;
    case L_FLIP_X:       cfg->flip_x = !cfg->flip_x; return VX_MENU_CHANGED;
    case L_FLIP_Y:       cfg->flip_y = !cfg->flip_y; return VX_MENU_CHANGED;
    case L_BRIGHT:       cfg->brightness = stepf(cfg->brightness, d, 0.05f, 0.3f, 2.0f); return VX_MENU_CHANGED;
    case L_GAMMA:        cfg->gamma = stepf(cfg->gamma, d, 0.05f, 0.3f, 2.0f); return VX_MENU_CHANGED;
    case L_COLOR: {
        int i = color_index(cfg);
        i = i < 0 ? 0 : (i + d + NCOLORS) % NCOLORS;
        cfg->color_r = COLORS[i].r; cfg->color_g = COLORS[i].g; cfg->color_b = COLORS[i].b;
        return VX_MENU_CHANGED;
    }
    case L_BOARD_HZ:     cfg->board_hz = stepf(cfg->board_hz, d, 5.0f, 30.0f, 60.0f); return VX_MENU_CHANGED;
    case L_BIOS:         cfg->skip_bios = !cfg->skip_bios; return VX_MENU_CHANGED;
    case L_UVMC2:        cfg->uvmc2_fast = !cfg->uvmc2_fast; return VX_MENU_CHANGED;
    case L_VOLUME:       cfg->volume = stepf(cfg->volume, d, 0.1f, 0.0f, 1.0f); return VX_MENU_CHANGED;
    }
    return VX_MENU_NOTHING;
}

/* --- saving ---------------------------------------------------------------------- */

void vx_menu_save(vx_menu *u, const vx_config *cfg, vx_config *all, vp_input *in)
{
    char dir[900], path[1100];
    snprintf(dir, sizeof dir, "%s", vp_save_dir());
    vx_mkdir(dir);
    snprintf(path, sizeof path, "%s/games", dir); vx_mkdir(path);
    snprintf(path, sizeof path, "%s/controls", dir); vx_mkdir(path);

    if (u->scope == VX_SCOPE_ALL) {         /* what was changed now: every game's */
        vx_config_take_changed(all, &u->at_open, cfg);
        snprintf(path, sizeof path, "%s/vectrex.ini", dir);
        vx_config_save(all, path);
    }
    snprintf(path, sizeof path, "%s/games/%s.ini", dir, u->game);
    vx_config_save_diff(cfg, all, path);    /* (only what differs from every game's; none: no file) */

    if (u->controls_changed) {
        char own[1024];
        vx_controls_path(u->game, own, sizeof own);
        if (u->controls_changed == 2) remove(own);
        else if (u->scope == VX_SCOPE_ALL) { vp_input_save(in, vx_controls_path(NULL, path, sizeof path)); remove(own); }
        else vp_input_save(in, own);
    }
    u->at_open = *cfg;
    u->controls_changed = 0;
}

/* --- drawing -------------------------------------------------------------------- */

static void upper(char *dst, size_t size, const char *s)
{
    size_t k = 0;
    for (; *s && k + 1 < size; s++) dst[k++] = (char)toupper((unsigned char)*s);
    dst[k] = '\0';
}

static void describe(int line, const vx_menu *u, const vx_config *cfg, vp_input *in, const char **label,
                     char *value, size_t size)
{
    value[0] = '\0';
    if (line >= L_FN0) {
        int fn = line - L_FN0;
        *label = FN_LABEL[fn % 8];
        if (u->cap.capturing && !strcmp(u->cap.action, VX_FN_NAME[fn])) {
            snprintf(value, size, "PRESS THE CONTROL");
        } else {
            char buf[256];
            vp_input_spelled(in, VX_FN_NAME[fn], vp_input_controls(in, VX_FN_NAME[fn]), buf, sizeof buf);
            upper(value, size, buf);
            if (strlen(value) > 44) strcpy(value + 41, "...");
        }
        return;
    }
    switch (line) {
    case L_PAGE_PAD1:    *label = "CONTROLS  PLAYER 1"; break;
    case L_PAGE_PAD2:    *label = "CONTROLS  PLAYER 2"; break;
    case L_PAGE_STICK:   *label = "STICK"; snprintf(value, size, "DIGITAL AS ANALOG"); break;
    case L_PAGE_PICTURE: *label = "PICTURE"; break;
    case L_PAGE_MACHINE: *label = "MACHINE"; break;
    case L_PAGE_SOUND:   *label = "SOUND"; break;
    case L_SCOPE:        *label = "SAVE FOR"; snprintf(value, size, "%s", u->scope == VX_SCOPE_ALL ? "ALL GAMES" : "THIS GAME"); break;
    case L_GAME_RESET:   *label = "THIS GAME AS ALL GAMES"; snprintf(value, size, "%s", u->confirm ? "START AGAIN TO DO IT" : "ITS OWN SETTINGS GO"); break;
    case L_RESTART:      *label = "RESTART THE GAME"; snprintf(value, size, "POWER ON"); break;
    case L_EXIT:         *label = "SAVE AND BACK TO THE GAME"; break;
    case L_QUIT:         *label = "SAVE AND LEAVE THE GAME"; break;
    case L_BACK:         *label = "BACK"; break;
    case L_DEFAULTS1:
    case L_DEFAULTS2:    *label = "ALL BACK TO STANDARD"; break;
    case L_ATTACK:       *label = "RAMP OUT"; snprintf(value, size, cfg->dig_attack < 0.005f ? "AT ONCE" : "%.2f S", cfg->dig_attack); break;
    case L_RELEASE:      *label = "RAMP BACK"; snprintf(value, size, cfg->dig_release < 0.005f ? "AT ONCE" : "%.2f S", cfg->dig_release); break;
    case L_CURVE:
        *label = "CURVE";
        snprintf(value, size, "%.2f  %s", cfg->dig_curve,
                 fabsf(cfg->dig_curve - 1.0f) < 0.01f ? "STRAIGHT" : cfg->dig_curve > 1.0f ? "SLOW AT FIRST" : "FAST AT FIRST");
        break;
    case L_MAX:          *label = "HOW FAR"; snprintf(value, size, "%d %%", (int)lroundf(cfg->dig_max * 100.0f)); break;
    case L_DEADZONE:     *label = "ANALOG STICK DEAD ZONE"; snprintf(value, size, "%d %%", (int)lroundf(cfg->deadzone * 100.0f)); break;
    case L_TEST:
        *label = "TEST PLAYER 1'S STICK";
        if (u->testing) snprintf(value, size, "%+4d %+4d  ESC ENDS", u->test_pad.x, u->test_pad.y);
        break;
    case L_MONITOR:      *label = "TUBE"; snprintf(value, size, "%s", cfg->monitor == VX_MONITOR_VERTICAL ? "STANDING 3:4" : "LYING 4:3"); break;
    case L_SIZE:         *label = "SIZE"; snprintf(value, size, "%d", (int)lroundf(cfg->size * 100.0f)); break;
    case L_WIDTH:        *label = "WIDTH"; snprintf(value, size, "%d", (int)lroundf(cfg->size_x * 100.0f)); break;
    case L_HEIGHT:       *label = "HEIGHT"; snprintf(value, size, "%d", (int)lroundf(cfg->size_y * 100.0f)); break;
    case L_POS_X:        *label = "POSITION LEFT RIGHT"; snprintf(value, size, "%d", cfg->pos_x); break;
    case L_POS_Y:        *label = "POSITION DOWN UP"; snprintf(value, size, "%d", cfg->pos_y); break;
    case L_FLIP_X:       *label = "MIRROR LEFT RIGHT"; snprintf(value, size, "%s", cfg->flip_x ? "ON" : "OFF"); break;
    case L_FLIP_Y:       *label = "MIRROR UP DOWN"; snprintf(value, size, "%s", cfg->flip_y ? "ON" : "OFF"); break;
    case L_BRIGHT:       *label = "BRIGHTNESS"; snprintf(value, size, "%d", (int)lroundf(cfg->brightness * 100.0f)); break;
    case L_GAMMA:        *label = "GAMMA"; snprintf(value, size, "%.2f", cfg->gamma); break;
    case L_COLOR: {
        int i = color_index(cfg);
        *label = "COLOUR";
        if (i >= 0) snprintf(value, size, "%s", COLORS[i].name);
        else snprintf(value, size, "%d %d %d", cfg->color_r, cfg->color_g, cfg->color_b);
        break;
    }
    case L_BOARD_HZ:     *label = "BOARD RATE"; snprintf(value, size, "%d PICTURES A SECOND", (int)lroundf(cfg->board_hz)); break;
    case L_BIOS:         *label = "BIOS INTRO"; snprintf(value, size, "%s", cfg->skip_bios ? "SKIPPED" : "SHOWN"); break;
    case L_UVMC2:        *label = "UVMC2 CARTRIDGE"; snprintf(value, size, "%s", cfg->uvmc2_fast ? "FAST" : "EXACT  SLOW ON A PI"); break;
    case L_NOTE_NEXT:    *label = u->uvmc2 && cfg->uvmc2_fast != u->at_open.uvmc2_fast ? "THE UVMC2 MODE APPLIES WHEN THE GAME IS STARTED AGAIN"
                                                                                       : "THE INTRO SETTING APPLIES ON THE NEXT POWER ON"; break;
    case L_VOLUME:       *label = "VOLUME"; snprintf(value, size, "%d", (int)lroundf(cfg->volume * 10.0f)); break;
    default:             *label = "?"; break;
    }
}

void vx_menu_draw(vx_menu *u, const vx_config *cfg, vp_input *in, vp_dvg *d)
{
    int page = u->page >= 0 && u->page < N_PAGES ? u->page : P_MAIN;
    float STEP = 170.0f, GAP = 90.0f, H = 78.0f;
    const float LEFT = 520.0f, RIGHT = 3600.0f, ROOM = 3000.0f;
    uint8_t lines[PAGE_MAX + 1];
    page_lines(u, page, lines);

    char title[160];
    if (page == P_MAIN) upper(title, sizeof title, u->title);
    else snprintf(title, sizeof title, "%s", TITLE[page]);
    vp_text_font(VP_FONT_ASTEROIDS);
    vp_color(d, 255, 220, 60);
    {
        float h = 95.0f, w = vp_text_width(title, h);
        if (w > 3400.0f) h *= 3400.0f / w;
        vp_text(d, title, 2048, 3740, h, VP_TEXT_CENTER);
    }

    float height = 0.0f;
    for (int i = 0; lines[i] != L_END; i++) height += !lines[i] ? GAP : STEP;
    if (height > ROOM) {
        float s = ROOM / height;
        STEP *= s; GAP *= s; H *= s; height = ROOM;
    }
    float y = 2048.0f + height * 0.5f - STEP * 0.5f + 60.0f;
    int k = 0, chosen_id = L_GAP;
    for (int i = 0; lines[i] != L_END; i++) {
        int line = lines[i];
        if (!line) { y -= GAP; continue; }
        const char *label;
        char value[300];
        describe(line, u, cfg, in, &label, value, sizeof value);
        if (line == L_NOTE_NEXT) {
            vp_color(d, 110, 130, 255);
            vp_text(d, label, LEFT, y, H * 0.62f, VP_TEXT_LEFT);
            y -= STEP;
            continue;
        }
        int chosen = k == u->item[page];
        if (chosen) {
            chosen_id = line;
            vp_color(d, 255, 255, 255);
            float a = H / 78.0f;
            vp_moveto(d, (int)LEFT - 160, (int)(y + 70.0f * a));
            vp_lineto(d, (int)LEFT - 70, (int)(y + 38.0f * a));
            vp_lineto(d, (int)LEFT - 160, (int)(y + 6.0f * a));
        } else {
            vp_color(d, 60, 200, 90);
        }
        vp_text(d, label, LEFT, y, H, VP_TEXT_LEFT);
        if (value[0]) {
            float room = RIGHT - LEFT - vp_text_width(label, H) - 150.0f, w = vp_text_width(value, H), h = H;
            if (w > room && w > 0.0f) h = H * room / w;
            if (chosen) vp_color(d, 255, 255, 255);
            else vp_color(d, 80, 220, 255);
            if ((u->cap.capturing || u->testing) && chosen) vp_color(d, 255, 60, 50);
            vp_text(d, value, RIGHT, y, h, VP_TEXT_RIGHT);
        }
        y -= STEP;
        k++;
    }

    if (page == P_STICK) {                  /* the stick test: where player 1's stick is */
        const int cx = 3350, cy = 900, r = 230;
        vp_color(d, u->testing ? 150 : 60, u->testing ? 160 : 70, u->testing ? 255 : 120);
        vp_moveto(d, cx - r, cy - r); vp_lineto(d, cx + r, cy - r); vp_lineto(d, cx + r, cy + r);
        vp_lineto(d, cx - r, cy + r); vp_lineto(d, cx - r, cy - r);
        if (u->testing) {
            int px = cx + u->test_pad.x * r / 128, py = cy + u->test_pad.y * r / 128;
            vp_color(d, 255, 220, 60);
            vp_moveto(d, px - 30, py); vp_lineto(d, px + 30, py);
            vp_moveto(d, px, py - 30); vp_lineto(d, px, py + 30);
        }
    }

    const char *said = vp_capture_said(&u->cap);
    if (said) {
        vp_color(d, 255, 120, 60);
        vp_text(d, said, 2048, 430, 55, VP_TEXT_CENTER);
    }
    vp_color(d, 110, 130, 255);
    const char *one = "UP DOWN CHOOSE   LEFT RIGHT CHANGE", *two = "START 1 OR ENTER DO   ESC BACK   F2 OR TAB SAVE";
    if (u->testing) {
        one = "MOVE PLAYER 1'S STICK OR ITS DIRECTIONS";
        two = "ESC OR ENTER ENDS THE TEST";
    } else if (u->cap.capturing || chosen_id >= L_FN0) {
        const char *a = NULL, *b = NULL;
        vp_capture_help(&u->cap, &a, &b);
        if (a) one = a;
        if (b) two = b;
    }
    vp_text(d, one, 2048, 300, 50, VP_TEXT_CENTER);
    vp_text(d, two, 2048, 200, 50, VP_TEXT_CENTER);
}
