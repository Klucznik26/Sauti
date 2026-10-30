#include "sauti_app_state.h"
void sauti_app_toggle(SautiAppState *state,int setting){
    if(!state)return;
    if(setting==SAUTI_TOGGLE_REPEAT)state->repeat=!state->repeat;
    else if(setting==SAUTI_TOGGLE_SHUFFLE)state->shuffle=!state->shuffle;
}
void sauti_app_stopped(SautiAppState *state){
    if(state)state->was_playing=0;
}
void sauti_app_update_playback(SautiAppState *state,int playing){
    if(state)state->was_playing=!!playing;
}
