/* coloredit.h - the colour editor's logic: colouring a game without knowing anything about it.
 *
 * No screen and no controls here: the front ends are the PC's Vectrex Colour Studio (src/studio/studio.cpp)
 * and vxcolor (tools/vxcolor/main.c, docs/AI_GUIDE.md). The cabinet's emulator has no editor: it shows the
 * profiles made on the PC (PICTURE, COLOUR PROFILE) and its own simple colour modes.
 *
 * A frozen picture is cut into PIECES: lines drawn one after the other by the same kind of routine from
 * one vector list (a ship, a mine, a word, a row of dots). A front end picks a piece, a line or a set of
 * lines (vx_ce_mouse_pick), sets scope, color and gain in the state, and asks for the rules
 * (vx_ce_refresh); vx_ce_take puts them first in the profile.
 *
 * The scopes (which lines a new rule colours):
 *   VX_CE_SHAPE    the same vector list wherever it is drawn (all mines of this kind)
 *   VX_CE_LINE     one part of the shape wherever the shape is drawn: a vector of its list (the light of
 *                  every lamp post), an entry of a game's table (one building in Armor Attack), or, in a
 *                  game drawing without a list, one stroke
 *   VX_CE_OBJECT   everything the game draws for this object (the player's ship, its explosion, its
 *                  hyperspace jump): the 6809's U
 *   VX_CE_GROUP    what this routine draws for the objects of its table near this one (the mine seeds,
 *                  not the shots)
 *   VX_CE_CODE     all that one place in the game's code draws (vx_tag caller): the way to tell objects
 *                  apart in games drawing without lists
 *   VX_CE_ROUTINE  all the game draws with this routine (all dots, all text)
 *   VX_CE_SMART    what is connected to it: every line touching it, and the lines touching those (a whole
 *                  figure). The rules are chosen for it: a shape picked whole gets one SHAPE rule, other
 *                  lines one LINE rule each
 *   VX_CE_KIND     the clicked line's shape, narrowed to what looks like it: only its dots or only its
 *                  lines, only the lines about as long (when the shape's lines fall into lengths far
 *                  apart: money and walls)
 *
 * The rules are render/colors.h's: x / pc / u ranges, as a profile written by hand. A UVMC2 / PiTrex
 * game has no tags (the 6809 does not draw): vx_coloredit_open says so and there is nothing to edit. */
#ifndef VX_COLOREDIT_H
#define VX_COLOREDIT_H

#include "core/vx_core.h"
#include "platform/config.h"
#include "render/colors.h"

#define VX_CE_PIECES 512
#define VX_CE_MAXLINES 40000    /* (the core's lines a frame at most) */
#define VX_CE_CANDS 256         /* rules made at once for lines picked with a box */

enum { VX_CE_SHAPE, VX_CE_LINE, VX_CE_OBJECT, VX_CE_GROUP, VX_CE_CODE, VX_CE_ROUTINE, VX_CE_SMART, VX_CE_KIND, VX_CE_SCOPES };

typedef struct vx_piece {
    int      first, last;       /* its lines (draw order) */
    uint16_t pc_lo, pc_hi;      /* its routine (a 32-byte block of code) */
    uint16_t x_lo, x_hi;        /* its vector list */
    uint16_t u;                 /* the object (U of its first line) */
    uint16_t caller;            /* the game's code that drew it */
    int      parts;             /* its stretches with one X (vx_line part): 1 when drawn without a list */
    int      dots;              /* all its lines are dots */
    float    cx, cy;            /* its middle (canvas units) */
} vx_piece;

typedef struct vx_coloredit {
    int        open;
    int        panel;           /* something is picked and rules are being made for it */
    int        line;            /* VX_CE_LINE: the line (its index in the picture) */
    int        scope;           /* VX_CE_* */
    int        color;           /* the palette's index, or -1 NONE */
    float      gain;
    int        sel;             /* the chosen piece */
    vx_piece   piece[VX_CE_PIECES];
    int        npieces;
    vx_rule    cand;            /* the first of the rules below */
    char       said[96];        /* a message for the front end to show */
    int        pick_line;       /* the line clicked (-1 none): VX_CE_LINE starts there */
    int        nmsel;           /* lines picked with a box / Ctrl+click (0: the chosen piece as usual) */
    unsigned char msel[VX_CE_MAXLINES];
    vx_rule    cands[VX_CE_CANDS];   /* the rules a take would make (one per piece / line picked) */
    int        ncand;
} vx_coloredit;

/* Open on the frozen picture. 0, or -1 (the game has no tags: a UVMC2 / PiTrex game). */
int  vx_coloredit_open(vx_coloredit *e, const vx_line *l, int n);
/* The mouse let go (board units): a click at (x1, y1) when box is 0, else the box from (x0, y0);
 * ctrl: Ctrl held (one line more or less / the box added). */
void vx_ce_mouse_pick(vx_coloredit *e, const vx_colors *colors, const vx_config *cfg, const vx_line *l, int n,
                      int x0, int y0, int x1, int y1, int box, int ctrl);
/* On the editor's state (piece / line / lines picked, scope, color, gain): the rules made again after a
 * change; those rules into the profile (0, or -1 full); the rules colouring what is picked removed (how
 * many); whether the rules would colour a line. */
