#ifndef SAUTI_DECODER_MOD_H
#define SAUTI_DECODER_MOD_H
#include "maktabasauti/maktabasauti_codec.h"
/* ProTracker / SoundTracker: 15 i 31 instrumentow, 1..99 kanalow.
 * Osobny replay, nie adapter PCM.  Deskryptor jest w rejestrze odtwarzacza.
 * Domyslny dzwiek to ProTracker 1.1.  mod.tracker: pt10, ust, st2, st9, tjc, st23, st25.
 * Mod's Grave to drugi deskryptor.  Wybiera go rozszerzenie .wow, nie test rozmiaru. */
extern const maktabasauti_codec_t maktabasauti_mod_desc;
extern const maktabasauti_codec_t maktabasauti_mod_wow_desc;
#endif
