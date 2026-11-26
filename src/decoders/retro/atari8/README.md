# Atari8 research core

Initial milestone: independent experimental POKEY model for four pure-tone
channels, volume-only, and the 28/114 master-clock divisors. The internal
interface is not registered as a decoder. The level is a linear digital sum,
not PCM. STIMER and initialization are simplified; noise, joined counters,
high clocks, filters, IRQs, exact register timing, analog output and CPU
emulation are not implemented. Tests validate the stated subset, not
hardware equivalence.

Implementation policy: original code based on hardware and format
documentation; ASAP and Altirra are external behavioral references only.
Do not incorporate third-party player routines, tables or ROM images
without a separate provenance and license review. External test music
and tools remain outside the repository until redistribution rights are
confirmed. Intended distribution remains closed-source freeware.

Technical reference: Altirra Hardware Reference Manual, POKEY chapter
(https://virtualdub.org/altirra.html). Compare subsequent milestones against
independent synthetic traces and hardware/emulator tests.

Standalone test (from repository root):

    mkdir -p build
    cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
      src/decoders/retro/atari8/atari8_pokey.c tests/test_atari8_pokey.c \
      -o build/test_atari8_pokey
    ./build/test_atari8_pokey

No production build or decoder registry changes in this milestone.
