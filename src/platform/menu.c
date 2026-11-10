/* menu.c - see menu.h (built as Major Havoc PE Enhanced's platform/menu.c). */
#include "platform/menu.h"
#include "platform/coloredit.h"
#include "platform/controls.h"

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
    L_DEFAULTS1, L_DEFAULTS2, L_PAGE_MENUCTL, L_DEFAULTS3,
    L_ATTACK, L_RELEASE, L_CURVE, L_MAX, L_DEADZONE, L_TEST,
    L_MONITOR, L_SIZE, L_WIDTH, L_HEIGHT, L_POS_X, L_POS_Y, L_FLIP_X, L_FLIP_Y, L_BRIGHT, L_GAMMA,
    L_COLOR, L_BOARD_HZ,
    L_BIOS, L_UVMC2, L_NOTE_NEXT,
    L_VOLUME, L_SPEECH_VOL,
    L_PAGE_PORT2, L_P2_DEV, L_SPIN_SENS, L_SPIN_STEPS, L_SPIN_REV, L_TB_SENS, L_TB_STEPS, L_TB_REVX,
    L_TB_REVY, L_P2_NOTE, L_PROFILE, L_HUM,
    L_CMODE, L_BPRESET, L_BAND1, L_BAND2, L_BAND3, L_BAND4,
    L_FN0,                      /* + the function (controls.h VX_FN_NAME) */
    L_END = 0xff
};

enum { P_MAIN, P_PAD1, P_PAD2, P_STICK, P_PICTURE, P_MACHINE, P_SOUND, P_PORT2, P_MENUCTL, N_PAGES };
static const char *const TITLE[N_PAGES] = {
    "", "CONTROLS  PLAYER 1", "CONTROLS  PLAYER 2", "STICK", "PICTURE", "MACHINE", "SOUND", "PORT 2", "CONTROLS  MENUS",
};
static const char *const MENU_FN_LABEL[VX_FN_COUNT - VX_FN_MENU] = {
    "UP", "DOWN", "LEFT  LETTER BACK", "RIGHT  LETTER ON", "SELECT  START", "BACK", "OPTIONS",
};
static const char *const FN_LABEL[8] = {
    "STICK UP", "STICK DOWN", "STICK LEFT", "STICK RIGHT", "BUTTON 1", "BUTTON 2", "BUTTON 3", "BUTTON 4",
};

typedef struct { const char *name; int r, g, b; } color_preset;
static const color_preset COLORS[] = { VX_PALETTE_ENTRIES };   /* (platform/coloredit.h) */

/* by brightness: whole sets of the four bands' colours (dim to full) */
typedef struct { const char *name; int band[4]; } band_preset;
static const band_preset BAND_PRESETS[] = {
    { "ICE", { 0x2050FF, 0x00C8FF, 0xA0F0FF, 0xFFFFFF } },
    { "FIRE", { 0xC02000, 0xFF6000, 0xFFC020, 0xFFFFA0 } },
    { "NEON", { 0xFF30D0, 0x00E0FF, 0xFFF040, 0xFFFFFF } },
    { "RAINBOW", { 0x4060FF, 0x30FF60, 0xFFE030, 0xFF4040 } },
    { "MATRIX", { 0x105020, 0x20A040, 0x40FF70, 0xC0FFC0 } },
    { "AMBER", { 0x603000, 0xB06000, 0xFFA020, 0xFFE0A0 } },
    { "WHITE", { 0x808080, 0xB0B0B0, 0xE0E0E0, 0xFFFFFF } },
};
#define NBPRESETS (int)(sizeof BAND_PRESETS / sizeof BAND_PRESETS[0])

