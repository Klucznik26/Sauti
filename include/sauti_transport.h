#ifndef SAUTI_TRANSPORT_H
#define SAUTI_TRANSPORT_H
/* Portable playback policy. The platform adapter executes these actions
 * through SDL_mixer, WASAPI, AyoAPI or another audio backend. */
typedef enum {
    SAUTI_ACTION_NONE=0,
    SAUTI_ACTION_PLAY,
    SAUTI_ACTION_RESUME,
    SAUTI_ACTION_PAUSE,
    SAUTI_ACTION_STOP
} SautiTransportAction;
typedef enum {
    SAUTI_AFTER_NONE=0,
    SAUTI_AFTER_REPEAT,
    SAUTI_AFTER_QUEUE,
    SAUTI_AFTER_SHUFFLE
} SautiAfterTrack;
SautiTransportAction sauti_transport_command(int button,int loaded,int playing,int paused);
SautiAfterTrack sauti_transport_finished(int was_playing,int playing,int loaded,
                                        int repeat,int queued,int shuffle);
#endif
