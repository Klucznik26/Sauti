#include "sauti_app_state.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    SautiAppState state={0};
    assert(!state.repeat&&!state.shuffle&&!state.was_playing);
    sauti_app_toggle(&state,SAUTI_TOGGLE_REPEAT);
    sauti_app_toggle(&state,SAUTI_TOGGLE_SHUFFLE);
    assert(state.repeat&&state.shuffle);
    sauti_app_toggle(&state,SAUTI_TOGGLE_REPEAT);
    assert(!state.repeat&&state.shuffle);
    sauti_app_update_playback(&state,1);
    assert(state.was_playing);
    sauti_app_stopped(&state);
    assert(!state.was_playing);
    sauti_app_update_playback(&state,0);
    assert(!state.was_playing);
    sauti_app_toggle(NULL,SAUTI_TOGGLE_REPEAT);
    puts("Sauti portable app state tests passed");
    return 0;
}