static int rgb_of(int i) { return COLORS[i].r << 16 | COLORS[i].g << 8 | COLORS[i].b; }
static int rgb_index(int rgb)
{
    for (int i = 0; i < (int)(sizeof COLORS / sizeof COLORS[0]); i++) if (rgb_of(i) == (rgb & 0xffffff)) return i;
    return -1;
}
static int band_preset_index(const vx_config *c)
{
    for (int i = 0; i < NBPRESETS; i++)
        if (BAND_PRESETS[i].band[0] == c->band1 && BAND_PRESETS[i].band[1] == c->band2 &&
            BAND_PRESETS[i].band[2] == c->band3 && BAND_PRESETS[i].band[3] == c->band4) return i;
    return -1;
}
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
    case P_MENUCTL:                     /* (the menus: as the VectorPie menu, for every game) */
        for (int f = VX_FN_MENU; f < VX_FN_COUNT; f++) ADD(L_FN0 + f);
        ADD(L_GAP); ADD(L_DEFAULTS3); ADD(L_BACK);
        break;
    case P_STICK:
        ADD(L_ATTACK); ADD(L_RELEASE); ADD(L_CURVE); ADD(L_MAX); ADD(L_GAP); ADD(L_DEADZONE); ADD(L_GAP);
        ADD(L_TEST); ADD(L_BACK);
        break;
    case P_PICTURE:
        ADD(L_MONITOR); ADD(L_SIZE); ADD(L_WIDTH); ADD(L_HEIGHT); ADD(L_POS_X); ADD(L_POS_Y);
        ADD(L_FLIP_X); ADD(L_FLIP_Y); ADD(L_GAP); ADD(L_BRIGHT); ADD(L_GAMMA); ADD(L_BOARD_HZ); ADD(L_GAP);
        ADD(L_PROFILE); ADD(L_CMODE);
        if (u->cmode_shown == VX_COLOR_BRIGHTNESS) { ADD(L_BPRESET); ADD(L_BAND1); ADD(L_BAND2); ADD(L_BAND3); ADD(L_BAND4); }
        else ADD(L_COLOR);
        ADD(L_GAP); ADD(L_BACK);
        break;
    case P_MACHINE:
        ADD(L_BIOS); if (u->uvmc2) ADD(L_UVMC2); ADD(L_HUM);
        if (!u->in_list) { ADD(L_NOTE_NEXT); ADD(L_GAP); ADD(L_RESTART); } else ADD(L_GAP);
        ADD(L_BACK);
        break;
    case P_SOUND:
        ADD(L_VOLUME); ADD(L_SPEECH_VOL); ADD(L_GAP); ADD(L_BACK);
        break;
    case P_PORT2:                       /* the device, then its own settings */
        ADD(L_P2_DEV); ADD(L_GAP);
        if (u->p2_shown == VX_P2_SPINNER) { ADD(L_SPIN_SENS); ADD(L_SPIN_STEPS); ADD(L_SPIN_REV); }
        else if (u->p2_shown == VX_P2_TRACKBALL) { ADD(L_TB_SENS); ADD(L_TB_STEPS); ADD(L_TB_REVX); ADD(L_TB_REVY); }
        else {
            if (u->p2_shown == VX_P2_VECVOX || u->p2_shown == VX_P2_ATARIVOX || u->p2_shown == VX_P2_VECVOICE) ADD(L_SPEECH_VOL);
            ADD(L_P2_NOTE);
        }
        ADD(L_GAP); ADD(L_BACK);
        break;
    default:
        ADD(L_SCOPE); ADD(L_GAP);
        ADD(L_PAGE_PAD1); ADD(L_PAGE_PAD2); ADD(L_PAGE_MENUCTL); ADD(L_PAGE_PORT2); ADD(L_PAGE_STICK); ADD(L_PAGE_PICTURE);
        ADD(L_PAGE_MACHINE); ADD(L_PAGE_SOUND); ADD(L_GAP); ADD(L_GAME_RESET); ADD(L_GAP);
        if (u->in_list) ADD(L_EXIT);
        else { ADD(L_RESTART); ADD(L_EXIT); ADD(L_QUIT); }
        break;
    }
#undef ADD
    out[n] = L_END;
    return n;
}

static int selectable(int line) { return line != L_GAP && line != L_NOTE_NEXT && line != L_P2_NOTE; }

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

