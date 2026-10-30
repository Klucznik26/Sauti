#ifndef SAUTI_PLAYBACK_BACKEND_H
#define SAUTI_PLAYBACK_BACKEND_H
/* Shared playback contract. The platform adapter owns decoding and output. */
typedef struct {
    void *context;
    const struct SautiPlaybackOps *ops;
} SautiPlaybackBackend;
typedef struct SautiPlaybackOps {
    int (*load)(void *context,const char *path);
    void (*play)(void *context);
    void (*pause)(void *context);
    void (*stop)(void *context);
    void (*seek)(void *context,double seconds);
    void (*mute)(void *context,int muted);
    int (*playing)(const void *context);
    int (*paused)(const void *context);
    double (*position)(const void *context);
    double (*duration)(const void *context);
} SautiPlaybackOps;
int sauti_playback_valid(const SautiPlaybackBackend *backend);
int sauti_playback_load(SautiPlaybackBackend *backend,const char *path);
void sauti_playback_play(SautiPlaybackBackend *backend);
void sauti_playback_pause(SautiPlaybackBackend *backend);
void sauti_playback_stop(SautiPlaybackBackend *backend);
void sauti_playback_seek(SautiPlaybackBackend *backend,double seconds);
void sauti_playback_mute(SautiPlaybackBackend *backend,int muted);
int sauti_playback_playing(const SautiPlaybackBackend *backend);
int sauti_playback_paused(const SautiPlaybackBackend *backend);
double sauti_playback_position(const SautiPlaybackBackend *backend);
double sauti_playback_duration(const SautiPlaybackBackend *backend);
#endif
