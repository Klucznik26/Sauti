#ifndef SAUTI_DECODER_H
#define SAUTI_DECODER_H
/* Portable PCM decoder boundary: no OS, file API, SDL or audio device types. */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_rate;
    uint32_t channels;
    uint64_t total_frames; /* 0 when unknown */
} SautiPcmInfo;

typedef struct SautiDecoderOps {
    const char *name;
    /* Probe a small prefix without consuming external stream state. */
    int (*probe)(const uint8_t *header,size_t length);
    /* Open takes a read-only byte view; decoder must not outlive these bytes. */
    void *(*open)(const uint8_t *data,size_t length,SautiPcmInfo *info);
    /* Interleaved float PCM, number of frames returned; 0 = EOF. */
    size_t (*read)(void *state,float *samples,size_t frames);
    int (*seek)(void *state,uint64_t frame);
    void (*close)(void *state);
} SautiDecoderOps;

typedef struct {
    const SautiDecoderOps *ops;
    void *state;
    SautiPcmInfo info;
} SautiDecoder;

int sauti_decoder_ops_valid(const SautiDecoderOps *ops);
int sauti_decoder_open(SautiDecoder *out,const SautiDecoderOps *const *registry,
                       size_t count,const uint8_t *data,size_t length);
size_t sauti_decoder_read(SautiDecoder *decoder,float *samples,size_t frames);
int sauti_decoder_seek(SautiDecoder *decoder,uint64_t frame);
void sauti_decoder_close(SautiDecoder *decoder);
#endif
