#include "sauti_playback_time.h"
#include <math.h>
int sauti_playback_percent(double position,double duration){
    if(!isfinite(position)||!isfinite(duration)||duration<=0.0)return 0;
    if(position<=0.0)return 0;
    if(position>=duration)return 100;
    return (int)(position*100.0/duration+0.5);
}
double sauti_playback_seek_seconds(int percent,double duration){
    if(!isfinite(duration)||duration<=0.0)return 0.0;
    if(percent<0)percent=0;
    if(percent>100)percent=100;
    return duration*(double)percent/100.0;
}
