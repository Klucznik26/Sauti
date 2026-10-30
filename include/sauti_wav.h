#ifndef SAUTI_WAV_H
#define SAUTI_WAV_H
#include <stdint.h>
#include <stdio.h>
/* Portable stereo PCM16 WAV writer. Caller owns output path, capture and timing.
 * Stream sample rate MUST match the passed sample_rate. */
typedef struct {
    FILE *file;
    uint64_t frames;
    uint32_t sample_rate;
    int failed;
} SautiWav;
int sauti_wav_begin(SautiWav *wav,FILE *file,uint32_t sample_rate);
int sauti_wav_append(SautiWav *wav,const float *left,const float *right,uint32_t frames);
int sauti_wav_finish(SautiWav *wav);
#endif
