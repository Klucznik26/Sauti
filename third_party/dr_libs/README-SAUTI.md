# dr_flac vendored in Sauti

Upstream: https://github.com/mackron/dr_libs (`dr_flac.h`, v0.13.4 development snapshot, public domain / MIT-0). Used with `DR_FLAC_NO_STDIO`, memory input only.

## Local modifications (`third_party/dr_libs/dr_flac.h`)

- `drflac__calculate_prediction_32`: the accumulation `prediction += coefficients[i] * pDecodedSamples[-k]` is done in `drflac_uint32` and converted back
  (`SAUTI` comment). The original relies on 32-bit signed wrap-around, which is undefined behaviour in C; UBSan (gcc) reported it on corrupt streams
  (`signed integer overflow: -48504325 * -55`). The upstream `no_sanitize("signed-integer-overflow")` attribute exists for clang only, and is ineffective on an inlined
  function. Results are bit-identical on every target with two's complement `int` (all supported targets).
- All `residue + prediction` sums and the unencoded-residual path (`drflac__decode_samples_with_residual__unencoded`) use `drflac_uint32` arithmetic as well (16 sites, found by
  UBSan fuzzing: `signed integer overflow: 1426913846 + 755590732`).
Upstream status: not reported yet; patch in `docs/upstream-patches/dr_flac.patch`.
