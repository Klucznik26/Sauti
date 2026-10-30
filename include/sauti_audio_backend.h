#ifndef SAUTI_AUDIO_BACKEND_H
#define SAUTI_AUDIO_BACKEND_H
#include <stdint.h>
#include "sauti.h"
#include "sauti_record_ring.h"
/* OS-independent capture contract. A platform adapter owns the native handle.
 * An instance must be closed before its rings are freed or replaced. */
typedef struct SautiAudioBackend SautiAudioBackend;
typedef struct {
    void *(*open)(SautiStereoRing *,SautiAudioRing *,SautiRecordRing *,
                  int monitor,const char *target);
    int (*status)(const void *);
    uint32_t (*sample_rate)(const void *);
    void (*record_enable)(void *,int);
    void (*close)(void *);
} SautiAudioBackendOps;
struct SautiAudioBackend {
    void *handle;
    const SautiAudioBackendOps *ops;
};
int sauti_audio_open(SautiAudioBackend *backend,const SautiAudioBackendOps *ops,
                     SautiStereoRing *stereo,SautiAudioRing *mono,
                     SautiRecordRing *record,int monitor,const char *target);
int sauti_audio_status(const SautiAudioBackend *backend);
uint32_t sauti_audio_sample_rate(const SautiAudioBackend *backend);
void sauti_audio_record_enable(SautiAudioBackend *backend,int enabled);
void sauti_audio_close(SautiAudioBackend *backend);
#endif
