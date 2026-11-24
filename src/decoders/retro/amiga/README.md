# Amiga audio

Candidates: 8SVX, 16SV, MAUD, SoundTracker MOD, MED, OKT, AHX, HivelyTracker and other well-documented replay formats. Share IFF parsing with `../../containers/`. Tracker modules and synthetic music are not ordinary PCM files and should use a separate replay interface.

`maktabasauti_svx.c` decodes IFF 8SVX. `maktabasauti_mod.c` is an original replay for the MOD family: 31-instrument signatures (`M.K.`, `M&K!`, `M!K!`, `FLT4`, `xCHN`, `xxCH`, `xxCN`, `TDZ1`–`TDZ3`, `CD81`, `OCTA`, `OKTA`), StarTrekker `FLT8` as a pair of 4-channel patterns, and the signature-less 15-instrument header. The replay mixes Paula periods. Stereo pan repeats the Amiga pairs, left right right left. `Cxx`, `Fxx`, arpeggio, portamento, vibrato, volume slide and sample offset change playback. `Bxx`, `Dxx` and `Exx` stay on the cell. Finetune is kept and does not change the written period. The song plays once and stops. This decoder is not registered in the player. The mixer is C until the render is measured; it is not assembly yet.

No other Amiga format is claimed.
