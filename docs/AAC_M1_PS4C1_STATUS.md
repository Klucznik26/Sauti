# AAC-M1 PS-4c1 — one-frame PS QMF lookahead

Status: code committed to `feature/aac-m1-ps-qmf`.
Not yet tested on Fedora.

The previously verified `sauti_aac_he_ps_bridge_render` needs
38 time-major mono SBR QMF slots to produce 32 stereo output slots.
This stage supplies the **six true future slots**, not silence, and
does not re-use already rendered QMF.

`sauti_aac_he_ps_delay_push` is a 32-slot QMF frame FIFO:
- On first frame, retain 32 QMF slots and accompanying PS payload,
  return success with `produced=0` and leave PCM untouched.
- On subsequent frames N+1, create a 38-slot input from pending N
  followed by the FIRST SIX slots of N+1.
- Render frame N using N's PS payload and publish 4096 interleaved
  L/R float samples; retain N+1 as next pending frame.
- Both DSP state and FIFO contents commit only on successful output.
  Reject NaN, invalid PS, buffer sizes, and overlapping storage.
- At end of stream a final pending frame is intentionally **not**
  synthesized without a verified lookahead/flush policy.
- Reset after seek or discontinuity; no global mutable state.

Native test `test-aac-he-ps-delay` compares the FIFO renderer
against direct PS bridge calls for 20/34-band configurations on
three delayed frames, and checks first-frame latency, final pending
status, guards and state/output rollback. Included in
`test-aac-sanitize` ASan+UBSan.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-he-ps-delay PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

Remaining critical PS-4c steps:
1. Implement an atomic ADTS AAC-LC+SBR+PS driver which obtains
   *real* HF-adjusted QMF through
   `sauti_aac_sbr_qmf_frame_export_qmf_ex`,
   preserves parser/core/QMF/PS state and pushes it through the FIFO.
2. Resolve SBR temporal ownership/HF handoffs, LPC and normative
   delay for the supported real ADTS frames.
3. Independently test HE-AAC v2 bitstream stereo PCM against
   FFmpeg with explicit delay alignment; never claim success from
   synthetic-QMF-only test vectors.
4. Clarify end-of-stream valid drain without inventing future slots.

This buffer alone does **not** decode an ADTS file nor prove standard
compliance. It is the temporal PS-frame boundary needed by the
upcoming full adapter.
