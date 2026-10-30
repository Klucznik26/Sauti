#ifndef SAUTI_PCM_OUTPUT_H
#define SAUTI_PCM_OUTPUT_H
/*
 * Platform-neutral sink contract for already-decoded interleaved float PCM.
 * The decoder owns file formats, the sink owns playback timing and hardware.
 * No SDL/PipeWire/Linux types cross this boundary.
 */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
} SautiPcmFormat;

typedef struct SautiPcmOutputOps {
    int (*open)(void *ctx,SautiPcmFormat format);
    size_t (*write)(void *ctx,const float *interleaved,size_t frames);
    void (*pause)(void *ctx,int pause);
    void (*flush)(void *ctx);
    size_t (*queued_frames)(const void *ctx);
    void (*close)(void *ctx);
} SautiPcmOutputOps;

typedef struct {
    void *context;
    const SautiPcmOutputOps *ops;
    SautiPcmFormat format;
    int opened;
} SautiPcmOutput;

int sauti_pcm_output_open(SautiPcmOutput *out,void *context,
                          const SautiPcmOutputOps *ops,SautiPcmFormat format);
size_t sauti_pcm_output_write(SautiPcmOutput *out,const float *pcm,size_t frames);
void sauti_pcm_output_pause(SautiPcmOutput *out,int pause);
void sauti_pcm_output_flush(SautiPcmOutput *out);
size_t sauti_pcm_output_queued(const SautiPcmOutput *out);
void sauti_pcm_output_close(SautiPcmOutput *out);

#endif /* SAUTI_PCM_OUTPUT_H */
