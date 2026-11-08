/* browser.h - the game list: the program started without a cartridge (as the VectorPie menu starts
 * a package) shows the cartridges of platform/roms.h, Mine Storm (the console's own game) first.
 *
 *   up / down       a game (held: repeats)
 *   left / right    the previous / next first letter
 *   START 1, Enter, button 1 (ui_select)   play it
 *   Esc (ui_cancel) back to the VectorPie menu
 *
 * Looks like the VectorPie menu: the game chosen full size in the middle, pulsing, the others
 * smaller towards the edges; a hint line at the bottom. */
#ifndef VX_BROWSER_H
#define VX_BROWSER_H

#include "platform/roms.h"

#include <vectorpie/vp.h>

typedef struct vx_browser {
    int    sel;             /* the entry chosen: 0 Mine Storm, 1.. the list's rom[sel - 1] */
    int    rep_dir;
    double rep_at;
    int    armed;           /* the select controls were seen up since the list came back */
} vx_browser;

/* Choose the entry of game id `last` (or the first). */
void vx_browser_init(vx_browser *b, const vx_rom_list *l, const char *last);
/* Once per frame: the entry to play (0 Mine Storm, 1.. a cartridge), or -1. */
int  vx_browser_update(vx_browser *b, const vx_rom_list *l, vp_input *in, double now);
void vx_browser_draw(const vx_browser *b, const vx_rom_list *l, vp_dvg *d, double now);

/* An entry's name and its game id. */
const char *vx_browser_name(const vx_rom_list *l, int entry);
const char *vx_browser_id(const vx_rom_list *l, int entry);

#endif
