# Sauti decoder organization

This is an additive migration scaffold, not an active build path yet.

- `common/`: reusable byte readers, endian conversion, PCM handling, chunk scanners and format validation (without OS dependencies).
- `modern/`: compressed and contemporary format decoders.
- `retro/`: vintage computer and historical formats. Group by format origin, not runtime OS.
- `containers/`: container parsing reusable by several decoders, including RIFF/WAVE/BWF and IFF/AIFF/8SVX.

Existing `src/sauti_decoder_*.c` and matching `include/` headers remain unchanged until moved in small tested steps. Keep a single implementation of each format. This layout does **not** imply that the current decoders are ASM; most existing decoder frontends are C.

New ASM decoder components should expose the existing Sauti decoder contract through a small, documented ABI boundary. The first planned retro implementation is AVR in `retro/atari/`; do not register a decoder before there is code and a passing test. No kernel or Linux dependencies in decoding code.
