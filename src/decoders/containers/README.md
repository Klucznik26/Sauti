# Reusable container parsers

IFF-family chunks support AIFF/AIFC, 8SVX, 16SV, MAUD and related formats. Uncompressed 16SV is decoded by `../retro/amiga/maktabasauti_16sv.c`. MAUD PCM and G.711 are decoded by `../retro/amiga/maktabasauti_maud.c`. SAMP linear PCM with no MIDI map is decoded by `../retro/amiga/maktabasauti_samp.c`. RIFF-family handling supports WAV/BWF; Sony Wave64 is related but uses a distinct GUID-based layout. Container recognition is not itself a claim of codec support. Keep bounds checks and chunk padding rules explicit.
