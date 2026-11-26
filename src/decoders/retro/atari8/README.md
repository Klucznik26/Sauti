# Atari8 research core

Experimental independent POKEY model: four pure-tone channels, volume-only,
28/114 master-clock divisors, fast clocks on channels 1/3, and steady-state
16-bit linked periods on channels 2/4. Linked low timers continue to
clock their waveforms and IRQ/filter paths: the first period is based on
the low byte, later pulses are 256 ticks apart until the pair reloads.
The fast high timer fires three modeled cycles after the final low pulse;
slow linked pulses are approximated as simultaneous on the final tick.
Slow periods are N+1 selected ticks, fast periods
are N+4 (8-bit) or N+7 (16-bit) master cycles. Counter capacity includes
65536 slow ticks and 65542 fast cycles. STIMER, initialization and AUDCTL
mode transitions and exact timer pipeline/IRQ phase are simplified;
analog output and CPU emulation are not implemented.
IRQEN/IRQST now model latched timer 1/2/4 interrupts, active-low status,
selective acknowledgment by disabling sources, and a level IRQ signal.
Timer 3 does not generate an IRQ. Timer 1 also generates IRQs when
linked to timer 2. Other IRQ sources
(serial, keyboard and break) are not implemented and enabling them is
rejected. IRQ assertions occur at modeled timer events, not yet at exact
hardware propagation times. Tests cover masking, simultaneous pending
sources, repeated reads, re-enabling, fast and linked high timers.
High-pass XOR latches on channels 1/2 are clocked by timers 3/4.
The roughly 1.5-cycle hardware propagation is rounded to two cycles;
simultaneous events use the updated lower-channel waveform. Linked
timer 3 also clocks filter 1. STIMER sets raw output
flip-flops to one, and channels 1/2 have inverted unfiltered polarity.
Volume-only overrides the filter. Tests cover both filters, polarity,
volume override, and the simplified propagation delay.
All AUDC waveform selections now have a digital model. Four LFSRs run
at master-clock speed with per-channel sample delays. The 9/17-bit modes
currently use separate registers; physical mode switching and absolute
noise phase are not modeled. SKCTL ($D20F) bits 0/1 control the modeled
initialization state: polynomial feedback is forced to one, slow clocks
stop, and fast timers keep advancing. RANDOM ($D20A read) exposes the
top eight bits of the selected long polynomial. The initial slow-clock
pulses after release are approximated at 19/78 cycles, without the
hardware timer/IRQ pipeline. Reset starts in active playback mode (3).
SKCTL bits 3–7 are rejected; keyboard, serial and potentiometer hardware
are not implemented. Tests cover initialization, slow/fast timer
behavior, RANDOM settling, and the documented 9-bit scanline sequence.
The sound subsystem now contains one or two POKEYs with a common
master-cycle advance and independent register, filter and timer state.
In stereo, address bit A4 selects the primary ($D200) or secondary
($D210) chip in the $D2xx page; mono mirrors all 16 registers throughout
the page. The primary chip supplies left audio and CPU IRQ; the secondary
supplies right audio and its IRQ is not routed to the CPU in the standard
stereo modification. No analog mixing or nonstandard stereo wiring is implemented. Chip limitations above apply to both instances.
The internal interface is not registered as a decoder. Tests validate
the stated subset, not hardware equivalence.

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

Stereo subsystem test:

    cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
      src/decoders/retro/atari8/atari8_pokey.c \
      src/decoders/retro/atari8/atari8_core.c tests/test_atari8_core.c \
      -o build/test_atari8_core
    ./build/test_atari8_core

Streaming PCM: atari8_pcm_feed integrates each master-cycle digital
level over the exact rational sample boundary. The rate range is
8–192 kHz, with 16-bit mono or interleaved stereo frames. A diagnostic
unipolar output is available; the listening mode includes an independent
first-order DC blocker per channel. Feed the level before advancing the
master cycle. This is a boxcar integrator, not a full anti-aliasing
resampler or a measured analog POKEY model. Tests check frame counts,
fractional cycle boundaries, stereo isolation, DC settling and generated
tones. No external source or music data is used.

    cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
      src/decoders/retro/atari8/atari8_pokey.c \
      src/decoders/retro/atari8/atari8_core.c \
      src/decoders/retro/atari8/atari8_pcm.c tests/test_atari8_pcm.c \
      -o build/test_atari8_pcm
    ./build/test_atari8_pcm

