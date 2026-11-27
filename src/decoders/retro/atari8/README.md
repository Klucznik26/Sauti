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

Pending comparison after ff3a519: all nine previously silent entries
match ASAP silence/onset over 15 seconds (maximum onset difference
13 ms, not a waveform comparison). Ten files still fail. Six enter
PC=0000 through absolute JMP with IRQ/NMI inactive; this is not evidence
of an interrupt fault. Two other PLAYER routines exhaust the interval.
Impossible_but_Real has a standard stereo TYPE B header (no COVOX);
parser rejected its segment overlap with I/O. SAP image loading now
copies backing RAM without bus writes, including segments crossing I/O,
while CPU accesses keep the existing map and MMIO entry points are
rejected. SKCTL $20 is accepted only as a headless initialization
case, not general serial support. Trace now includes operand bytes,
A/X/Y and the instruction history immediately before the first jump
to zero. No change to PLAYER timing yet. References: SAP format
specification https://asap.sourceforge.net/sap-format.html and Altirra
Hardware Reference Manual, SKCTL register layout.

Pending comparison after 094a1f8: Impossible_but_Real now renders
stereo with 21 ms onset (ASAP 23 ms); Zador_II advances from SKCTL $20
to $96. All seven tests passed. SKCTL async receive, serial mode 001,
now holds timers 3/4 reset while disconnected input waits for a start
bit; channels 1/2 and volume-only output remain available. No serial
data transfer. Reference: Altirra Hardware Reference Manual section
Asynchronous receive mode. SAP-B overlong PLAYER calls now finish
without reentry, skip deadlines on the existing FASTPLAY grid, and
report skipped deadlines. A bounded watchdog (up to eight intervals,
normally capped at two emulated seconds, at least one interval) still
rejects nonreturning calls. This is a compatibility policy for music
not conforming to the ideal timing contract, not verified equivalence
to ASAP scheduling. Six absolute JMP $0000 failures remain under
investigation; no special treatment of those jumps is introduced.

Pending comparison after f04fd9d: 7 regression tests passed.
Zador_II and Decision_in_the_Desert now render 15 s (onsets 1 vs 20 ms
and 201 vs 202 ms respectively). Seven files still fail, including
six JMP $0000 cases. SAP execution specification requires nine
refresh cycles per scanline even with display DMA off. SAP-profile
CPU bus accesses now stall at phases 25,29,...,57 (documented ANTIC
refresh slots); audio/master time continues, CPU cycles exclude
stalls. Both reads and writes stall in this approximation; real
write-cycle deferral is not modeled. No refresh is added during
CPU-idle or WSYNC time. General restricted machine is unchanged.
Regression: 106 CPU cycles span 115 master cycles on an aligned line.
The external comparison script now prints renderer deadline counts
and, on budget failure, original segment ranges, entry-point bytes
and possible absolute stores to a failing JMP operand. These pattern
matches are not proof of execution. No guest image is altered or
copied into the repository. Sources:
https://asap.sourceforge.net/sap-format.html (Timing) and Altirra
Hardware Reference Manual (Memory refresh DMA). The effect on
real songs and the seven remaining failures needs validation.

Pending comparison after a8eac54: all seven regression tests passed;
Zador_II onset now matches ASAP at 20 ms. Seven files still fail.
Local inspection of two user-supplied SAP images identifies an original
VBI chaining idiom: a routine saves VVBLKI ($0224/$0225) into an
absolute JMP operand, but INIT does not install that VBI. The direct
PLAYER call later jumps through its zero operand after restoring the
scratch area. SAP-B formally requires RTS; this is a narrow legacy
host ABI adaptation, not a 6502 behavior. After INIT the decoder
recognizes the two absolute load/store pairs and zero JMP operand,
without patching the file or guest memory. A jump to zero from a
recognized site can terminate a call only with the synthetic stack
frame intact, zero old vector and zero RAM[0], and no interrupt
dispatch. Such returns are counted separately. Arbitrary JMP $0000
and general machine operation remain strict. Synthetic positive and
negative regressions are included. The two supplied tracks and
independent instruction-level probe are not stored in the repository.
Chromatics also loops in its first PLAYER call; its cause remains
open. No claim of waveform fidelity or complete archive validation.
Reference: https://asap.sourceforge.net/sap-format.html (TYPE B).

Pending comparison after 7009ca1: all seven synthetic tests passed,
but all six real VBI chain failures remained. Independent execution
probe of Tinory_Quest_Morning confirmed that after INIT, VVBLKI is
the current PLAYER address ($9569), while the saved previous JMP
operand is $0000. The previous guard incorrectly conflated these
two values. Permit current VVBLKI to equal the current host call
entry (or zero); retain all existing recognized-site, stack, zero
operand, RAM[0] and interrupt guards. Added synthetic regression
for the installed-current-vector case and retained unrelated-vector
negative regression. No claim of successful real-file render until
retested. Chromatics remains a separate investigation.

