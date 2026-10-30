#include "sauti_transport.h"
SautiTransportAction sauti_transport_command(int button,int loaded,int playing,int paused){
    if(button==8)return SAUTI_ACTION_STOP;
    if(!loaded)return SAUTI_ACTION_NONE;
    if(button==3)return playing&&!paused?SAUTI_ACTION_PAUSE:SAUTI_ACTION_NONE;
    if(button==2){
        if(paused)return SAUTI_ACTION_RESUME;
        if(!playing)return SAUTI_ACTION_PLAY;
    }
    return SAUTI_ACTION_NONE;
}
SautiAfterTrack sauti_transport_finished(int was_playing,int playing,int loaded,
                                        int repeat,int queued,int shuffle){
    if(!was_playing||playing||!loaded)return SAUTI_AFTER_NONE;
    if(repeat)return SAUTI_AFTER_REPEAT;
    if(queued>0)return SAUTI_AFTER_QUEUE;
    if(shuffle)return SAUTI_AFTER_SHUFFLE;
    return SAUTI_AFTER_NONE;
}