Build the original synthetic listening demo:

    cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
      src/decoders/retro/atari8/atari8_pokey.c \
      src/decoders/retro/atari8/atari8_core.c \
      src/decoders/retro/atari8/atari8_pcm.c tests/demo_atari8_wav.c \
      -o build/demo_atari8_wav
    ./build/demo_atari8_wav build/atari8-demo.wav

No production build or decoder registry changes in this milestone.

SAP-R experimental reader: CRLF header ending in an empty line, 9 bytes
per mono interval or 18 bytes (left then right register group) per stereo
interval. Default FASTPLAY 312 PAL / 262 NTSC, each scanline 114 cycles.
The empty-line boundary is confirmed against the public saprtools
saprplay/test.sapr file (not included or copied). Stereo group order is
a working assumption pending an external stereo fixture. Source:
https://asap.sourceforge.net/sap-format.html .
TIME is informational; payload length determines EOF, no repeat or seek.
Only one song, no executable SAP types, no arbitrary tag extensions.
Unmodeled POKEY modes are reported as unsupported, not silently rendered.
The converter is a diagnostic tool, not a registered production decoder.
No external decoder or player code is included.

NMOS 6502 milestone 1: independent CPU state, callback bus, data transfer
instructions (all official LDA/LDX/LDY/STA/STX/STY modes), transfers,
index increment/decrement, flags, conditional branches, JMP/JSR/RTS,
PHA/PHP/PLA/PLP and NOP. Each bus access counts a cycle, including
documented dummy reads for the implemented subset. Unsupported opcodes
stop explicitly. Deterministic entry is not a hardware RESET. Arithmetic and logic (ADC/SBC, ORA/AND/EOR, CMP/CPX/CPY, BIT) now include
all official addressing modes. NMOS BCD adjustment and flag behavior are
implemented; binary ADC/SBC tests exhaust 262144 input combinations and
valid BCD tests exhaust 40000 combinations, with selected NMOS flag and
invalid-digit cases. Full invalid-BCD external validation is pending.
All 151 official opcodes are now implemented, including ASL/LSR/ROL/ROR,
INC/DEC, BRK and RTI. NMOS memory RMW emits old and new bus writes.
IRQ level and NMI assertion edges are serviced at instruction boundaries;
precise interrupt polling delays, vector hijacking, hardware RESET,
undocumented opcodes and Atari bus integration remain pending.
No copied CPU code.
Reference: https://www.dusted.dk/pages/c64/C64-programming/files/6502%20Instruction%20Set.html
Tests use original synthetic instructions and bus traces. An external
functional runner accepts the unmodified 64 KiB Klaus Dormann NMOS
6502_functional_test.bin from the separate test checkout (entry $0400,
success trap $3469 for the distributed binary). External test data is
not bundled. See https://github.com/Klaus2m5/6502_65C02_functional_tests .

External validation reported by the local tester for commit 5656498:
PASS: Klaus Dormann 6502 functional test, 30646177 instructions, 96241367 cycles.
This confirms functional coverage, not exact interrupt timing.

Undocumented NMOS milestone: 84 additional opcode encodings (235/256
implemented), including SLO/RLA/SRE/RRA/DCP/ISC (42), LAX/SAX (10),
alternate NOPs (27), ANC/ALR/AXS and alternate SBC (5). All addressing
forms are covered by synthetic tests, including RMW bus writes and
indexed dummy reads. Decimal compound operations reuse ADC/SBC.
Remaining: 12 JAM encodings (currently explicit unsupported stop), ARR,
unstable immediate/load/store variants, LAS; no claim of hardware lockup
or unstable silicon behavior. Reference: https://cc65.github.io/doc/cpus.html

Opcode map completed: 256/256 encodings are explicitly handled (244
executing instructions and 12 JAM encodings). JAM stops the player after
the opcode fetch; reinitialization clears it. This is not an electrical
lockup simulation. ARR covers binary and decimal adjustment. LAS and
masked stores (AHX/SHX/SHY/TAS) have a deterministic page-carry model;
XAA/LXA use a fixed 0xEE magic constant. These unstable instructions
vary by silicon and bus conditions; this model is not hardware-verified.
External functional test validated the earlier official instruction set,
not these undocumented instructions. Hardware RESET and exact IRQ/NMI
polling remain pending. Reference: https://cc65.github.io/doc/cpus.html

