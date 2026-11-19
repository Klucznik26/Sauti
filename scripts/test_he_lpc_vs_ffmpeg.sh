#!/usr/bin/env bash
# SBR-78: LPC (alpha0/alpha1) i chirp (bw) sondy HE-AAC kontra instrumentowany FFmpeg na PRAWDZIWYM pliku z filtrowaniem odwrotnym.
# Uzycie: scripts/test_he_lpc_vs_ffmpeg.sh PLIK.aac [RAMKI=400]
# Wymaga: sondy (make build/sauti_he_aac_probe) i instrumentowanego FFmpeg (SAUTI_FFMPEG_INSTR, domyslnie ~/Sauti-tools/ffmpeg-sbr54w/ffmpeg,
# zrzut SAUTI_SBR73_FFMPEG_HFGEN).  Bez instrumentowanego FFmpeg lub pliku wypisuje SKIP i konczy kodem 0 -- to NIE jest dowod zgodnosci.
set -euo pipefail
cd "$(dirname "$0")/.."
FILE="${1:-}"; FRAMES="${2:-400}"
FF="${SAUTI_FFMPEG_INSTR:-$HOME/Sauti-tools/ffmpeg-sbr54w/ffmpeg}"
[ -n "$FILE" ] && [ -r "$FILE" ] || { echo "SKIP: brak pliku wejsciowego (uzycie: $0 PLIK.aac [RAMKI])"; exit 0; }
[ -x "$FF" ] || { echo "SKIP: brak instrumentowanego FFmpeg ($FF); ustaw SAUTI_FFMPEG_INSTR"; exit 0; }
make -s build/sauti_he_aac_probe >/dev/null
W="$(mktemp -d "${TMPDIR:-/tmp}/sauti-lpc.XXXXXX")"; trap 'rm -rf -- "$W"' EXIT
python3 -I - "$FILE" "$FRAMES" "$W/cut.aac" <<'PY'
import sys
data=open(sys.argv[1],'rb').read(); n=int(sys.argv[2]); out=bytearray(); pos=0; cnt=0
while pos+7<=len(data) and cnt<n:
    if data[pos]!=0xFF or (data[pos+1]&0xF0)!=0xF0: pos+=1; continue
    ln=((data[pos+3]&3)<<11)|(data[pos+4]<<3)|(data[pos+5]>>5)
    if ln<7 or pos+ln>len(data): break
    out+=data[pos:pos+ln]; pos+=ln; cnt+=1
open(sys.argv[3],'wb').write(out); print("ramek ADTS: %d"%cnt)
PY
SAUTI_SBR73_FFMPEG_HFGEN="$W/ff_hfgen.csv" "$FF" -v error -i "$W/cut.aac" -f null - >/dev/null 2>&1 || true
[ -s "$W/ff_hfgen.csv" ] || { echo "SKIP: ten FFmpeg nie zapisal zrzutu HFGEN (nieinstrumentowany?)"; exit 0; }
SAUTI_SBR75_ALPHA="$W/alpha.csv" SAUTI_SBR78_BW="$W/bw.csv" build/sauti_he_aac_probe "$W/cut.aac" "$FRAMES" >"$W/probe.txt" 2>&1 || true   # sonda konczy kodem 1 z zalozenia (brak PCM HE)
[ -s "$W/alpha.csv" ] || { echo "FAIL: sonda nie zapisala zrzutu alpha"; tail -3 "$W/probe.txt"; exit 1; }
python3 -I tools/sauti_he_lpc_vs_ffmpeg.py "$W/ff_hfgen.csv" "$W/alpha.csv" "$W/bw.csv"
