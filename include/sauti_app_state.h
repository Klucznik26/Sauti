#ifndef SAUTI_APP_STATE_H
#define SAUTI_APP_STATE_H
/* No SDL, PipeWire, filesystem or platform dependencies. */
typedef struct {
    int repeat;
    int shuffle;
    int was_playing;
} SautiAppState;
enum { SAUTI_TOGGLE_REPEAT=1, SAUTI_TOGGLE_SHUFFLE=2 };
void sauti_app_toggle(SautiAppState *state,int setting);
void sauti_app_stopped(SautiAppState *state);
void sauti_app_update_playback(SautiAppState *state,int playing);
#endif
