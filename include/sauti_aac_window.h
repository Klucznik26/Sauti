#ifndef SAUTI_AAC_WINDOW_H
#define SAUTI_AAC_WINDOW_H
#include <stddef.h>
#include <stdint.h>
/* AAC window shapes 0=SIN 1=KBD; previous shape is carried between frames.
 * sequence: 0 ONLY_LONG, 1 LONG_START, 2 EIGHT_SHORT, 3 LONG_STOP.
 * long_time points to 2048 unwindowed IMDCT samples for sequences 0,1,3.
 * short_time points to 8 contiguous, 256-sample short IMDCT blocks for 2.
 * Output PCM and 1024-sample overlap history update atomically on failure.
 * Caller manages sequence transition validation and state reset at seek. */
int sauti_aac_window_generate(unsigned n,unsigned shape,float *window);
int sauti_aac_window_synth(unsigned sequence,unsigned previous_shape,
                           unsigned current_shape,const float *imdct,
                           const float previous_overlap[1024],
                           float pcm[1024],float next_overlap[1024]);
#endif
