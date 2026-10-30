#ifndef SAUTI_PLAYBACK_TIME_H
#define SAUTI_PLAYBACK_TIME_H
/* Platform-neutral playback timeline math. Backend supplies seconds. */
int sauti_playback_percent(double position,double duration);
double sauti_playback_seek_seconds(int percent,double duration);
#endif
