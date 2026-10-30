#ifndef SAUTI_MP3_SDL_PLAYER_H
#define SAUTI_MP3_SDL_PLAYER_H
/* Linux SDL output adapter; MP3 decoding remains portable. */
#include <stddef.h>
typedef struct SautiMp3SdlPlayer SautiMp3SdlPlayer;
SautiMp3SdlPlayer *sauti_mp3_sdl_open(const char *path);
void sauti_mp3_sdl_destroy(SautiMp3SdlPlayer *p);
void sauti_mp3_sdl_pump(SautiMp3SdlPlayer *p);
void sauti_mp3_sdl_play(SautiMp3SdlPlayer *p);
void sauti_mp3_sdl_pause(SautiMp3SdlPlayer *p);
void sauti_mp3_sdl_stop(SautiMp3SdlPlayer *p);
void sauti_mp3_sdl_seek(SautiMp3SdlPlayer *p,double seconds);
void sauti_mp3_sdl_mute(SautiMp3SdlPlayer *p,int muted);
int sauti_mp3_sdl_playing(const SautiMp3SdlPlayer *p);
int sauti_mp3_sdl_paused(const SautiMp3SdlPlayer *p);
double sauti_mp3_sdl_position(const SautiMp3SdlPlayer *p);
double sauti_mp3_sdl_duration(const SautiMp3SdlPlayer *p);
#endif
