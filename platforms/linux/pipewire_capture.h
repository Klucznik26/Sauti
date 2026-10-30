#ifndef SAUTI_PIPEWIRE_CAPTURE_H
#define SAUTI_PIPEWIRE_CAPTURE_H
#include "sauti.h"
#include "sauti_record_ring.h"
#include "sauti_audio_backend.h"
typedef struct SautiCapture SautiCapture;
extern const SautiAudioBackendOps sauti_pipewire_ops;
/* Only Linux adapter references PipeWire. The shared DSP/ring ABI is OS-free.
 * target is optional PipeWire node name/serial selected by the user.
 * monitor=true captures a sink monitor, false captures a source/microphone.
 * Returns NULL on synchronous setup failure; state may later become ERROR.
 */
SautiCapture *sauti_capture_start(SautiAudioRing *ring, int monitor, const char *target);
SautiCapture *sauti_capture_start_stereo(SautiStereoRing *stereo, SautiAudioRing *mono,
                                        int monitor, const char *target);
SautiCapture *sauti_capture_start_recording(SautiStereoRing *stereo, SautiAudioRing *mono,
                                        SautiRecordRing *record, int monitor, const char *target);
/* Serializes recorder enable/disable with the PipeWire process callback. */
void sauti_capture_record_enable(SautiCapture *capture,int enabled);
/* Returns 0 until stereo F32 format is negotiated, otherwise the real rate. */
uint32_t sauti_capture_sample_rate(const SautiCapture *capture);
int sauti_capture_status(const SautiCapture *capture);
void sauti_capture_stop(SautiCapture *capture);
#endif