/* The settings a line shows and changes (vx_config's keys), -1 ending. */
static const int *line_fields(int line)
{
    static int f[4];
    const char *k[3] = { NULL, NULL, NULL };
    switch (line) {
    case L_ATTACK:   k[0] = "dig_attack"; break;
    case L_RELEASE:  k[0] = "dig_release"; break;
    case L_CURVE:    k[0] = "dig_curve"; break;
    case L_MAX:      k[0] = "dig_max"; break;
    case L_DEADZONE: k[0] = "deadzone"; break;
    case L_MONITOR:  k[0] = "monitor"; break;
    case L_SIZE:     k[0] = "size"; break;
    case L_WIDTH:    k[0] = "size_x"; break;
    case L_HEIGHT:   k[0] = "size_y"; break;
    case L_POS_X:    k[0] = "pos_x"; break;
    case L_POS_Y:    k[0] = "pos_y"; break;
    case L_FLIP_X:   k[0] = "flip_x"; break;
    case L_FLIP_Y:   k[0] = "flip_y"; break;
    case L_BRIGHT:   k[0] = "brightness"; break;
    case L_GAMMA:    k[0] = "gamma"; break;
    case L_COLOR:    k[0] = "color_r"; k[1] = "color_g"; k[2] = "color_b"; break;
    case L_BOARD_HZ: k[0] = "board_hz"; break;
    case L_BIOS:     k[0] = "skip_bios"; break;
    case L_UVMC2:    k[0] = "uvmc2_fast"; break;
    case L_VOLUME:   k[0] = "volume"; break;
    case L_SPEECH_VOL: k[0] = "speech_volume"; break;
    case L_P2_DEV:   k[0] = "port2"; break;
    case L_SPIN_SENS: k[0] = "spin_sens"; break;
    case L_SPIN_STEPS: k[0] = "spin_steps"; break;
    case L_SPIN_REV: k[0] = "spin_reverse"; break;
    case L_TB_SENS:  k[0] = "tb_sens"; break;
    case L_TB_STEPS: k[0] = "tb_steps"; break;
    case L_TB_REVX:  k[0] = "tb_reverse_x"; break;
    case L_TB_REVY:  k[0] = "tb_reverse_y"; break;
    case L_PROFILE:  k[0] = "color_profile"; break;
    case L_HUM:      k[0] = "mains_hum"; break;
    case L_CMODE:    k[0] = "color_mode"; break;
    case L_BAND1:    k[0] = "band1"; break;
    case L_BAND2:    k[0] = "band2"; break;
    case L_BAND3:    k[0] = "band3"; break;
    case L_BAND4:    k[0] = "band4"; break;
    case L_BPRESET:  k[0] = "band1"; k[1] = "band2"; k[2] = "band3"; break;   /* (band4 below) */
    }
    int n = 0;
    for (int i = 0; i < 3; i++)
        if (k[i]) f[n++] = vx_config_field(k[i]);
    if (line == L_BPRESET) f[n++] = vx_config_field("band4");
    f[n] = -1;
    return f;
}

static int line_own(const vx_menu *u, int line)
{
    for (const int *f = line_fields(line); *f >= 0; f++)
        if (u->own[*f]) return 1;
    return 0;
}

/* cfg: every game's settings, with the game's own on top */
static void sync(const vx_menu *u, vx_config *cfg, const vx_config *all)
{
    for (int i = 0; i < vx_config_nfields() && i < VX_CONFIG_MAX_FIELDS; i++)
        if (!u->own[i]) vx_config_copy_field(cfg, all, i);
}

void vx_menu_open(vx_menu *u, const vx_config *cfg, const vx_config *all, const char *game_id,
                  const char *game_name, int uvmc2, int in_list)
{
    memset(u, 0, sizeof *u);
    u->scope = in_list ? VX_SCOPE_ALL : VX_SCOPE_GAME;
    u->open = 1;
    u->page = P_MAIN;
    u->at_open = *cfg;
    u->uvmc2 = uvmc2;
    u->in_list = in_list;
    for (int i = 0; i < vx_config_nfields() && i < VX_CONFIG_MAX_FIELDS; i++)
        u->own[i] = (unsigned char)vx_config_differs(cfg, all, i);
    char path[1024];
    vx_fn_table_read(&u->fn_all, vx_controls_path(NULL, path, sizeof path));
    vx_fn_table_read(&u->fn_game, vx_controls_path(game_id, path, sizeof path));
    snprintf(u->game, sizeof u->game, "%s", game_id);
    snprintf(u->title, sizeof u->title, "%s", game_name);
}

/* The game has its own controls for function fn (in its file, or laid out now for THIS GAME). */
static int fn_own(const vx_menu *u, int fn)
{
    if (u->fn_changed[fn]) return u->fn_changed[fn] == VX_SCOPE_GAME + 1;
    return !u->game_reset && u->fn_game.v[fn][0] != '\0';
}

