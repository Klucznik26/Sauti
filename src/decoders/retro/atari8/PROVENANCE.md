# Atari8 SAP provenance review — 2026-11-27

Status: preliminary source/provenance review, not legal clearance.
Reviewed branch tip ee6dcce against main 41506876f6afc12834d366eff8227b34eab0092c.
The compare reports 209 commits and 40 changed paths, all C sources,
headers, Python diagnostic tools or documentation. No added third-party
music files, executable test binaries, ROM images or vendor directories
in this comparison. This is not a scan of every historical tree.

## Inventory and policy

| Component | Recorded provenance / reviewed evidence |
|---|---|
| 6502 CPU | Internal C implementation; opcode behavior from technical references. External functional binary is loaded from an argument by the diagnostic runner, not embedded. |
| POKEY and machine | Internal C model of documented register, timer, polynomial and bus behavior. Hardware approximations remain documented. |
| PCM | Internal integer integrator and first-order DC blocker. |
| Analog LUT | 61 values independently reproduced exactly from the documented formula, see below. |
| SAP header and playback | Internal parser, scheduler, buffering, seek and repeat. Guest program originates from the user's input file. |
| Diagnostics | Reviewed Python comparison/scan scripts use standard library modules; ASAP is invoked as a separate executable. |
| Real music | Local ASMA and SAP-R fixtures, not added in the compared paths. No redistribution rights inferred. |

Technical references and external tools:
- SAP specification: https://asap.sourceforge.net/sap-format.html
- ASAP: https://asap.sourceforge.net/ (GPL; upstream mirror README describes GPL v2 or later).
  Retain as external development reference only, not a linked decoder or shipped dependency.
- Avery Lee, Altirra Hardware Reference Manual: https://virtualdub.org/altirra.html
  Use documented hardware behavior, not copied emulator implementation or manual passages.
- Klaus Dormann's functional tests: https://github.com/Klaus2m5/6502_65C02_functional_tests
  Repository declares GPL-3.0. Keep test binaries external.
- ASMA: https://asma.atari.org/ ; individual music rights are not established by archive availability.

## Reproducibility of analog table

For integer i = 0..60, set x = i/60. Define f(x) = x for x <= 0.14,
otherwise f(x) = 0.14 + (1 - exp(-2.85*(x-0.14)))/2.85.
Quantize round(65535*f(i/60)/f(1)).
Compared all 61 generated integers with atari8_pokey_curve in
atari8_pcm.c: 61/61 exact, maximum absolute difference 0.
This verifies the stated formula, not legal ownership or analog accuracy.

## Remaining release gates

- Check complete history and contributor provenance, including whether any
  external implementation was used in code generation. Absence of a
  third-party copyright notice is not evidence of independence.
- Recheck the final production build inputs and linker dependencies;
  this branch has not yet registered SAP in MaktabaSauti.
- Keep external music, converted music, third-party binaries and test
  corpus out of the release unless separately cleared.
- Repository metadata currently reports public visibility; root LICENSE,
  LICENSE.md, LICENSE.txt and COPYING were not found. Confirm intended
  distribution terms and repository access policy with the owner before
  release. Do not change licensing or visibility as part of this review.
- Verify the licenses of all other MaktabaSauti decoders separately.
- Obtain legal review if definitive closed-source distribution clearance
  is required.

## Security regression checkpoint

Local ee6dcce: seven strict C99 regression executables PASS after adding
common-stream resource limits. The new limits have not yet been rerun
under sanitizers or validated in the public API. Prior sanitizer and
thread results are recorded in README.md.
