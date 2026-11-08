/* menu.h - the options menu (as Major Havoc PE Enhanced's service menu, platform/menu.c there).
 *
 * Tab or F2 opens it while a game runs; the game stands still meanwhile. Pages:
 *
 *   CONTROLS PLAYER 1 / 2   the controller's stick directions and four buttons, laid out the SDK's
 *                           way (vp_capture): START 1 or Enter on a row, then the control; the first
 *                           since the cursor came to the row replaces, every further one adds.
 *                           Left / right: none or the standard. Delete: the standard.
 *   STICK                   digital directions as the analog stick (ramp out, back, its curve, how
 *                           far), the analog stick's dead zone, and a live test
 *   PICTURE                 the tube (lying / standing), size, width, height, place, mirroring,
 *                           brightness, gamma, colour, the board's rate; the game's picture behind it
 *   MACHINE                 the BIOS intro, the UVMC2 mode
 *   SOUND                   volume
 *
 * SAVE FOR says where the changes go: THIS GAME (save/games/<id>.ini and save/controls/<id>.ini:
 * only this game) or ALL GAMES (save/vectrex.ini and save/controls.ini: every game without its own
 * setting; the controls laid out then become every game's, and this game's own controls file goes).
 *
 *   up / down            choose a line
 *   left / right         change its value
 *   START 1 or Enter     change it, open a page, or do what it says
 *   Esc                  back (on the first page: save and back to the game)
 *   F2 or Tab            save and back to the game
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
    VX_MENU_CLOSE,          /* save and back to the game */
    VX_MENU_QUIT,           /* save and leave the game (back to the game list) */
};

enum { VX_SCOPE_GAME = 0, VX_SCOPE_ALL = 1 };

typedef struct vx_menu {
    int    open;
    int    page;
    int    item[8];
    int    scope;           /* VX_SCOPE_*: where SAVE puts the changes */
    int    confirm;         /* fire once more: this game back to every game's settings */
    int    dir_x, dir_y;
    double repeat_at;
    int    rep_n;
    int    armed_x, armed_y;
    int    testing;         /* the stick test: the controls go to the pads, not the menu */
    vx_pad test_pad;
    vp_capture cap;
    vx_config at_open;      /* the settings when the menu opened (what changed goes to ALL GAMES) */
    int    controls_changed;
    int    uvmc2;           /* the game is a UVMC2 one (its mode line) */
    char   game[64];        /* the game's id, name */
    char   title[96];
} vx_menu;

void vx_menu_open(vx_menu *u, const vx_config *cfg, const char *game_id, const char *game_name, int uvmc2);
/* Once a frame while open. all: every game's settings (THIS GAME AS ALL GAMES); p1: player 1's
 * controller as read this frame (the stick test). */
int  vx_menu_update(vx_menu *u, vx_config *cfg, const vx_config *all, vp_input *in, const vx_pad *p1, double now);
void vx_menu_draw(vx_menu *u, const vx_config *cfg, vp_input *in, vp_dvg *d);
/* 1 while the PICTURE page is open: the game's picture goes behind it. */
int  vx_menu_on_picture(const vx_menu *u);

/* Write the changes where SAVE FOR says. all: every game's settings (changed with ALL GAMES). */
void vx_menu_save(vx_menu *u, const vx_config *cfg, vx_config *all, vp_input *in);

#endif
