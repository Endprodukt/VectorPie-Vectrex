/* audio.h - the Vectrex's sound to the SDK's stream (as Major Havoc PE Enhanced's platform/audio.c). */
#ifndef VX_AUDIO_H
#define VX_AUDIO_H

#include <stdint.h>

int  vx_audio_open(void);
void vx_audio_close(void);
/* A frame's samples (vx_core.h vx_audio): 1 or 2 channels at VX_AUDIO_RATE, the two mixed. */
void vx_audio_feed(const int16_t *samples, int frames, int channels, float volume);

#endif