/* the SP0256-AL2 ROM the speech modules need (the package's system folder) */
static int speech_rom_there(void)
{
    char p[1024];
    FILE *f = fopen(vp_asset_path("system/sp0256-al2.bin", p, sizeof p), "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* --- the menu's own controls -------------------------------------------------- */
typedef struct { int x, y, px, py, fire, back, del, close; } nav_t;

static nav_t read_nav(vp_input *in)
{
    nav_t n;
    vx_nav v = vx_menu_nav(in);                 /* (the VectorPie menu's controls, controls.h) */
    n.x = v.x; n.y = v.y; n.px = v.px; n.py = v.py;
    n.fire = v.fire; n.back = v.back; n.close = v.close;
    n.del = vp_input_key_pressed(in, SDL_SCANCODE_DELETE) || vp_input_key_pressed(in, SDL_SCANCODE_KP_PERIOD);
    return n;
}

/* A function laid out now: for the level chosen. */
static void fn_mark(vx_menu *u, int fn)
{
    u->fn_changed[fn] = (unsigned char)((fn >= VX_FN_MENU ? VX_SCOPE_ALL : u->scope) + 1);   /* (the menus: every game's) */
}

int vx_menu_update(vx_menu *u, vx_config *cfg, vx_config *all, vp_input *in, const vx_pad *p1, double now)
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
        if (vp_capture_step(&u->cap, in) == VP_CAPTURE_CHANGED) {
            for (int f = 0; f < VX_FN_COUNT; f++)
                if (!strcmp(u->cap.action, VX_FN_NAME[f])) fn_mark(u, f);
        }
        u->dir_x = nav.x;
        u->dir_y = nav.y;
        u->repeat_at = now + 0.6;
        return VX_MENU_NOTHING;
    }
    if (nav.close) return VX_MENU_CLOSE;

    uint8_t lines[PAGE_MAX + 1];
    int n;
    u->p2_shown = (u->scope == VX_SCOPE_ALL ? all : cfg)->port2;
    u->cmode_shown = (u->scope == VX_SCOPE_ALL ? all : cfg)->color_mode;
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
    int fine = u->rep_n < 10 ? 1 : u->rep_n < 30 ? 5 : 25;   /* (the picture: fine steps, quick when held) */
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
        int fn = line - L_FN0;
        const char *name = VX_FN_NAME[fn];
        int left = dx < 0, right = dx > 0;
        vp_capture_filter(&u->cap, &left, &right);
        if (fire) vp_capture_begin(&u->cap, in, name);
        else if (nav.del) { vp_capture_standard(&u->cap, in, name); fn_mark(u, fn); }
        else if (left || right) { vp_capture_toggle(&u->cap, in, name); fn_mark(u, fn); }
        return VX_MENU_NOTHING;
    }

    /* Delete on a setting with THIS GAME: the game's own value goes, the all games value applies */
    if (nav.del && u->scope == VX_SCOPE_GAME && line_own(u, line)) {
        for (const int *f = line_fields(line); *f >= 0; f++) u->own[*f] = 0;
        sync(u, cfg, all);
        return VX_MENU_CHANGED;
    }

    int d = dx ? dx : (fire ? 1 : 0);
    if (!d) return VX_MENU_NOTHING;
    vx_config *t = u->scope == VX_SCOPE_ALL ? all : cfg;     /* the level changed */
    switch (line) {
    case L_PAGE_PAD1:    if (fire) { u->page = P_PAD1; vp_capture_moved(&u->cap); } return VX_MENU_NOTHING;
    case L_PAGE_PAD2:    if (fire) { u->page = P_PAD2; vp_capture_moved(&u->cap); } return VX_MENU_NOTHING;
    case L_PAGE_MENUCTL: if (fire) { u->page = P_MENUCTL; vp_capture_moved(&u->cap); } return VX_MENU_NOTHING;
    case L_DEFAULTS3:
        if (!fire) return VX_MENU_NOTHING;
        for (int fn = VX_FN_MENU; fn < VX_FN_COUNT; fn++) { vp_input_set(in, VX_FN_NAME[fn], "standard"); fn_mark(u, fn); }
        return VX_MENU_NOTHING;
    case L_PAGE_STICK:   if (fire) u->page = P_STICK; return VX_MENU_NOTHING;
    case L_PAGE_PICTURE: if (fire) u->page = P_PICTURE; return VX_MENU_NOTHING;
    case L_PAGE_MACHINE: if (fire) u->page = P_MACHINE; return VX_MENU_NOTHING;
    case L_PAGE_SOUND:   if (fire) u->page = P_SOUND; return VX_MENU_NOTHING;
    case L_PAGE_PORT2:   if (fire) u->page = P_PORT2; return VX_MENU_NOTHING;
    case L_BACK:         if (fire) u->page = P_MAIN; return VX_MENU_NOTHING;
    case L_EXIT:         return fire ? VX_MENU_CLOSE : VX_MENU_NOTHING;
    case L_QUIT:         return fire ? VX_MENU_QUIT : VX_MENU_NOTHING;
    case L_RESTART:      return fire ? VX_MENU_RESTART : VX_MENU_NOTHING;
    case L_SCOPE:        u->scope = !u->scope; return VX_MENU_NOTHING;
    case L_GAME_RESET:
        if (!fire) return VX_MENU_NOTHING;
        if (!u->confirm) { u->confirm = 1; return VX_MENU_NOTHING; }
        u->confirm = 0;
        memset(u->own, 0, sizeof u->own);   /* every game's settings, and every game's controls */
        sync(u, cfg, all);
        for (int f = 0; f < VX_FN_COUNT; f++)
            vp_input_set(in, VX_FN_NAME[f], u->fn_all.v[f][0] ? u->fn_all.v[f] : "standard");
        memset(u->fn_changed, 0, sizeof u->fn_changed);
        u->game_reset = 1;
        return VX_MENU_CHANGED;
    case L_DEFAULTS1:
    case L_DEFAULTS2:
        if (!fire) return VX_MENU_NOTHING;
        for (int f = 0; f < 8; f++) {
            int fn = (line == L_DEFAULTS2 ? 8 : 0) + f;
            vp_input_set(in, VX_FN_NAME[fn], "standard");
            fn_mark(u, fn);
        }
        return VX_MENU_NOTHING;
    case L_TEST:         if (fire) { u->testing = 1; u->test_pad = *p1; } return VX_MENU_NOTHING;
    case L_ATTACK:       t->dig_attack = stepf(t->dig_attack, d * mult, 0.02f, 0.0f, 1.0f); break;
    case L_RELEASE:      t->dig_release = stepf(t->dig_release, d * mult, 0.02f, 0.0f, 1.0f); break;
    case L_CURVE:        t->dig_curve = stepf(t->dig_curve, d * mult, 0.05f, 0.25f, 4.0f); break;
    case L_MAX:          t->dig_max = stepf(t->dig_max, d, 0.05f, 0.1f, 1.0f); break;
    case L_DEADZONE:     t->deadzone = stepf(t->deadzone, d, 0.01f, 0.0f, 0.5f); break;
    case L_MONITOR:      t->monitor = !t->monitor; break;
    case L_SIZE:         t->size = stepf(t->size, d * fine, 0.002f, 0.5f, 1.5f); break;
    case L_WIDTH:        t->size_x = stepf(t->size_x, d * fine, 0.002f, 0.5f, 1.5f); break;
    case L_HEIGHT:       t->size_y = stepf(t->size_y, d * fine, 0.002f, 0.5f, 1.5f); break;
    case L_POS_X:        t->pos_x = clampi(t->pos_x + d * fine, -1024, 1024); break;
    case L_POS_Y:        t->pos_y = clampi(t->pos_y + d * fine, -1024, 1024); break;
    case L_FLIP_X:       t->flip_x = !t->flip_x; break;
    case L_FLIP_Y:       t->flip_y = !t->flip_y; break;
    case L_BRIGHT:       t->brightness = stepf(t->brightness, d, 0.05f, 0.3f, 2.0f); break;
    case L_GAMMA:        t->gamma = stepf(t->gamma, d, 0.05f, 0.3f, 2.0f); break;
    case L_COLOR: {
        int i = color_index(t);
        i = i < 0 ? 0 : (i + d + NCOLORS) % NCOLORS;
        t->color_r = COLORS[i].r; t->color_g = COLORS[i].g; t->color_b = COLORS[i].b;
        break;
    }
    case L_BOARD_HZ:     t->board_hz = stepf(t->board_hz, d, 5.0f, 30.0f, 60.0f); break;
    case L_BIOS:         t->skip_bios = !t->skip_bios; break;
    case L_UVMC2:        t->uvmc2_fast = !t->uvmc2_fast; break;
    case L_VOLUME:       t->volume = stepf(t->volume, d, 0.1f, 0.0f, 1.0f); break;
    case L_SPEECH_VOL:   t->speech_volume = stepf(t->speech_volume, d, 0.1f, 0.0f, 5.0f); break;
    case L_P2_DEV: {                     /* (joystick, spinner, trackball, AtariVox: the VecVox and VecVoice of
                                            earlier versions are the AtariVox now, which is either) */
        int p = t->port2 >= VX_P2_ATARIVOX ? VX_P2_ATARIVOX : t->port2;
        t->port2 = (p + d + VX_P2_ATARIVOX + 1) % (VX_P2_ATARIVOX + 1);
        break;
    }
    case L_SPIN_SENS:    t->spin_sens = stepf(t->spin_sens, d * mult, 0.01f, 0.01f, 4.0f); break;
    case L_SPIN_STEPS:   t->spin_steps = clampi(t->spin_steps + d, 1, 30); break;
    case L_SPIN_REV:     t->spin_reverse = !t->spin_reverse; break;
    case L_TB_SENS:      t->tb_sens = stepf(t->tb_sens, d * mult, 0.01f, 0.01f, 4.0f); break;
    case L_TB_STEPS:     t->tb_steps = clampi(t->tb_steps + d, 1, 60); break;
    case L_TB_REVX:      t->tb_reverse_x = !t->tb_reverse_x; break;
    case L_TB_REVY:      t->tb_reverse_y = !t->tb_reverse_y; break;
    case L_PROFILE:                     /* OFF, ORIGINAL, PROFILE 1, 2 ... (those the game has); for all
                                           games only OFF / ORIGINAL */
        t->color_profile = u->scope == VX_SCOPE_ALL ? !t->color_profile : vx_colors_setting_step(u->game, t->color_profile, d);
        break;
    case L_HUM:          t->mains_hum = !t->mains_hum; break;
    case L_CMODE:        t->color_mode = !t->color_mode; break;
    case L_BPRESET: {
        int i = band_preset_index(t);
        i = i < 0 ? 0 : (i + d + NBPRESETS) % NBPRESETS;
        t->band1 = BAND_PRESETS[i].band[0]; t->band2 = BAND_PRESETS[i].band[1];
        t->band3 = BAND_PRESETS[i].band[2]; t->band4 = BAND_PRESETS[i].band[3];
        break;
    }
    case L_BAND1: case L_BAND2: case L_BAND3: case L_BAND4: {
        int *b = line == L_BAND1 ? &t->band1 : line == L_BAND2 ? &t->band2 : line == L_BAND3 ? &t->band3 : &t->band4;
        int nc = (int)(sizeof COLORS / sizeof COLORS[0]), i = rgb_index(*b);
        i = i < 0 ? 0 : (i + d + nc) % nc;
        *b = rgb_of(i);
        break;
    }
    default:             return VX_MENU_NOTHING;
    }
    /* a setting changed: for THIS GAME it is the game's own now; for ALL GAMES every game's, and the
     * game runs with it unless it has its own */
    if (u->scope == VX_SCOPE_GAME) {
        for (const int *f = line_fields(line); *f >= 0; f++) u->own[*f] = 1;
    } else {
        u->all_changed = 1;
    }
    sync(u, cfg, all);
    return VX_MENU_CHANGED;
}

