# Sauti YM listening A/B (experimental)

Three isolated PipeWire GUI builds, all using the existing `stereo-bars` UI:

| Command | YM path |
| --- | --- |
| `bash ./Sauti --ym-classic` (or `bash ./Sauti`) | Existing production YM decoder |
| `bash ./Sauti --ym-reference` | ST-Sound-inspired DAC + 512-sample DC average + 3-tap low-pass + output gain 0.5 |
| `bash ./Sauti --ym-pre-tick` | Reference path plus pre-tick envelope sample order |

Launch each mode separately, load the same YM file and compare by ear. Binaries have
separate names under `build/`; switching mode does **not** modify the production
player binary. The first use of each mode builds its binary.

These are *experimental listening* modes, not a bit-exact ST-Sound emulation:
the DC/low-pass approximations and scaling follow the existing diagnostic
`tools/compare_ym_pcm.py` pipeline. Seek restores YM state and the filter history
from checkpoints; a seek from the beginning clears that history.

For build-only checks:

```sh
make window-pipewire
make window-pipewire-reference
make window-pipewire-pre-tick
make test-ym-reference-tools
```

Known limitation: the filter and DAC experiment applies to **YM decoding only**;
it is not a general effect on other audio formats. The pre-tick update improves
some synthetic envelopes but worsens envelope shape 09 in the reference suite.
