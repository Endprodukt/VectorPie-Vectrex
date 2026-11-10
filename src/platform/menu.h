/* menu.h - the options menu (as Major Havoc PE Enhanced's service menu, platform/menu.c there).
 *
 * Tab or F2 opens it while a game runs (the game stands still meanwhile), or in the game list for the
 * game chosen there.
 *
 * Two levels of settings, chosen on the first line, SETTINGS FOR:
 *
 *   ALL GAMES   every game's settings (save/vectrex.ini, save/controls.ini); the lines show them, and a
 *               change applies to every game that has no value of its own for it. The default in the
 *               game list.
 *   THIS GAME   the game's own (save/games/<id>.ini, save/controls/<id>.ini: only what differs from all
 *               games); the lines show what the game runs with, a change becomes the game's own.
 *               Delete on a line: back to the all games value. The default in a game.
 *
 * A '*' marks a line where this game has a value of its own.
 *
 * Pages:
 *   CONTROLS PLAYER 1 / 2   the controller's stick directions and four buttons, laid out the SDK's
 *                           way (vp_capture): START 1 or Enter on a row, then the control; the first
 *                           since the cursor came to the row replaces, every further one adds.
 *                           Left / right: none or the standard. Delete: the standard.
 *   STICK                   digital directions as the analog stick (ramp out, back, its curve, how
 *                           far), the analog stick's dead zone, and a live test
 *   PICTURE                 the tube (lying / standing), size, width, height, place, mirroring,
 *                           brightness, gamma, the board's rate; the colour profile on / off; the
 *                           colour mode: one colour, or by brightness (four bands, presets or each
 *                           band's own colour); the game's picture behind it
 *                           (in the list: the Vectrex's screen as a frame)
 *   PORT 2                  what is plugged into controller port 2 (joystick, spinner, trackball,
 *                           AtariVox, VecVox, VecVoice) and the spinner's / trackball's settings
 *   MACHINE                 the BIOS intro, the UVMC2 mode, the mains hum
 *   SOUND                   volume
 *
 *   up / down            choose a line
 *   left / right         change its value
 *   START 1 or Enter     change it, open a page, or do what it says
 *   Esc                  back (on the first page: save and back)
 *   F2 or Tab            save and back
 *
 * The menu's own controls, never what is laid out (the SDK's menu controls). */
#ifndef VX_MENU_H
#define VX_MENU_H

#include "platform/config.h"
#include "platform/controls.h"

#include <vectorpie/vp.h>
#include <vectorpie/vp_controls.h>

enum {
    VX_MENU_NOTHING = 0,
    VX_MENU_CHANGED,        /* a setting changed: take it over (the picture at once) */
    VX_MENU_RESTART,        /* power the console on again (the BIOS intro setting applies) */
    VX_MENU_CLOSE,          /* save and back */
    VX_MENU_QUIT,           /* save and leave the game (back to the game list) */
};

enum { VX_SCOPE_GAME = 0, VX_SCOPE_ALL = 1 };

typedef struct vx_menu {
    int    open;
    int    page;
    int    item[8];
    int    scope;           /* VX_SCOPE_*: the level the lines show and change */
    int    confirm;         /* fire once more: this game back to all games */
    int    dir_x, dir_y;
    double repeat_at;
    int    rep_n;
    int    armed_x, armed_y;
    int    testing;         /* the stick test: the controls go to the pads, not the menu */
    vx_pad test_pad;
    vp_capture cap;
    unsigned char own[VX_CONFIG_MAX_FIELDS];   /* the game has its own value for this setting */
    int    all_changed;     /* every game's settings changed: save/vectrex.ini to write */
    vx_fn_table fn_all, fn_game;               /* the controls files as they are */
    unsigned char fn_changed[VX_FN_COUNT];     /* laid out now: VX_SCOPE_GAME + 1 or VX_SCOPE_ALL + 1 */
    int    game_reset;      /* the game's own controls all dropped */
    int    uvmc2;           /* the game is a UVMC2 one (its mode line) */
    int    in_list;         /* opened from the game list: no restart / leave lines */
    int    p2_shown;        /* port 2's device on the level shown (the PORT 2 page's lines) */
    int    cmode_shown;     /* the colour mode on the level shown (the PICTURE page's lines) */
    vx_config at_open;      /* (the UVMC2 note: its mode changed) */
    char   game[64];        /* the game's id, name */
    char   title[96];
} vx_menu;

/* cfg: what the game runs with (all games' settings with its own on top); all: every game's. */
void vx_menu_open(vx_menu *u, const vx_config *cfg, const vx_config *all, const char *game_id,
                  const char *game_name, int uvmc2, int in_list);
/* Once a frame while open. Changes go to all (ALL GAMES) or cfg (THIS GAME); cfg always ends up as
 * what the game runs with. p1: player 1's controller as read this frame (the stick test). */
int  vx_menu_update(vx_menu *u, vx_config *cfg, vx_config *all, vp_input *in, const vx_pad *p1, double now);
void vx_menu_draw(vx_menu *u, const vx_config *cfg, const vx_config *all, vp_input *in, vp_dvg *d);
/* 1 while the PICTURE page is open: the game's picture goes behind it. */
int  vx_menu_on_picture(const vx_menu *u);

/* Write what changed: every game's files and the game's own. */
void vx_menu_save(vx_menu *u, const vx_config *cfg, const vx_config *all, vp_input *in);

#endif
