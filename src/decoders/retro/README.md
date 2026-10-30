# Retro audio

Historical formats are organized by originating platform, not where Sauti executes them.

Current scaffold: `atari/`, `amiga/`. Add further platform directories only when first implemented. Share common PCM/conversion and container code. Tracker/replay and emulation-based formats need separate execution engines from sample-file decoders.

Historical provenance, known variants, unsupported modes and test evidence should be recorded per decoder. New decoding logic should be written in assembly wherever feasible.