Machine milestone: CPU callbacks clock POKEY and PCM once per bus cycle,
including dummy reads/writes. RAM is flat 64 KiB except $D000-$D7FF I/O;
only implemented POKEY registers are accessible. Reads/writes happen at
the end of the modeled cycle. Primary POKEY IRQ feeds CPU. PCM is emitted
via synchronous optional callback; host idle advances sound without CPU
execution. Host calls use a synthetic $FFFF return sentinel and a cycle
budget checked per instruction. I/O faults, CPU JAM and budget exhaustion
are sticky. The final instruction may overshoot by up to seven cycles.
No OS ROM, ANTIC, GTIA, PIA, DMA, WSYNC, full interrupt timing or SAP-B
parser yet. Unknown I/O is rejected rather than silently emulated. This
is a restricted music runtime, not a complete Atari computer emulator.
Do not copy or move an initialized machine; its CPU bus points to itself.
Tests: original synthetic CPU programs write mono/stereo POKEY registers,
check sample count, cycle ordering, repeated calls and bounded failure.

SAP-B milestone: strict CRLF header, FF FF Atari executable segments with
inclusive little-endian ranges, optional segment separators, subsong
selection, PAL/NTSC, stereo and FASTPLAY. INIT receives the subsong in A;
PLAYER is called at the start of each interval and remaining cycles are
spent with CPU parked. INIT output is discarded, with hardware state
retained. INIT budget is two modeled seconds; PLAYER budget is one
interval. TIME is metadata only; no seek or automatic EOF. Loaded code
is limited to the restricted machine, with no ANTIC refresh stalls,
WSYNC/VCOUNT or other non-POKEY I/O. IRQs are sampled only while CPU runs.
Parser requires INIT and PLAYER addresses in loaded RAM segments.
The interval callback is experimental, not yet registered in Sauti.
Tests use original synthetic 6502 programs only. Format reference:
https://asap.sourceforge.net/sap-format.html

SAP-B listening diagnostic: tests/render_atari8_sapb.c writes standard
16-bit little-endian PCM WAV directly at 48 kHz, mono or stereo. It takes
an external SAP file, output path, duration (1..300 seconds, default 15)
and optional zero-based song (-1 uses DEFSONG). The final interval is
executed completely but output is capped at the requested sample count.
Errors report the stage, CPU PC, machine status and unsupported I/O
address; incomplete output is removed. Use a distinct output path.
External music remains outside the repository, e.g. ASMA:
https://asma.atari.org/ . No external decoder is linked.

ASMA compatibility milestone: the local listening diagnostic successfully
rendered Bhogoosh.sap (720000 PCM frames, 748 calls). Sixteen earlier
files failed at ALLPOT ($D208 read), possibly due to indexed-store dummy
reads; their original CPU traces have not been inspected. The POKEY read
map now includes headless POT0-7, ALLPOT, keyboard/serial idle and unused
register reads. POTGO starts a shared disconnected-input counter, which
saturates at 228 on scanline ticks (or master cycles in fast scan mode).
This is not a full analog paddle, serial or keyboard model. SKREST and
unused write $D20C are accepted; unsupported serial modes/writes and
non-timer IRQ enables still fault. The model uses the documented active
high busy bits of ALLPOT; no audio-register readback. Tests include
indexed STA dummy reads, mono/stereo register mapping and scan timing.
Reference: https://atariwiki.org/wiki/Wiki.jsp?page=AUDCTL and
https://doczz.net/doc/1531988/altirra-hardware-reference-manual .

ASMA follow-up: Atari_Style.sap rendered 720000 stereo PCM frames after
the ALLPOT change (local user report). Corrected a stale test assertion
that still expected POT0 reads to leave the result untouched; the invalid
register 16 now tests this contract. The external archive scanner
tests the first 100 sorted SAP-B files by default, one second of the
default subsong each, with a bounded subprocess timeout and CSV report.
It distinguishes valid nonzero WAV, silence, and error categories.
This measures short-render compatibility only, not music fidelity.
No external files are included.

