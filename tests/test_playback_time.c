#include "sauti_playback_time.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    assert(sauti_playback_percent(0.0,100.0)==0);
    assert(sauti_playback_percent(25.0,100.0)==25);
    assert(sauti_playback_percent(130.0,100.0)==100);
    assert(sauti_playback_percent(-5.0,100.0)==0);
    assert(sauti_playback_percent(2.0,0.0)==0);
    assert(sauti_playback_seek_seconds(-1,200.0)==0.0);
    assert(sauti_playback_seek_seconds(50,200.0)==100.0);
    assert(sauti_playback_seek_seconds(200,200.0)==200.0);
    puts("Sauti timeline tests passed");
    return 0;
}