void vx_ce_refresh(vx_coloredit *e, const vx_line *l, int n);
int  vx_ce_take(vx_coloredit *e, vx_colors *colors);
int  vx_ce_remove(vx_coloredit *e, vx_colors *colors, const vx_line *l, int n);
int  vx_ce_matches(const vx_coloredit *e, const vx_line *l);
/* The picture's figures, as SMART picks them (lines connected): fig[i] the figure of line i (from 0, in
 * draw order; -1 for an unlit line). Returns how many. */
int  vx_ce_figures(const vx_line *l, int n, int *fig);
/* The own profile an editor writes for the game's color_profile setting: k (1 ..) and whether it is a new
 * one. new_one 0: an own profile shown is changed; 1, or no own profile shown (ORIGINAL, OFF): the next
 * number, so an ORIGINAL or another profile is never written over. */
int  vx_coloredit_target(const char *game_id, int setting, int new_one, int *is_new);

/* The parts the above is made of (the tests, vxshot): cutting the picture into pieces, the rule for a scope */
int  vx_ce_pieces(const vx_line *l, int n, vx_piece *out, int max);
void vx_ce_rule(const vx_piece *p, int np, int sel, const vx_line *l, int n, int scope,
                uint8_t r, uint8_t g, uint8_t b, float gain, vx_rule *out);
/* VX_CE_LINE: the rule for line `line` (its routine and its place in the vector list). */
void vx_ce_rule_line(const vx_line *l, int n, int line, uint8_t r, uint8_t g, uint8_t b, float gain, vx_rule *out);

/* The palette, shared with the options menu's colour lines (platform/menu.c): { name, r, g, b }, in
 * families from light to dark, so left / right walks through shades of one colour before the next.
 * (The colours of the first, 18-colour palette keep their values: settings and profiles store RGB.)
 * A vector monitor shows dark colours dim: the darkest shades are kept bright enough to be seen. */
#define VX_PALETTE_ENTRIES \
    /* whites and greys */ \
    { "WHITE", 255, 255, 255 }, { "WARM WHITE", 255, 240, 215 }, { "BLUE WHITE", 200, 220, 255 }, \
    { "SILVER", 200, 200, 210 }, { "GREY", 150, 150, 160 }, { "STEEL GREY", 120, 135, 160 }, \
    { "DARK GREY", 100, 100, 108 }, \
    /* reds */ \
    { "LIGHT RED", 255, 120, 110 }, { "RED", 255, 60, 50 }, { "SCARLET", 255, 30, 0 }, \
    { "CRIMSON", 220, 20, 60 }, { "DARK RED", 160, 20, 20 }, \
    /* pinks */ \
    { "LIGHT PINK", 255, 190, 210 }, { "PINK", 255, 140, 180 }, { "ROSE", 255, 70, 130 }, \
    /* oranges */ \
    { "PEACH", 255, 180, 130 }, { "ORANGE", 255, 140, 30 }, { "DARK ORANGE", 230, 90, 0 }, \
    /* yellows and golds */ \
    { "CREAM", 255, 245, 180 }, { "LIGHT YELLOW", 255, 255, 140 }, { "YELLOW", 255, 240, 80 }, \
    { "LEMON", 230, 255, 0 }, { "GOLD", 255, 200, 0 }, { "AMBER", 255, 180, 60 }, \
    /* browns */ \
    { "TAN", 210, 170, 120 }, { "LIGHT BROWN", 190, 130, 70 }, { "BROWN", 150, 90, 40 }, \
    { "COPPER", 200, 110, 50 }, { "DARK BROWN", 110, 60, 25 }, \
    /* greens */ \
    { "MINT", 160, 255, 190 }, { "LIGHT GREEN", 130, 255, 130 }, { "GREEN", 80, 255, 120 }, \
    { "LIME", 170, 255, 40 }, { "EMERALD", 0, 210, 110 }, { "FOREST GREEN", 40, 160, 50 }, \
    { "OLIVE", 150, 160, 40 }, { "DARK GREEN", 20, 120, 40 }, \
    /* cyans and teals */ \
    { "ICE", 160, 240, 255 }, { "AQUA", 80, 255, 230 }, { "CYAN", 0, 200, 255 }, \
    { "TURQUOISE", 0, 210, 190 }, { "TEAL", 0, 150, 140 }, \
    /* blues */ \
    { "SKY BLUE", 110, 190, 255 }, { "LIGHT BLUE", 140, 170, 255 }, { "BLUE", 80, 140, 255 }, \
    { "ROYAL BLUE", 50, 90, 255 }, { "DEEP BLUE", 32, 80, 255 }, { "DARK BLUE", 30, 50, 190 }, \
    /* purples and magentas */ \
    { "LAVENDER", 200, 170, 255 }, { "VIOLET", 160, 90, 255 }, { "PURPLE", 200, 100, 255 }, \
    { "DARK PURPLE", 120, 40, 190 }, { "MAGENTA", 255, 60, 220 }, { "FUCHSIA", 255, 0, 160 }
int  vx_ce_palette_count(void);
const char *vx_ce_palette(int i, uint8_t *r, uint8_t *g, uint8_t *b);

#endif
