/* port2.h - what is plugged into the Vectrex's controller port 2 (vx_config port2, per game).
 *
 *   joystick    player 2's controller (platform/controls.h)
 *   spinner     the cabinet's spinner (the SDK's p1_dialx: on the cabinet the SpinTrak, on a PC the
 *               mouse) as an Atari driving controller: a Gray code on buttons 1 and 2, one position a step
 *               (00, button 2, both, button 1; C. Malcolm's spinner example, 2017)
 *   trackball   the cabinet's trackball (p1_trackballx / _y; a mouse) as an Atari trackball: per axis a
 *               direction line and a clock line that turns over each step: X direction on button 1
 *               (pressed = left), X clock on button 2, Y direction on button 3 (pressed = up), Y clock on
 *               button 4 (as the LOTR and Fortress of Narzod trackball patches read it; Narzod's polarity
 *               checked on hardware)
 *   atarivox, vecvox, vecvoice   speech: the core's (core/vx_core.h vx_set_port2_device)
 *
 * The steps come out while the frame runs (vx_set_port2_hook): at most spin_steps / tb_steps a frame,
 * evenly spaced, so a game that reads the port several times a frame sees each step. Steps the game
 * could not take in time wait (at most a frame's worth), the rest are dropped. */
#ifndef VX_PORT2_H
#define VX_PORT2_H

#include "core/vx_core.h"
#include "platform/config.h"

#include <vectorpie/vp_input.h>

typedef struct vx_port2_dev {
    int      kind;              /* vx_port2 */
    double   acc_x, acc_y;      /* steps to give out (signed) */
    int      pos;               /* spinner: its position 0..3 */
    int      clk_x, clk_y, dir_x, dir_y;     /* trackball lines */
    unsigned long long next_x, next_y;       /* (cycles) */
    unsigned gap_x, gap_y;      /* cycles between steps */
    int      limit;             /* steps that may wait */
    unsigned buttons;           /* what the lines are now */
} vx_port2_dev;

/* Take the device of the settings (again after they changed): the core's port 2 device and hook. */
void vx_port2_setup(vx_port2_dev *p, const vx_config *cfg);
/* Once per update, before the frames run: the cabinet's spinner / trackball movement since the last. */
void vx_port2_read(vx_port2_dev *p, vp_input *in, const vx_config *cfg);

/* (for the tests) the hook itself */
unsigned vx_port2_hook(void *user, unsigned long long cycle);

#endif
