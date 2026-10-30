#include "sauti_playback_backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {int played,paused,stopped,muted,loaded;double seconds;} Fake;
static int load(void *p,const char *path){((Fake *)p)->loaded=!!path;return 0;}
static void play(void *p){((Fake *)p)->played++;}
static void pause_it(void *p){((Fake *)p)->paused++;}
static void stop(void *p){((Fake *)p)->stopped++;}
static void seek(void *p,double s){((Fake *)p)->seconds=s;}
static void mute_it(void *p,int m){((Fake *)p)->muted=m;}
static int playing(const void *p){return ((const Fake *)p)->played>0;}
static int paused(const void *p){return ((const Fake *)p)->paused>0;}
static double position(const void *p){return ((const Fake *)p)->seconds;}
static double duration(const void *p){(void)p;return 200.0;}
int main(void){
    Fake f={0};
    const SautiPlaybackOps ops={load,play,pause_it,stop,seek,mute_it,playing,paused,position,duration};
    SautiPlaybackBackend b={&f,&ops};
    assert(sauti_playback_valid(&b));
    assert(sauti_playback_load(&b,"test.mp3")==0 && f.loaded);
    sauti_playback_play(&b);
    sauti_playback_pause(&b);
    sauti_playback_stop(&b);
    sauti_playback_seek(&b,37.0);
    sauti_playback_mute(&b,1);
    assert(sauti_playback_playing(&b) && sauti_playback_paused(&b));
    assert(sauti_playback_position(&b)==37.0 && sauti_playback_duration(&b)==200.0);
    assert(f.stopped==1 && f.muted==1);
    puts("Sauti playback backend contract tests passed");
    return 0;
}
