#include "sauti_playback_backend.h"
int sauti_playback_valid(const SautiPlaybackBackend *b){
    return b && b->context && b->ops && b->ops->load && b->ops->play &&
        b->ops->pause && b->ops->stop && b->ops->seek && b->ops->mute &&
        b->ops->playing && b->ops->paused && b->ops->position && b->ops->duration;
}
int sauti_playback_load(SautiPlaybackBackend *b,const char *p){
    return sauti_playback_valid(b) && p && *p ? b->ops->load(b->context,p):-1;
}
void sauti_playback_play(SautiPlaybackBackend *b){if(sauti_playback_valid(b))b->ops->play(b->context);}
void sauti_playback_pause(SautiPlaybackBackend *b){if(sauti_playback_valid(b))b->ops->pause(b->context);}
void sauti_playback_stop(SautiPlaybackBackend *b){if(sauti_playback_valid(b))b->ops->stop(b->context);}
void sauti_playback_seek(SautiPlaybackBackend *b,double s){if(sauti_playback_valid(b))b->ops->seek(b->context,s);}
void sauti_playback_mute(SautiPlaybackBackend *b,int v){if(sauti_playback_valid(b))b->ops->mute(b->context,v);}
int sauti_playback_playing(const SautiPlaybackBackend *b){return sauti_playback_valid(b)?b->ops->playing(b->context):0;}
int sauti_playback_paused(const SautiPlaybackBackend *b){return sauti_playback_valid(b)?b->ops->paused(b->context):0;}
double sauti_playback_position(const SautiPlaybackBackend *b){return sauti_playback_valid(b)?b->ops->position(b->context):0.0;}
double sauti_playback_duration(const SautiPlaybackBackend *b){return sauti_playback_valid(b)?b->ops->duration(b->context):0.0;}