Pending comparison after fb2fbc7: seven regression tests passed.
All six legacy VBI-chain cases now produce 15 s, each with 748
calls/legacy returns, zero skipped deadlines and onset within 1 ms
of ASAP. Only Chromatics remains unresolved in the pending list;
3823/3824 is an incremental estimate, not a fresh full scan.
Independent instruction-level probe of the supplied Chromatics file
observed a non-returning command loop during the first PLAYER call:
the guest's command interpreter repeatedly jumps to the same music
offset ($06F8) without POKEY writes. The unmodified input contains
that loop. More CPU budget is not a fix. Reference ASAP emits
nonzero PCM, but its sustained signal and exact execution path are
not yet established. No guest-specific or CPU changes introduced
here. Comparison utility adds optional path filter, input SHA256
and per-second PCM peak/RMS/nonzero counts to aid external
reference diagnosis. Reference remains a separate executable.

### Full SAP-B archive validation (fb2fbc7)

All seven regression suites passed. User-run complete repeat scan of
3824 ASMA SAP-B files, 3 seconds at 48000 Hz, default subsong:
3814 nonzero WAVs, 9 silent WAVs, 1 runtime budget failure (Chromatics).
3823/3824 (99.97%) conversion success, no additional failures. Report:
build/sapb-final-scan.csv (local, not committed). The nine initial
silences were previously checked against ASAP at 15 seconds.
This is not waveform fidelity or all-subsong validation.
Decision_in_the_Desert rendered 15 seconds with 114 skipped deadlines;
tempo fidelity remains unverified. Next diagnostic: obtain reference
CPU trace using the external asapscan tool and compare the first
PLAYER call, including the branch-table target at music offset $06F8.
No CPU or player changes, guest data, or reference source included.

### Chromatics: external reference trace evidence

