# uniDRC (MPEG-D DRC, ISO/IEC 23003-4) for USAC: plan and measurements

Status (2026-11-24): NOT implemented.  The decision of the owner (2026-11-24): do it as an OPTIONAL OUTPUT FEATURE; the default PCM stays untouched (the reference decoders do not apply it by default either, see below).

## What the references do (measured)

* Encoder: libxaac `xaacenc -aot:42 -usac:1 -drc:1` reads `impd_drc_config_params.txt` from the WORKING DIRECTORY (copy of `test/encoder/impd_drc_config_params.txt`; without the file the option is silently dropped) and writes a `uniDrcConfig` (3 gain sets, all single band: effects 0x0001, 0x0800, 0x0800), a `loudnessInfoSet` (`method_value` -16 LUFS) into the `UsacConfig` extension and `uniDrcGain()` into every access unit (extension element).  The stream grows by about 1.1 kB for 1.5 s.
* Decoder: libxaac `xaacdec` output of that stream is IDENTICAL to the decode of the same stream without DRC (RMS 5912) as long as no DRC option is given (also with `-loudness_leveling_flag` 0 / 1).  With ANY of `-target_loudness`, `-effect` the DRC is applied: `-target_loudness:-24` gives a TIME-VARYING gain (ratio of the outputs 0.36 .. 0.49, median -8.1 dB: the static loudness normalisation -16 -> -24 LUFS = -8 dB plus the dynamic range gains of the selected DRC set), `-effect:1` -11.7 dB RMS.
* So the minimum useful feature is the whole chain: parse -> selection (target loudness, effect request, downmix id) -> gain decoding (nodes, splines) -> gain application (time domain for single band sets; QMF / filter bank for multi band) -> peak limiter.  A "loudness only" shortcut would NOT match the reference.

## Staging

1. U1 parser: `uniDrcConfig()` (coefficients, instructions, downmix instructions, v1 extension), `loudnessInfoSet()`, `uniDrcGain()` (gain sequences: delta coded nodes with the Huffman tables of the standard).  Check: consumes exactly the bits in every AU of libxaac streams; dump of the decoded node values against an instrumented xaacdec (`drc_src/impd_drc_dynamic_payload.c`).
2. U2 selection (`impd_drc_selection_process*.c`: target loudness, DRC effect request, set with loudness info matching).
3. U3 gain decoding to a per-sample gain curve (interpolation: linear, spline; time constants), time domain single band.
4. U4 application + peak limiter; multi band (filter bank) last.
5. API: `MAKTABASAUTI_OPEN_*` / option struct fields: target loudness (LUFS, none = off), effect, boost / cut factors.

Reference code: libxaac `decoder/drc_src/*.c` (Apache 2.0, about 11 000 lines for the whole pipeline): READ it, write the logic anew; tables (Huffman delta codes, slope / characteristic tables) as data with a register entry (the owner's decision 2026-11-21 covers libxaac tables).  Second reference: FDK libDRCdec (same standard) when built with it.
