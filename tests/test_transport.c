#include "sauti_transport.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    assert(sauti_transport_command(2,1,0,0)==SAUTI_ACTION_PLAY);
    assert(sauti_transport_command(2,1,1,1)==SAUTI_ACTION_RESUME);
    assert(sauti_transport_command(2,1,1,0)==SAUTI_ACTION_NONE);
    assert(sauti_transport_command(3,1,1,0)==SAUTI_ACTION_PAUSE);
    assert(sauti_transport_command(3,1,1,1)==SAUTI_ACTION_NONE);
    assert(sauti_transport_command(2,0,0,0)==SAUTI_ACTION_NONE);
    assert(sauti_transport_command(8,0,0,0)==SAUTI_ACTION_STOP);
    assert(sauti_transport_finished(1,0,1,1,3,1)==SAUTI_AFTER_REPEAT);
    assert(sauti_transport_finished(1,0,1,0,3,1)==SAUTI_AFTER_QUEUE);
    assert(sauti_transport_finished(1,0,1,0,0,1)==SAUTI_AFTER_SHUFFLE);
    assert(sauti_transport_finished(1,0,1,0,0,0)==SAUTI_AFTER_NONE);
    assert(sauti_transport_finished(1,1,1,1,1,1)==SAUTI_AFTER_NONE);
    assert(sauti_transport_finished(0,0,1,1,1,1)==SAUTI_AFTER_NONE);
    puts("Sauti portable transport tests passed");
    return 0;
}