The user supplied the first 300000 instruction trace lines from their
separately built ASAP asapscan -c on Chromatics.sap. Analysis finds 45
entries at PLAYER $9969, 45 synthetic JSRs from $D200 and 186950 BRK
instructions at $0000. The first call starts at trace line 63392;
ASAP schedules the next call at line 73221 before the previous call
returns (SP $EB before the synthetic JSR, $E9 at PLAYER; initial PLAYER
SP $FD). At line 73381 the guest executes JMP $0000 from $97C5,
followed by BRK at line 73382. Repeated calls occur despite this
broken control flow. The guest violates the TYPE B requirement that
PLAYER return via RTS (https://asap.sourceforge.net/sap-format.html).
Sauti's bounded non-reentrant scheduler detects the failure rather
than reproducing abandoned call frames and repeated BRK dispatch.
Reference PCM onset alone does not establish valid playback; check
per-second energy via the existing external comparison utility.
Keep the budget failure visible; do not count the file as rendered or
silently patch its data. No trace, guest bytes or reference code added.
Archive result remains 3823/3824. This evidence does not establish that
all SAP-B files are correct or that every other track matches hardware.

### Experimental frame-restart scheduling

A 30-second external ASAP render of Chromatics (SHA256
 db675978c3a0837bc26eb9bd1c6ae95b412ba88153190690a9d5f83542b51ccc)
has sustained nonzero PCM in every second (RMS 1002.60 to 2766.53).
It is not merely an initial transient; musical fidelity remains unverified.
Introduce an explicit alternate scheduler, never enabled by default.
The host schedules a six-cycle synthetic call on each FASTPLAY deadline,
retaining guest registers, RAM, and stack even after an unfinished call.
BRK runs normally. Normal returns park the CPU. Abandoned calls are
counted; I/O faults and JAM still fail. Initial SP, flags and phase are
adjusted only in this mode based on the observed external trace. The
call timing and INIT return address are approximations. Each call is
bounded by the next deadline plus instruction overshoot, and no source
or guest-specific bytes are imported. Synthetic tests cover repeated
abandonment, normal return and JAM. The regular 3823/3824 policy is
unchanged. Use --frame-restart on the diagnostic renderer or comparison
script; verify the real input before considering automatic selection.

### Frame-restart real-file validation (5788a52)

All seven user-run C regression suites PASS. Chromatics renders
30 seconds / 1440000 mono PCM frames at 48000 Hz with the opt-in
scheduler. Onset 84 ms in both Sauti and ASAP. Sauti executes 1496
PLAYER calls, all abandoned at their deadline, zero skipped deadlines.
All thirty one-second blocks contain nonzero output. Sauti RMS ranges
589.68–1479.49 vs ASAP 1002.60–2766.53, with broadly similar changes
in energy over time. Output level and waveform/tempo fidelity are not
established; no gain correction is applied on the basis of one track.
Combined observed file coverage: 3823 standard + 1 explicit legacy
mode = 3824/3824, across separate runs and default subsongs. No claim
of complete SAP-B conformance or all-subsong/audio fidelity.

The optional scanner flag --retry-frame-restart retries only a PLAYER
budget failure in a new process with the alternate scheduler. CSV
retains the original failure, both diagnostics and a separate mode
column; recovered nonzero output is counted separately. Silent retry
is not classified as recovery. Standard scan remains unchanged in
playback policy. This is a diagnostic policy, not automatic production
fallback. Repeat the full scan with the flag to obtain one combined
report. The legacy mode deliberately permits abandoning guest call
frames; it must remain explicit until further validation.

### SAP-B milestone and SAP-C initial implementation

Complete user-run combined scan at df2ea28: 3824/3824 ASMA SAP-B
files rendered (3814 nonzero standard, 9 silent standard, 1 nonzero
frame-restart). Three seconds, default subsong. No conversion errors.
Not an audio-accuracy, full-duration or all-subsong certification.
Chromatics requires the explicit alternate policy; no production fallback
or gain change is introduced. Before Sauti 1.0, a detailed licensing and
provenance audit remains a release gate, including tables and dependencies.

SAP-C (CMC calling convention) now shares the B parser, segment loader,
CPU, POKEY and normal scheduler. Header MUSIC replaces INIT. Initialization
uses PLAYER+3 twice: first A=$70, X/Y music address, then A=0 and X selected
subsong. Recurring calls enter PLAYER+6. Entry arithmetic is bounded before
narrowing; missing segments and incompatible header tags are rejected.
Frame-restart remains B-only. Existing internal sapb names retained for
compatibility, renderer prints the actual type. Synthetic tests cover ABI,
subsong selection, tick and malformed input. Scanner --type C selects
real C files without changing B default. No external player code or music
included. Real C corpus and regression runs pending.
Reference: https://asap.sourceforge.net/sap-format.html (TYPE C ABI).


### Session checkpoint — 2026-11-27 (resume here)

Branch: feature/atari8. Last tested code: 5c8c277.
All seven regression executables PASS, including SAP-B/C.

| Variant | Tested ASMA files | Nonzero | Silent | Failed |
| --- | ---: | ---: | ---: | ---: |
| SAP-B | 3824 | 3815 (1 frame-restart) | 9 | 0 |
| SAP-C | 1755 | 1741 | 13 | 1 |

Both scans used three seconds, 48 kHz, default subsong. SAP-B
result is 3824/3824 (100%) with the explicit alternate scheduler
for Chromatics; SAP-C is 1754/1755 (99.94%). This is render coverage,
not full-duration, all-subsong, or waveform-fidelity validation.
SAP-C silences have not yet been compared with ASAP. The one failure
is INIT/parse 2 (unsupported); exact file/header not yet inspected.
Local reports: build/sapb-complete-scan.csv and build/sapc-full-scan.csv.
SAP-R has passed synthetic tests and a real mono listening test earlier.
SAP-D and SAP-S remain to implement.

Next session: identify the unsupported SAP-C file and compare all
14 unresolved entries against the external ASAP executable at 15 s.
Use the existing diagnostic script from the repository root:

```sh
python3 tests/compare_atari8_sapb_pending.py \
    "$HOME/Sauti-tools/atari8/asma" \
    build/sapc-full-scan.csv \
    --asap "$HOME/Sauti-tools/atari8/asap/asapconv" \
    --seconds 15
```

The current renderer already supports SAP-C. After resolving or
documenting remaining cases, rerun C and B regressions before moving
to SAP-D/S. Do not classify a nonzero waveform alone as correct music.
Future work: compare audio levels, timing and timbre, mono/dual POKEY,
full durations and all subsongs, then remaining native Atari formats.
Listening test of Chromatics confirmed Sauti is quieter than ASAP;
do not apply global gain based on a single file. Production integration
and automatic fallback policy are still pending.

Keep ASAP, ASMA and all external diagnostic/reference material outside
the distribution and repository. Mandatory detailed licensing and
provenance audit immediately before Sauti 1.0 release; resolve all
license concerns before publishing. No decoder changes in this checkpoint.

### SAP-B/C final rendering validation — 2026-11-27

- SAP-B: 3824/3824 rendered (100%).
- SAP-C: 1755/1755 rendered (100%).
- Total: 5579/5579 rendered.
- SAP-B: 9 initial silences, 1 frame-restart recovery.
- SAP-C: 13 initial silences verified against ASAP.
- Fixed empty header lines in Hcl5.sap.
- All seven regression executables PASS.
- Zero rendering failures.

Scope: 3-second scans, 48 kHz, default subsong.
Full-duration, all-subsong and sound-fidelity
validation remain pending.

Next milestone: SAP-D implementation.

### SAP-B/C final rendering validation — 2026-11-27

- SAP-B: 3824/3824 rendered (100%).
- SAP-C: 1755/1755 rendered (100%).
- Total: 5579/5579 rendered.
- SAP-B: 9 initial silences, 1 frame-restart recovery.
- SAP-C: 13 initial silences verified against ASAP.
- Fixed empty header lines in Hcl5.sap.
- All seven regression executables PASS.
- Zero rendering failures.

Scope: 3-second scans, 48 kHz, default subsong.
Full-duration, all-subsong and sound-fidelity
validation remain pending.

Next milestone: SAP-D implementation.

### SAP-D D1 — continuous INIT (experimental)

Initial support for TYPE D without a PLAYER tag. INIT starts at the
header address with A set to the selected subsong and I clear. PCM
starts immediately; CPU state persists across absolute FASTPLAY chunks.
The existing SAP timing profile retains DRAM refresh, WSYNC, VCOUNT,
POKEY IRQ and disabled ANTIC NMI delivery. No periodic PLAYER calls
are implemented yet; D files with PLAYER explicitly return unsupported.
GTIA CONSOL audio and COVOX remain unimplemented. No external player
code is included. Specification: https://asap.sourceforge.net/sap-format.html

Synthetic regression and 94 no-PLAYER ASMA files await local validation.
Build the existing renderer and tests, then run:

    python3 tests/scan_atari8_sapb.py "$HOME/Sauti-tools/atari8/asma" \\
        --type D --without-player --seconds 3 --limit 0 \\
        --report build/sapd-d1-scan.csv

Do not interpret successful WAV generation as accurate sound.

### SAP-D D1 hardware follow-up

Baseline: 94 no-PLAYER files, 4 nonzero, 86 silent, 3 DMACTL write
faults and 1 PACTL write fault. The continuous profile now mixes the
CONSOL bit-3 speaker with provisional gain (8 of 60 POKEY level units,
clamped). Gain and waveform agreement are not yet validated. DMACTL
writes are accepted only with display/player/missile DMA disabled;
PACTL control is modeled without enabled external interrupts. No
full ANTIC or PIA emulation is claimed. B/C/R paths are unchanged.
Renderer records POKEY audio writes, CONSOL writes/edges, CPU PC,
IRQEN and SKCTL for diagnostics. Synthetic speaker test added.
All changes await local compilation and corpus validation.

### SAP-D D1 PORTB follow-up

After CONSOL support: 92/94 no-PLAYER files rendered nonzero in a
3-second scan, with no silent results and two PORTB faults (one read,
one write). The SAP-D-only PIA model now supports PORTB/PBCTL data
and direction registers with pulled-up inputs, initially FE/3C/FF
(PORTB/PBCTL/DDRB). Memory remains flat 64 KiB as specified by SAP;
OS ROM, external banks, and PIA interrupts are not emulated. This is
an explicitly limited compatibility model, not full XL/XE banking.
Synthetic register readback and flat memory regression added.
Retest both failed files and all regressions before claiming coverage.

### SAP-D periodic PLAYER (experimental)

Confirmed on Fedora at 2c74df9: all 94 no-PLAYER files render nonzero
(3 seconds, 48 kHz, default subsong); SAP-B/C/D1 regression PASS.
Sound fidelity remains unverified.

TYPE D now accepts optional PLAYER, validates its loaded entry and
runs it at absolute FASTPLAY boundaries starting at time zero before
INIT. Save and restore CPU A/X/Y/P/SP/PC across the bounded host call;
retain guest RAM, IRQ state, master cycles and audio evolution. A call
must return via RTS; overlong calls skip elapsed deadlines, and an
unreturned call faults within the existing watchdog budget. Host call
setup is not cycle-exact interrupt-entry emulation. No new ROM, video,
COVOX or source code from other players is included.
Synthetic tests cover registers, timing, malformed data and watchdog.
The full 751-file SAP-D corpus (657 with PLAYER) awaits local testing.

### SAP-D full corpus: headless hardware follow-up

At a1efc06, Fedora SAP-B/C/D regression passed. Full SAP-D scan:
751 files, 674 nonzero, 4 silent, 73 execution failures (3 s, 48 kHz,
default subsong). Most faults were ANTIC video setup and PIA PORTA.
This revision adds PORTA/DDRA and ANTIC video configuration latches
for D only. Active graphics DMA requests are counted, not executed:
the SAP timing profile keeps graphics DMA disabled. This does not
emulate graphics, full PIA interrupts, or prove original hardware
sound fidelity. Existing POKEY IRQEN/SKCTL restrictions remain; do
not mask unsupported audio modes. Test and rescan locally, then
inspect remaining PLAYER budget faults, POKEY writes and silences.

### SAP-D pending diagnostics: timer-only profile

At 6d43ca3 the SAP-B/C/D test passed, but the 21-file retry still
contained 17 runtime failures and four silent files. Five D406 faults
advanced to D40C. Implement disconnected light-pen reads as FF (an
idle approximation). D-only IRQEN accepts all masks but models only
bits 0..2; non-timer requests are counted explicitly. SKCTL 23 uses
normal modeled audio clocks without serial transfers, retaining its
raw setting and counting the request. No complete SIO, keyboard or
serial-complete IRQ semantics are claimed. Strict/B/C unchanged.
Basis: SAP execution model, https://asap.sourceforge.net/sap-format.html
Remaining watchdogs and silent outputs require comparison with ASAP;
no watchdog suppression or forced PCM is introduced here.
Local regression and retry results pending.

### SAP-D pending PLAYER compatibility experiment

Fedora at cd17365: regression PASS; all 11 remaining I/O failures
resolved in the 21-file retry. Four initially silent tracks produce
PCM by 7.505 seconds; their onset differs from external ASAP by
6–12 ms in the 15-second comparison. Combined scan/retry coverage:
745/751 files rendered, six PLAYER watchdog failures remain.
Five traces show non-returning guest execution (including sample loops
and stack resets); one shows an unacknowledged timer IRQ loop.

Experimental --frame-restart now also accepts D with PLAYER. Inject a
new call on the absolute FASTPLAY grid; restore saved registers on a
matching RTS, otherwise retain live guest registers and stack at the
next deadline. This is a separate explicit diagnostic policy, not a
claim of hardware equivalence or a default fallback. An unfinished
frame may leak stack entries; count abandoned calls and compare sound
with the reference, including full duration, before considering any
production use. JAM and unsupported I/O still fail. Default D watchdog
and B/C paths unchanged. New synthetic regression awaits local test.

### SAP-D periodic IRQ entry correction

Fedora at ab6ccec: regression PASS. Experimental frame injection
rendered 15 seconds of all six formerly failing files. Lemmings and
four Vaxeen files had nonzero PCM throughout all 15 one-second bins;
75–117 abandoned calls each. Das Omen Intro had 740 abandoned calls
and silence after the first second while ASAP remained active.
Consequently rendering coverage is not equivalent to playback accuracy.
RMS levels generally lower than ASAP; no gain adjustment made.

Hypothesis from the pending IRQ trace: periodic service must be able
to execute while INIT has a pending timer IRQ. Set I at entry in both
D schedulers, restore interrupted flags on normal return, leave timer
pending state untouched. Guest CLI can still enable IRQ. This models
one aspect of an interrupt-like service, not cycle-exact NMI entry.
The SAP specification does not explicitly prescribe PLAYER entry I.
Synthetic pending-IRQ regression added. Retest Das Omen in standard
mode first, then experiment with frame injection only if required;
rescan the entire D corpus and retest the five compatibility files.
No production fallback or fidelity claim yet.

### SAP-D IRQ correction confirmed; five compatibility candidates

Fedora at 9411b0b: regression PASS. Das Omen Intro now renders 15 s
in standard mode, 748 completed calls, no skipped deadlines, onset
22 ms versus ASAP 20 ms, active signal in all fifteen one-second
bins. Ten-file pending retry: five standard nonzero (including four
known delayed-onset files) and five PLAYER budget failures. Combined
historical coverage 746 standard / 751, pending full post-change scan.

The scanner now accepts --retry-frame-restart for D as well as B,
only on a standard PLAYER budget failure and only by explicit opt-in.
It labels experimental results separately. No decoder default or
production fallback changed. Run a complete 3-second D scan with
this option, and a 60-second reference comparison of the five
remaining candidates before promoting any compatibility policy.
RMS and onset alone do not establish fidelity; listen and compare
waveforms/timing across complete tracks and subsongs separately.

### SAP-D 60-second compatibility follow-up; SAP-S initial implementation

Five remaining D candidates rendered 60 seconds each with explicit
frame injection at ceee2aa/9411b0b, 2,880,000 mono frames each at 48 kHz,
2,992 calls each, zero skipped deadlines and no entirely silent
one-second bins. Lemmings Intro / Cygnus Lyrae / Poison / Star Pawns /
Vaxeen 4U: abandoned calls 407 / 361 / 300 / 374 / 477; Pearson
correlation of one-second RMS bins against ASAP .967 / .987 / .944 /
.796 / .952; aggregate RMS differences -6.13 / -4.82 / -5.06 / -4.85 /
-5.29 dB. These are not waveform fidelity metrics; no gain change or
production fallback. Full D post-IRQ corpus scan still unreported.

SAP-S is now parsed with required INIT and ignored optional PLAYER.
It runs continuously in the existing headless audio profile. At each
FASTPLAY boundary decrement byte $0045, increment $B07B only on zero
result, with modulo-256 wrap. Default interval 78*114 cycles, explicit
FASTPLAY honored. Initial RAM clock tick is placed at time zero and
subsequent updates occur at instruction boundaries; exact timing and
real files await comparison. No external player code incorporated.
Source: https://asap.sourceforge.net/sap-format.html (TYPE S).
Synthetic tests cover counter wrap, ignored PLAYER, default subsong,
NTSC/FASTPLAY override, malformed segment and CPU stop. Scanner now
supports --type S. No claim of real-file coverage until local tests.

### SAP-S first real-file scan

Fedora at 0980a33: SAP-B/C/D/S synthetic regression PASS. All seven
SoftSynth ASMA files (Husak Jakub) rendered nonzero mono PCM for
15 seconds, 720,000 frames per file at 48 kHz, 2,992 RAM clock ticks,
8,892 master cycles per interval, no runtime failures. This confirms
execution coverage, not correct musical progression or fidelity.
The final $B07B value was zero in all seven reports; verify clock
behavior and musical progression against the reference before release.
Comparison tool now supports --all to include successful scan rows.
Combined historical coverage of ASMA B/C/D/S: 6,337 files, including
explicit experimental scheduler retries and delayed onsets. Final
post-IRQ D full scan, all subsongs, longer playback and fidelity checks
remain outstanding. SAP-R has separate tests outside this ASMA count.
No decoder code changed in this checkpoint.

### SAP-S reference comparison (30 seconds)

Fedora at 541a9a4 (decoder unchanged from 0980a33): all seven SAP-S
files rendered 1,440,000 mono frames at 48 kHz, 5,984 clock ticks,
no entirely silent one-second bins. Onsets match external ASAP at
reported millisecond precision (1–5 ms). Pearson correlation of
one-second RMS bins, all 30 / excluding the first bin, and aggregate
RMS ratio in dB (Sauti relative to ASAP):

| File | r (30 bins) | r (29 bins) | dB |
|---|---:|---:|---:|
| Aquanaut | .8413 | .9932 | +2.18 |
| Do_You_Want_to_Dance_With | .8943 | .9977 | +1.86 |
| Kim_Wilde | .8235 | .9967 | +2.13 |
| Opanowanie_Swiata | .9815 | .9897 | +0.50 |
| Ucieczka_w_Nicosc | .9628 | .9689 | -1.29 |
| Wodospadem_Czasu | .9763 | .9984 | +1.19 |
| Wsrod_Gwiazd_Nadziei | .9683 | .9881 | +1.24 |

Computed from rounded RMS values in the local comparison report.
Initial transients differ substantially in some files. These are
energy-envelope measurements, not waveform or listening validation.
The Sauti/ASAP level ratio differs across SAP types; no global gain
adjustment. Clock progression is promising, but cycle-level fidelity
and full-track playback remain unverified. No decoder changes.
Next checkpoint: full post-change B/C/D/S corpus regression, all seven
synthetic test executables, then duration/subsong and fidelity audit.

### Full synthetic regression checkpoint

Fedora at cb057f9: all seven test executables rebuilt with C99,
-Wall -Wextra -Werror -pedantic -O2 and passed (core, cpu, machine,
pcm, pokey, sapb, sapr). This covers all five implemented SAP types
and mono/dual POKEY, not complete hardware or musical fidelity.
Next: repeat the full ASMA B/C/D/S scans with the current decoder,
explicit frame-restart retries for B/D only. Keep delayed-onset and
experimental results separately classified. All subsongs, complete
track durations, fidelity and provenance audit remain outstanding.
No decoder code changed in this checkpoint.

### Full ASMA regression checkpoint (2026-11-27)

Fedora, decoder at cb057f9, scanner at b01f40b. All seven synthetic
executables passed before corpus testing. Full ASMA scan at 48 kHz,
3 seconds, default subsong, explicit frame-restart retries for B/D:

| Type | Standard nonzero | Frame-restart nonzero | Silence | Runtime failures | Total |
|---|---:|---:|---:|---:|---:|
| B | 3814 | 1 | 9 | 0 | 3824 |
| C | 1742 | 0 | 13 | 0 | 1755 |
| D | 742 | 5 | 4 | 0 | 751 |
| S | 7 | 0 | 0 | 0 | 7 |
| Total | 6305 | 6 | 26 | 0 | 6337 |

All 6,337 files rendered; this is execution coverage, not fidelity.
D scan interrupted by operator; --resume validated 609 existing rows
and completed the remaining 142, with aggregate counts intact.
Previously investigated delayed onsets and experimental compatibility
cases are not new failures. No default fallback enabled. SAP-R tested
separately. Basic SAP implementation milestone complete; production
integration, complete durations, all subsongs, fidelity and detailed
license/provenance audit remain open. No decoder changes.

### All ASMA subsongs execution checkpoint

Fedora at 14bb6ad, same decoder as full default-subsong regression:
additional non-default subsongs, 3 seconds each at 48 kHz.

| Type | Standard nonzero | Frame-restart nonzero | Silence | Total |
|---|---:|---:|---:|---:|
| B | 1295 | 0 | 3 | 1298 |
| C | 661 | 0 | 3 | 664 |
| D | 48 | 2 | 0 | 50 |
| S | 0 | 0 | 0 | 0 |
| Total | 2004 | 2 | 6 | 2012 |

Combined default/non-default coverage: 8,349/8,349 rendered,
8,309 standard nonzero, 8 explicit frame-restart nonzero, 32 silent
at 3 seconds, zero runtime failures. This is not musical fidelity.
The six additional silent entries and two compatibility entries
require targeted reference checks. Comparison tool now passes the
CSV's zero-based song index to both Sauti and external asapconv (-s,
see https://asap.sourceforge.net/convert.html). Explicit --report-mode
uses the recorded scheduler per row; existing default behavior is
unchanged. No decoder changes. Full durations and production work remain.

### Additional subsong reference checks and declared-duration scan

Fedora at 2247577: eight pending non-default subsongs compared for
60 seconds each, zero runtime errors. Vox Regis songs 1/2/3 (zero-based)
Sauti/ASAP onsets 7.702/7.703, 7.701/7.701, 7.701/7.701 seconds.
Greenmon/Hello/Ray song 1 entirely silent in both players for 60 s.
Microx songs 3/4 experimental frame-restart: both onsets 20 ms;
53/422 abandoned calls, no skipped deadlines. Song 3 has 46 silent
one-second bins in both players. Pearson correlations of 60 RMS bins
for the five non-silent comparisons .99950/.99976/.99928/.99885/.99575;
aggregate Sauti/ASAP levels -6.72/-6.42/-6.48/-4.72/-6.75 dB.
These metrics confirm progression and explain silence, not waveform
fidelity or correctness of experimental stack behavior.

Renderer accepts explicit --milliseconds (before optional
--frame-restart), 1..5999999 ms, exact 48 frames/ms at 48 kHz.
Default seconds CLI remains unchanged. Scanner --full-duration tests
all subsongs including default, using per-song TIME tags in header
order. LOOP is recorded but only the declared interval is rendered;
missing, invalid or zero TIME are separate untested classifications.
WAV validation reads bounded chunks. New CSV includes song,
duration_ms and loop; resume validates the task prefix and metadata.
Source: https://asap.sourceforge.net/sap-format.html (TIME).
This is a diagnostic tool, not decoder EOF/loop/seek implementation.
Start with a small pilot per type before the entire corpus; recompile
the renderer first. New tool changes not yet locally tested.

### Parallel PCM-only duration pilot

Fedora at 05c4aaa, ASUS i7-12700H, first 20 SAP-B subsongs at
individual declared TIME, explicit B frame-restart retry enabled,
--no-wav. All five runs: 20/20 nonzero, no failures.

| Jobs | Wall seconds | User CPU seconds |
|---:|---:|---:|
| 4 | 85.548 | 213.167 |
| 8 | 51.341 | 220.557 |
| 12 | 43.891 | 225.295 |
| 16 | 33.074 | 241.294 |
| 20 | 25.821 | 263.324 |

20 workers fastest in this pilot (3.31x relative to 4), not a
sustained-load benchmark. PCM-only mode checks frame count, channels,
sample rate and nonzero status without WAV serialization; this does
not measure fidelity. Continue B using sapb-duration-fast20.csv with
--resume --limit 0, same renderer and duration policy. Other types
use distinct reports; B/D only enable explicit frame-restart retry.
No decoder changes. Full-duration coverage remains unverified.

### Full declared-duration corpus checkpoint

Fedora, PCM-only scanner, 20 workers, all 8,349 ASMA B/C/D/S
subsongs enumerated. User reported the following final CSV counts:

| Type | Standard nonzero | Frame-restart nonzero | Unknown TIME | Zero TIME | Total |
|---|---:|---:|---:|---:|---:|
| B | 5114 | 4 | 4 | 0 | 5122 |
| C | 2416 | 0 | 0 | 3 | 2419 |
| D | 790 | 8 | 3 | 0 | 801 |
| S | 7 | 0 | 0 | 0 | 7 |
| Total | 8327 | 12 | 7 | 3 | 8349 |

All 8,339 positive declared durations rendered exact frame counts
and nonzero PCM, with zero reported runtime failures. Ten entries
without positive duration were not tested at full length (previous
3-second coverage exists). Twelve compatibility rows versus eight
in the 3-second scan; identify exact changed rows before implementing
any automatic fallback. Do not infer correct audio from nonzero PCM.
LOOP entries rendered one declared duration, not validated looping.
Next: targeted 60-second reference comparison of unknown/zero TIME,
review headers and all compatibility rows; production metadata,
seek/loop, fidelity, security and provenance remain open. Comparator
supports repeatable --result filter; no decoder changes.

### Missing and zero TIME reference checkpoint

Fedora at 398e68f: all ten entries excluded from positive-duration
scan compared to external ASAP for 60 seconds. Seven unknown TIME
entries rendered nonzero (B: Why Do You Dance With Me, Mission Failed
Successfully, Unicum, Yo Ho; D: Veeblefetzer, Ataraxia, Silky Adventure).
Reported onsets agree within 1 ms, zero skipped deadlines. Three C
zero-TIME entries (Greenmon, Hello, Ray, song index 1) were silent in
both renderers throughout 60 seconds. No runtime errors. Unknown TIME
must remain distinct from zero TIME; these bounded tests cannot prove
an unknown end time. All 8349 subsongs now have execution coverage:
8339 at positive declared TIME, ten exceptions at 60 seconds. Fidelity,
loop behavior and production API remain unverified. Next inspect the
12 full-duration frame-restart rows, including late failures, before
automatic fallback design. No decoder changes.

### Full-duration compatibility comparison and rejected stack experiment

All 12 frame-restart subsongs matched external ASAP's channel count
and exact declared PCM frame count. One-second RMS envelopes (not
waveform or note-level equivalence), user runs 1153959 and 05ee15e:

| Subsong | Baseline full RMS r | Baseline tail r | Baseline relative dB | Restore-SP full RMS r |
|---|---:|---:|---:|---:|
| Chromatics | .944687 | .977252 | -6.77 | .944687 |
| Face of the Galaxy | .937735 | -.012874 | -6.52 | .937735 |
| Get Lucky | .967607 | .535404 | -6.52 | .967607 |
| Graphic Demo 8 | .976316 | .954547 | -4.83 | .976316 |
| Lemmings Intro | .932484 | .911238 | -6.01 | .838693 |
| Microx 3 | .666772 | .669218 | -4.71 | .624632 |
| Microx 4 | .996800 | .999969 | -6.71 | .801113 |
| Vaxeen Cygnus Lyrae | .942756 | .849974 | -4.72 | .919005 |
| Vaxeen Photographic | .958287 | .959410 | -4.32 | .952429 |
| Vaxeen Poison | .953130 | .988483 | -4.74 | .947907 |
| Vaxeen Star Pawns | .792967 | .906671 | -4.96 | .774648 |
| Vaxeen 4U | .936785 | .997811 | -4.54 | .897776 |

Restoring SP after abandoned calls leaves B reported metrics unchanged
and degrades full-envelope correlation for every D row. Six D rows
end at PC=$0000 in that experiment. Reject and remove the option;
restore all four source/tool files to 1153959. No automatic fallback
or global gain correction. Next run a paired control on Microx songs
0,1,2 (standard vs frame-restart, same reference and duration), plus
short Face of the Galaxy before the late budget failure. This tests
whether differences are introduced by scheduler even when standard
returns normally. It does not establish waveform fidelity.

### POKEY diagnostics and analog mixing experiment

Microx songs 0/1/2: standard and frame-restart reported identical
RMS metrics (.752651/.995567/.996657), POKEY write counts, and zero
abandoned calls in restart mode. AUDCTL no-reset experiment was
rejected (songs 0 and 3 worsened to .734507/.570444); reverted.
Synthetic SAP-B fixtures confirm steady-state divider frequencies;
slow 6.8 Hz tone has matching FFT peak despite misleading crossing
count. POLY4 phase sweep yielded mathematically allowed families in
Sauti, while ASAP selected a fixed family for the tested NOP delays.
No hardware phase correction inferred from that comparison alone.

Mixer test f16447c: constant other-channel bias 0/15/30/45 with
volume-15 DAC modulation: relative RMS Sauti 100/100/100/100%,
ASAP 100/52.75/22.71/11.90%. Hardware saturation is independently
documented in Avery Lee, Altirra Hardware Reference Manual, appendix E.2
(https://www.virtualdub.org/downloads/Altirra%20Hardware%20Reference%20Manual.pdf).
An opt-in 61-entry, normalized 16-bit LUT implements the published
piecewise exponential transfer approximation on the instantaneous
sum of four digital levels, before cycle integration. Generated from
the mathematical specification; no external emulator code/table used.
Uniform DAC weights, existing single DC blocker and provisional GTIA
mix remain approximations. Legacy path stays the default; no change
to CPU/POKEY digital scheduling. Select --nonlinear-mix in the renderer
and both reference comparison scripts. First local regression and
mixer/Microx comparisons pending; no claim of physical calibration.

Local 44db740 comparison: all 25 mixer fixtures completed. At DAC
volume 15, bias 0/15/30/45 normalized RMS now 100/55.74/27.34/13.41%
(vs ASAP 100/52.75/22.71/11.90%). Absolute response is not calibrated.
Microx all five exact-length results, unchanged CPU/write counters:

| Song | Linear RMS r | Nonlinear RMS r | Linear relative dB | Nonlinear relative dB |
|---|---:|---:|---:|---:|
| 0 | .752651 | .799459 | -5.30 | -.67 |
| 1 | .995567 | .995154 | -7.13 | -.52 |
| 2 | .996657 | .998522 | -7.08 | -.96 |
| 3 | .666772 | .563789 | -4.71 | -.63 |
| 4 | .996800 | .999905 | -6.71 | -.61 |

Keep the nonlinear option, not yet default. Song 3 remains an open
fidelity issue despite improved overall level. Add wide-integrator
regressions and a deterministic --sample selection to compare both
mixers on a larger corpus including stereo and other SAP types.
