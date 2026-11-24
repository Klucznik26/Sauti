#ifndef SAUTI_DECODER_MOD_H
#define SAUTI_DECODER_MOD_H
#include "maktabasauti/maktabasauti_codec.h"
/* ProTracker / SoundTracker: 15 i 31 instrumentow, 1..32 kanalow.
 * Osobny replay, nie adapter PCM.  Deskryptor jest w rejestrze odtwarzacza.
 * Domyslny dzwiek to ProTracker 1.1.  mod.tracker pt10, ust, st2 i st9 sa opcjami hosta. */
extern const maktabasauti_codec_t maktabasauti_mod_desc;
#endif
