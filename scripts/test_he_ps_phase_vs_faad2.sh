#!/usr/bin/env bash
# SBR-96: PS IPD/OPD phase rotation against the reference decoder FAAD2 (libfaad.so.2) on generated constant-parameter streams: IID 0 / 3, ICC = 1 (the
# decorrelator then does not contribute, so the two decoders' different decorrelators do not matter), IPD 0..7 except 4, OPD 0 / 3, 20- and 34-band layouts, tones at
# 40 90 180 300 700 1500 Hz (the lowest ones fall into the hybrid bands where the sign of the imaginary part is reversed).
# usage: scripts/test_he_ps_phase_vs_faad2.sh   (needs libfaad.so.2, fdkaac for the tone core: SAUTI_FDKAAC=path, python3 with numpy)
# SKIP (exit 0, stated loudly) without them: that is not a proof of agreement.
set -euo pipefail
cd "$(dirname "$0")/.."
FDK="${SAUTI_FDKAAC:-$HOME/Sauti-tools/fdkaac/fdkaac}"
MAX="${PS_PHASE_MAX:-0.02}"
[ -x "$FDK" ] || { echo "SKIP: no fdkaac ($FDK); set SAUTI_FDKAAC"; exit 0; }
ldconfig -p 2>/dev/null | grep 'libfaad.so.2' >/dev/null || { echo "SKIP: no libfaad.so.2"; exit 0; }
make -s build/sauti_he_remix build/sauti_aac_adapter_cli >/dev/null
cc -O2 -o build/sauti_faad_oracle tools/sauti_faad_oracle.c -ldl
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-psphase.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
python3 -I - "$W/tones.wav" <<'PY'
import sys,wave
import numpy as np
sr=48000; t=np.arange(sr*8)/sr
x=sum(0.1*np.sin(2*np.pi*f*t+0.3*i) for i,f in enumerate([40,90,180,300,700,1500]))
w=wave.open(sys.argv[1],'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
w.writeframes((np.clip(x,-1,1)*32767).astype('<i2').tobytes()); w.close()
PY
"$FDK" -p 5 -b 32000 -f 2 -o "$W/tones.aac" "$W/tones.wav" >/dev/null 2>&1
n=0; worst=0; fail=0
for mode in 1 2; do for ipd in 0 1 2 3 5 6 7; do for opd in 0 3; do for iid in 0 3; do
  SAUTI_REMIX_PS=1 SAUTI_REMIX_PS_CONST=1 SAUTI_REMIX_PS_IID_MODE=$mode SAUTI_REMIX_PS_ICC_MODE=$mode SAUTI_REMIX_PS_IPD=1 SAUTI_REMIX_PS_IPDV=$ipd \
    SAUTI_REMIX_PS_OPDV=$opd SAUTI_REMIX_PS_IIDV=$iid build/sauti_he_remix "$W/tones.aac" "$W/c.aac" 1 1 250 2 0 >/dev/null
  build/sauti_aac_adapter_cli "$W/c.aac" "$W/c.sau" >/dev/null 2>&1 || { echo "FAIL mode=$mode ipd=$ipd opd=$opd iid=$iid: adapter rejected"; fail=1; continue; }
  build/sauti_faad_oracle "$W/c.aac" "$W/c.fad" >/dev/null 2>&1 || { echo "SKIP: libfaad could not decode"; exit 0; }
  d=$(python3 -I tools/sauti_ps_phase_sweep.py "$W/c.sau" "$W/c.fad" | sed 's/A-B //')
  n=$((n+1)); worst=$(python3 -c "print(max($worst,$d))")
  python3 -c "import sys;sys.exit(0 if $d<=$MAX else 1)" || { echo "FAIL mode=$mode ipd=$ipd opd=$opd iid=$iid: relative difference $d > $MAX"; fail=1; }
done; done; done; done
[ "$fail" = 0 ] && { echo "WYNIK: ZGODNE ($n strumieni, najwieksza roznica wzgledna $worst, prog $MAX)"; exit 0; } || { echo "WYNIK: NIEZGODNE"; exit 1; }
