/* draw.h - the Vectrex's lines on the VectorPie output.
 *
 * The Vectrex's picture (the core's canvas, standing, vx_core.h) is fitted into the tube with its own
 * shape kept: on a lying 4:3 tube (VX_MONITOR_HORIZONTAL) as high as the tube, with borders left and
 * right; on a standing 3:4 one as large as it goes. Then the size, stretch, place and mirroring of
 * vx_config. The board's coordinates are as the viewer sees the tube (the SDK's own: a standing
 * monitor's are already turned). */
#ifndef VX_DRAW_H
#define VX_DRAW_H

#include "core/vx_core.h"
#include "platform/config.h"
#include "render/colors.h"

#include <vectorpie/vp_dvg.h>

/* The canvas point (cx, cy) in board units. */
void vx_map(const vx_config *cfg, double cx, double cy, float *x, float *y);
/* colors: the game's colour profile (render/colors.h; NULL or cfg->color_profile 0: the picture's own
 * colour). */
void vx_draw_lines(vp_dvg *d, const vx_config *cfg, const vx_colors *colors, const vx_line *l, int n);
/* A line's colour as drawn (the profile's or the picture's, dimmed by its brightness): 0 if it is dark. */
int  vx_line_color(const vx_config *cfg, const vx_colors *colors, const vx_line *l, uint8_t *r, uint8_t *g, uint8_t *b);
/* The Vectrex's screen as a frame with a cross in its middle, in the settings' size and place (the
 * picture page when no game runs). */
void vx_draw_frame(vp_dvg *d, const vx_config *cfg);

#endif
