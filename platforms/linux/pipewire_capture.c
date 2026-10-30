/* Platform boundary only: PipeWire F32 stereo -> NASM SPSC mono queue.
 * RT callback does no allocation, logging, blocking, FFT or rendering.
 * Single producer thread. Application/UI owns the single consumer.
 */
#include "pipewire_capture.h"
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <spa/param/param.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

struct SautiCapture {
    struct pw_thread_loop *loop;
    struct pw_stream *stream;
    SautiAudioRing *ring;
    SautiStereoRing *stereo_ring;
    SautiRecordRing *record_ring;
    atomic_int state;
    atomic_uint negotiated_rate;
    int loop_started;
};

static void on_state_changed(void *data, enum pw_stream_state old,
                             enum pw_stream_state state, const char *error) {
    (void)old;
    (void)error;              /* No log I/O from PipeWire callback threads. */
    SautiCapture *c = data;
    atomic_store_explicit(&c->state, (int)state, memory_order_release);
}

static void on_param_changed(void *data,uint32_t id,const struct spa_pod *param){
    SautiCapture *c=data;
    if(id!=SPA_PARAM_Format)return;
    uint32_t rate=0;
    if(param){
        struct spa_audio_info_raw info={0};
        if(spa_format_audio_raw_parse(param,&info)>=0 &&
           info.format==SPA_AUDIO_FORMAT_F32 && info.channels==2)
            rate=info.rate;
    }
    atomic_store_explicit(&c->negotiated_rate,rate,memory_order_release);
}
static void on_process(void *data) {
    SautiCapture *c = data;
    struct pw_buffer *b = pw_stream_dequeue_buffer(c->stream);
    if (!b) return;
    struct spa_buffer *buf = b->buffer;
    if (buf && buf->n_datas >= 1) {
        const struct spa_data *d = &buf->datas[0];
        if (d->data && d->chunk && d->chunk->offset <= d->maxsize &&
            d->chunk->size <= d->maxsize - d->chunk->offset &&
            (d->chunk->stride == 0 || d->chunk->stride == 8) &&
            !(d->chunk->size & 7u)) {
            const uint8_t *ptr = (const uint8_t *)d->data + d->chunk->offset;
            if (((uintptr_t)ptr & 3u) == 0) {
                uint32_t n = d->chunk->size / 8u;
                /* A single excessively large block is rejected, never overflowed. */
                if (n > 0 && n <= SAUTI_RING_CAPACITY) {
                    if(c->record_ring)sauti_record_push(c->record_ring,(const float *)ptr,n);
                    if (c->stereo_ring) {
                        (void)sauti_stereo_ring_push_f32(c->stereo_ring, (const float *)ptr, n);
                    } else {
                        (void)sauti_ring_push_stereo_f32(c->ring, (const float *)ptr, n);
                    }
                }
            }
        }
    }
    (void)pw_stream_queue_buffer(c->stream, b);
}
static const struct pw_stream_events stream_events = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .param_changed = on_param_changed,
    .process = on_process,
};

SautiCapture *sauti_capture_start(SautiAudioRing *ring, int monitor, const char *target) {
    return sauti_capture_start_stereo(NULL, ring, monitor, target);
}
SautiCapture *sauti_capture_start_stereo(SautiStereoRing *stereo, SautiAudioRing *mono,
                                        int monitor, const char *target) {
    return sauti_capture_start_recording(stereo,mono,NULL,monitor,target);
}
SautiCapture *sauti_capture_start_recording(SautiStereoRing *stereo, SautiAudioRing *mono,
                                        SautiRecordRing *record, int monitor, const char *target) {
    if ((!stereo && !mono) || (stereo && mono)) return NULL;
    SautiCapture *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->ring = mono;
    c->stereo_ring = stereo;
    c->record_ring = record;
    atomic_init(&c->state, PW_STREAM_STATE_UNCONNECTED);
    atomic_init(&c->negotiated_rate,0);
    pw_init(NULL, NULL);
    c->loop = pw_thread_loop_new("sayari-sauti-audio", NULL);
    if (!c->loop || pw_thread_loop_start(c->loop) < 0) goto fail;
    c->loop_started = 1;

    pw_thread_loop_lock(c->loop);
    struct pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Music",
        NULL);
    if (!props) {
        pw_thread_loop_unlock(c->loop);
        goto fail;
    }
    if (monitor) pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");
    if (target && target[0]) pw_properties_set(props, PW_KEY_TARGET_OBJECT, target);
    /* pw_stream_new_simple takes ownership of props. */
    c->stream = pw_stream_new_simple(pw_thread_loop_get_loop(c->loop),
                                     "Sayari Sauti Capture", props, &stream_events, c);
    if (!c->stream) {
        pw_thread_loop_unlock(c->loop);
        goto fail;
    }
    uint8_t param_buf[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(param_buf, sizeof(param_buf));
    struct spa_audio_info_raw fmt = SPA_AUDIO_INFO_RAW_INIT(
        .format = SPA_AUDIO_FORMAT_F32,
        .rate = 48000,
        .channels = 2
    );
    const struct spa_pod *params[1] = {
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &fmt)
    };
    int rc = pw_stream_connect(c->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
        PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_RT_PROCESS, params, 1);
    pw_thread_loop_unlock(c->loop);
    if (rc < 0) goto fail;
    return c;
fail:
    sauti_capture_stop(c);
    return NULL;
}
void sauti_capture_record_enable(SautiCapture *c,int enabled){
    if(!c||!c->record_ring||!c->loop)return;
    if(!c->loop_started)return;
    pw_thread_loop_lock(c->loop);
    sauti_record_enable(c->record_ring,enabled);
    pw_thread_loop_unlock(c->loop);
}
uint32_t sauti_capture_sample_rate(const SautiCapture *c){
    return c?atomic_load_explicit(&c->negotiated_rate,memory_order_acquire):0;
}
int sauti_capture_status(const SautiCapture *c) {
    return c ? atomic_load_explicit(&c->state, memory_order_acquire) : -1;
}
void sauti_capture_stop(SautiCapture *c) {
    if (!c) return;
    /* Stop callback thread before destroying stream/ring-backed callback state. */
    if (c->loop) {
        if (c->loop_started) pw_thread_loop_stop(c->loop);
        if (c->stream) pw_stream_destroy(c->stream);
        pw_thread_loop_destroy(c->loop);
    }
    pw_deinit();
    free(c);
}

/* The only place where the common backend contract knows PipeWire. */
static void *pipewire_open(SautiStereoRing *stereo,SautiAudioRing *mono,
                           SautiRecordRing *record,int monitor,const char *target){
    return sauti_capture_start_recording(stereo,mono,record,monitor,target);
}
static int pipewire_status(const void *handle){
    return sauti_capture_status(handle);
}
static uint32_t pipewire_rate(const void *handle){
    return sauti_capture_sample_rate(handle);
}
static void pipewire_record(void *handle,int enabled){
    sauti_capture_record_enable(handle,enabled);
}
static void pipewire_close(void *handle){
    sauti_capture_stop(handle);
}
const SautiAudioBackendOps sauti_pipewire_ops={
    pipewire_open,pipewire_status,pipewire_rate,pipewire_record,pipewire_close
};