/* --- saving ---------------------------------------------------------------------- */

void vx_menu_save(vx_menu *u, const vx_config *cfg, const vx_config *all, vp_input *in)
{
    char dir[900], path[1100];
    snprintf(dir, sizeof dir, "%s", vp_save_dir());
    vx_mkdir(dir);
    snprintf(path, sizeof path, "%s/games", dir); vx_mkdir(path);
    snprintf(path, sizeof path, "%s/controls", dir); vx_mkdir(path);

    if (u->all_changed) {                   /* every game's settings */
        snprintf(path, sizeof path, "%s/vectrex.ini", dir);
        vx_config_save(all, path);
    }
    snprintf(path, sizeof path, "%s/games/%s.ini", dir, u->game);
    vx_config_save_diff(cfg, all, path);    /* the game's own: only what differs (none: no file) */

    /* the controls, function by function: laid out for ALL GAMES into every game's file (and out of
     * the game's own), for THIS GAME into the game's own unless it is what every game has */
    int any_all = 0, any = u->game_reset;
    vx_fn_table A = u->fn_all, G = u->fn_game;
    if (u->game_reset) memset(&G, 0, sizeof G);
    for (int f = 0; f < VX_FN_COUNT; f++) {
        if (!u->fn_changed[f]) continue;
        any = 1;
        const char *cur = vp_input_controls(in, VX_FN_NAME[f]);
        if (!cur) cur = "standard";
        if (u->fn_changed[f] == VX_SCOPE_ALL + 1) {
            snprintf(A.v[f], sizeof A.v[f], "%s", cur);
            G.v[f][0] = '\0';
            any_all = 1;
        } else {
            const char *every = A.v[f][0] ? A.v[f] : "standard";
            if (strcmp(cur, every)) snprintf(G.v[f], sizeof G.v[f], "%s", cur);
            else G.v[f][0] = '\0';
        }
    }
    if (any_all) vx_fn_table_write(&A, vx_controls_path(NULL, path, sizeof path));
    if (any) vx_fn_table_write(&G, vx_controls_path(u->game, path, sizeof path));

    u->fn_all = A;
    u->fn_game = G;
    memset(u->fn_changed, 0, sizeof u->fn_changed);
    u->game_reset = 0;
    u->all_changed = 0;
    u->at_open = *cfg;
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
        *label = fn >= VX_FN_MENU ? MENU_FN_LABEL[fn - VX_FN_MENU] : FN_LABEL[fn % 8];
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
    case L_PAGE_MENUCTL: *label = "CONTROLS  MENUS"; snprintf(value, size, "AS THE VECTORPIE MENU"); break;
    case L_DEFAULTS3:    *label = "ALL AS THE VECTORPIE MENU"; break;
    case L_PAGE_STICK:   *label = "STICK"; snprintf(value, size, "DIGITAL AS ANALOG"); break;
    case L_PAGE_PICTURE: *label = "PICTURE"; break;
    case L_PAGE_MACHINE: *label = "MACHINE"; break;
    case L_PAGE_SOUND:   *label = "SOUND"; break;
    case L_PAGE_PORT2:   *label = "PORT 2"; upper(value, size, vx_port2_name(cfg->port2)); break;
    case L_SCOPE:        *label = "SETTINGS FOR"; snprintf(value, size, "%s", u->scope == VX_SCOPE_ALL ? "ALL GAMES" : "THIS GAME"); break;
    case L_GAME_RESET:   *label = "THIS GAME AS ALL GAMES"; snprintf(value, size, "%s", u->confirm ? "START AGAIN TO DO IT" : "ITS OWN SETTINGS GO"); break;
    case L_RESTART:      *label = "RESTART THE GAME"; snprintf(value, size, "POWER ON"); break;
    case L_EXIT:         *label = u->in_list ? "SAVE AND BACK TO THE LIST" : "SAVE AND BACK TO THE GAME"; break;
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
    case L_SIZE:         *label = "SIZE"; snprintf(value, size, "%.1f", cfg->size * 100.0f); break;
    case L_WIDTH:        *label = "WIDTH"; snprintf(value, size, "%.1f", cfg->size_x * 100.0f); break;
    case L_HEIGHT:       *label = "HEIGHT"; snprintf(value, size, "%.1f", cfg->size_y * 100.0f); break;
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
    case L_SPEECH_VOL:   *label = "SPEECH VOLUME"; snprintf(value, size, "%d", (int)lroundf(cfg->speech_volume * 10.0f)); break;
    case L_P2_DEV:       *label = "PLUGGED IN"; upper(value, size, vx_port2_name(cfg->port2 >= VX_P2_ATARIVOX ? VX_P2_ATARIVOX : cfg->port2)); break;
    case L_SPIN_SENS:    *label = "SPINNER SPEED"; snprintf(value, size, "%.2f STEPS A COUNT", cfg->spin_sens); break;
    case L_SPIN_STEPS:   *label = "SPINNER STEPS A FRAME"; snprintf(value, size, "AT MOST %d", cfg->spin_steps); break;
    case L_SPIN_REV:     *label = "SPINNER DIRECTION"; snprintf(value, size, "%s", cfg->spin_reverse ? "REVERSED" : "NORMAL"); break;
    case L_TB_SENS:      *label = "TRACKBALL SPEED"; snprintf(value, size, "%.2f STEPS A COUNT", cfg->tb_sens); break;
    case L_TB_STEPS:     *label = "TRACKBALL STEPS A FRAME"; snprintf(value, size, "AT MOST %d", cfg->tb_steps); break;
    case L_TB_REVX:      *label = "TRACKBALL LEFT RIGHT"; snprintf(value, size, "%s", cfg->tb_reverse_x ? "REVERSED" : "NORMAL"); break;
    case L_TB_REVY:      *label = "TRACKBALL UP DOWN"; snprintf(value, size, "%s", cfg->tb_reverse_y ? "REVERSED" : "NORMAL"); break;
    case L_P2_NOTE:
        /* (a note, not a setting: what the device is for) */
        *label = cfg->port2 == VX_P2_JOYSTICK ? "NOTE  PLAYER 2'S CONTROLLER"
               : !speech_rom_there() ? "NOTE  SPEECH NEEDS SYSTEM/SP0256-AL2.BIN"
               : "NOTE  VECVOX OR VECVOICE AS THE GAME SPEAKS";
        break;
    case L_PROFILE: {
        *label = "COLOUR PROFILE";
        int s = u->scope == VX_SCOPE_ALL ? cfg->color_profile : vx_colors_setting_used(u->game, cfg->color_profile);
        if (u->scope == VX_SCOPE_ALL) snprintf(value, size, "%s", s ? "ORIGINAL" : "OFF");
        else if (s == 0) snprintf(value, size, "%s", cfg->color_profile ? "NONE YET" : "OFF");
        else if (s == 1) snprintf(value, size, "ORIGINAL");
        else snprintf(value, size, "PROFILE %d", s - 1);
        char p[1100];                       /* (made for another version of the game: its rules may miss) */
        if (u->scope != VX_SCOPE_ALL && s > 0 && vx_colors_profile_path(u->game, s - 1, p, sizeof p) == 0 && !vx_colors_file_fits(p)) {
            size_t k = strlen(value);
            snprintf(value + k, size - k, "  OTHER VERSION");
        }
        break;
    }
    case L_CMODE:        *label = "COLOUR MODE"; snprintf(value, size, "%s", cfg->color_mode == VX_COLOR_BRIGHTNESS ? "BY BRIGHTNESS" : "ONE COLOUR"); break;
    case L_BPRESET: {
        int i = band_preset_index(cfg);
        *label = "BRIGHTNESS COLOURS";
        snprintf(value, size, "%s", i >= 0 ? BAND_PRESETS[i].name : "YOUR OWN");
        break;
    }
    case L_BAND1: case L_BAND2: case L_BAND3: case L_BAND4: {
        static const char *const BL[4] = { "  DIM", "  MEDIUM", "  BRIGHT", "  FULL" };
        int b = line == L_BAND1 ? cfg->band1 : line == L_BAND2 ? cfg->band2 : line == L_BAND3 ? cfg->band3 : cfg->band4;
        int i = rgb_index(b);
        *label = BL[line - L_BAND1];
        if (i >= 0) snprintf(value, size, "%s", COLORS[i].name); else snprintf(value, size, "%06X", b & 0xffffff);
        break;
    }
    case L_HUM:          *label = "MAINS HUM"; snprintf(value, size, "%s", cfg->mains_hum ? "ON" : "OFF  AS A VECTOR MONITOR"); break;
    default:             *label = "?"; break;
    }
}

