/* controls.h - the cabinet's controls as the Vectrex's two controllers, through the SDK's own
 * controls (vectorpie/vp_controls.h).
 *
 * Each controller: a stick and four buttons. The functions, each laid out by the player (kept by
 * the SDK, vp_input_save), with the cabinet's own controls as their standard (advmame.rc):
 *
 *   pad1_up pad1_down pad1_left pad1_right   p1_up ...    the stick, digitally
 *   pad1_b1 .. pad1_b4                       p1_button1 .. 4
 *   pad2_up ... pad2_b4                      p2_up ..., p2_button1 .. 4
 *
 * (named apart from the advmame.rc actions they stand on), in this order in VX_FN_NAME.
 *
 * and an analog stick each, p1_stickx / p1_sticky and p2_*, when one is there. Read in two steps:
 * save/controls.ini (every game), then save/controls/<id>.ini (one game's own: what it names wins).
 *
 * A digital direction moves the Vectrex's analog stick along a ramp (vx_config dig_*): out to
 * dig_max in dig_attack seconds while held, back in dig_release seconds, shaped by dig_curve.
 * An analog stick, when pushed past its dead zone, goes straight through instead.
 *
 * The options menu: Tab or F2 (the cabinet's ui_configure and service), never laid out. */
#ifndef VX_CONTROLS_H
#define VX_CONTROLS_H

#include "core/vx_core.h"
#include "platform/config.h"

#include <vectorpie/vp_input.h>

#define VX_FN_COUNT 16      /* per controller: up, down, left, right, buttons 1..4 */
extern const char *const VX_FN_NAME[VX_FN_COUNT];

typedef struct vx_controls {
    float phase[2][2];      /* [player][x, y]: where the digital ramp is, -1..1 */
    int   menu;             /* the options menu's key went down */
} vx_controls;

/* Defines the functions on `in` and loads the player's: every game's, then game_id's own. */
void vx_controls_init(vx_controls *c, vp_input *in, const char *game_id);
/* Once per update: dt the seconds since the last. */
void vx_controls_read(vx_controls *c, vp_input *in, const vx_config *cfg, double dt,
                      vx_pad *p1, vx_pad *p2);

/* The digital ramp: where a direction held (dir -1, 0, 1) takes the phase in dt seconds, and the
 * stick's position (-1..1) for a phase. (Exposed for the tests.) */
float vx_ramp_step(float phase, int dir, double dt, const vx_config *cfg);
float vx_ramp_out(float phase, const vx_config *cfg);

/* The files (in buf). */
const char *vx_controls_path(const char *game_id, char *buf, int size);

#endif
