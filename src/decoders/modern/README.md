# Contemporary decoder implementations

Moved from `src/` into this directory with no intentional codec algorithm changes:
- `sauti_decoder_wav.c`
- `sauti_decoder_mp3.c`
- `sauti_decoder_flac.c`
- `sauti_decoder_vorbis.c`
- `sauti_decoder_opus.c`
- `sauti_decoder_midi.c`

Makefile sources were adjusted, as were relative third-party header paths in the moved files. Public headers remain in `include/`. The core decoder registry (`src/sauti_decoder.c`) remains untouched.

**Verification status:** GitHub source changes only; build and test suite have not been executed. The next step is to run `make test` and `make window-pipewire` from the Sauti directory on a suitable Linux build host and address any remaining references.