void vx_menu_draw(vx_menu *u, const vx_config *cfg, const vx_config *all, vp_input *in, vp_dvg *d)
{
    const vx_config *shown = u->scope == VX_SCOPE_ALL ? all : cfg;   /* the level the lines show */
    int page = u->page >= 0 && u->page < N_PAGES ? u->page : P_MAIN;
    float STEP = 170.0f, GAP = 90.0f, H = 78.0f;
    const float LEFT = 520.0f, RIGHT = 3600.0f, ROOM = 3000.0f;
    uint8_t lines[PAGE_MAX + 1];
    u->p2_shown = shown->port2;
    u->cmode_shown = shown->color_mode;
    page_lines(u, page, lines);

    char title[160];
    if (page == P_MAIN) upper(title, sizeof title, u->title);
    else snprintf(title, sizeof title, "%s  %s", TITLE[page], u->scope == VX_SCOPE_ALL ? "ALL GAMES" : "THIS GAME");
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
    int k = 0, chosen_id = L_GAP, any_own = 0, chosen_own = 0;
    for (int i = 0; lines[i] != L_END; i++) {
        int line = lines[i];
        if (!line) { y -= GAP; continue; }
        const char *label;
        char value[300];
        describe(line, u, shown, in, &label, value, sizeof value);
        /* '*': this game has a value of its own here */
        int own = line >= L_FN0 ? fn_own(u, line - L_FN0) : line_own(u, line);
        if (own) {
            char v[300];
            snprintf(v, sizeof v, "* %s", value);
            snprintf(value, sizeof value, "%s", v);
            any_own = 1;
        }
        if (k == u->item[page] && selectable(line)) chosen_own = own;
        if (line == L_NOTE_NEXT || line == L_P2_NOTE) {     /* (notes: smaller, not chosen) */
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
    } else if (any_own && page != P_MAIN) {
        vp_color(d, 255, 180, 80);
        vp_text(d, u->scope == VX_SCOPE_ALL ? "* THIS GAME HAS ITS OWN VALUE  (SET WITH THIS GAME)" : "* THIS GAME'S OWN VALUE",
                2048, 430, 50, VP_TEXT_CENTER);
    }
    vp_color(d, 110, 130, 255);
    const char *one = "UP DOWN CHOOSE   LEFT RIGHT CHANGE", *two = "START 1 OR ENTER DO   ESC BACK   F2 OR TAB SAVE";
    if (u->scope == VX_SCOPE_GAME && chosen_own && chosen_id < L_FN0)
        two = "DEL AS ALL GAMES   ESC BACK   F2 OR TAB SAVE";
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
