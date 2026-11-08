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

#include <vectorpie/vp_dvg.h>

/* The canvas point (cx, cy) in board units. */
void vx_map(const vx_config *cfg, double cx, double cy, float *x, float *y);
void vx_draw_lines(vp_dvg *d, const vx_config *cfg, const vx_line *l, int n);

#endif