ASMA baseline (commit 5cd9cfd, local report): 3824 SAP-B files tested
for three seconds, default subsong; 3546 nonzero WAV, 8 silence, 270
failures. Leading failure: 189 reads of $D01F. The machine now has a
headless GTIA read map (released triggers, no collisions, PAL/NTSC,
CONSOL output latch default $08); video writes are ignored except
CONSOL. Console speaker audio is not mixed. ANTIC VCOUNT reflects
master cycles / 114 and frame length; WSYNC stalls until the next
scanline boundary (approximation, not actual horizontal release phase).
POKEY and PCM advance during stalls; CPU cycles exclude stalled time.
NMIEN writes disabling both NMI sources are accepted, but enabling
either faults. No video DMA, DLI/VBI or full NMI emulation. Other
unsupported I/O remains a fault. Reference for register values:
https://www.oxyron.de/html/registers_gtia.html and
https://mads.atari8.info/library/doc/atari.html .
The scanner --retry option selects previous failures and silent WAVs
from a separate CSV; this is not a full regression scan.

ASMA retry after GTIA/ANTIC change: 209/278 nonzero, 9 silent,
60 failed. Combined with the previous 3546 nonzero files, the
estimated totals are 3755 nonzero and 3764 rendered of 3824, assuming
no regressions in the files not retested. This is not a full rerun.
The machine now models absent PBI/ECI and cartridge hardware at
$D100-$D1FF and $D500-$D7FF as XL-style pulled-up reads and ignored
writes. SEROUT stores a byte without transmitting; no SIO device,
serial timing, two-tone mode, or serial IRQ is emulated. Unsupported
write diagnostics include the attempted byte, so NMIEN/SKCTL failures
can be investigated without guessing. Active NMI sources still fault.
Reference: Altirra Hardware Reference Manual, bus pullups, PBI and
POKEY serial output. No emulator source imported.

ASMA retry2: 19 nonzero, 9 silent, 41 failures out of 69. Reported
SKCTL failure values: $8B (12 files), $20 (1 file); NMIEN values
$40/$60/$C0/$80 (26 files). SKCTL force-break two-tone with serial
clock disabled now reloads timer 1 on timer 2 underflow without
generating a spurious audio pulse or IRQ. Force-break alone is also
accepted with no audio impact. This is a steady-state approximation
of resynchronization, not the hardware propagation pipeline. Other
two-tone and serial clock modes, including $20, are still unsupported.
Tests cover suppression of timer 1 pulses and IRQs, timer 2 output and
exit from two-tone. Reference: Altirra Hardware Reference Manual,
section Two-tone mode, https://doczz.net/doc/1531988/altirra-hardware-reference-manual .
NMIEN remains a separate task.

ASMA retry3 (local report for 39fec39): 12 nonzero, 9 silent, 29
failures of 50; all seven regression tests passed. SAP host scheduling
is now explicitly selected for type B. Per the published SAP execution
model, PLAYER/FASTPLAY drives playback rather than ANTIC interrupts.
NMIEN writes latch the source bits in this profile but do not dispatch
ANTIC NMI. NMIST reports approximate VBI status from scanline 248
independent of enable; NMIRES clears it. DLI, precise VBI timing and
real ANTIC NMI delivery are not implemented. The default restricted
machine still faults on enabled ANTIC NMI sources. This policy is
not intended for a general Atari emulator. Reference:
https://asap.sourceforge.net/sap-format.html (Execution Model).
Stereo regression now also asserts that the generated left and right
sample streams differ for the synthetic dual-POKEY fixture.

ASMA retry4 (ddc6c16): 19 nonzero, 9 silent, 10 failed of 38;
seven regression tests passed. Eight budget failures (six at PC=0000),
one parser unsupported and one SKCTL $20. Do not increase the budget
or suppress IRQ without diagnosis. Machine records the last 16
instruction-boundary snapshots and first transition to PC=0000 during
a host call; renderer prints these on budget failures. Snapshot opcode
is backing RAM, not an extra bus read; IRQ/NMI flags are sampled before
CPU stepping. A new external comparison script checks pending entries
against an installed ASAP executable, prints unsupported header and
first nonzero sample at up to 60 seconds. This is onset/compatibility
diagnostics, not waveform fidelity; no external player code imported.
