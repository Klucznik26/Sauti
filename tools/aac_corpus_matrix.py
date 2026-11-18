#!/usr/bin/env python3
"""Macierz korpusu AAC: pokrycie i zgodnosc PCM natywnych adapterow Sauti (build/sauti_aac_adapter_cli) z referencja ffmpeg.

Dla kazdej kombinacji (koder, profil/opcje, czestotliwosc, kanaly, sygnal) generuje deterministyczny sygnal, koduje go do ADTS (ffmpeg `aac`, libfdk_aac LC/HE/HE-v2, faac LC/Main), dekoduje referencja
ffmpeg do float32 i natywnym dekoderem Sauti, a potem wypisuje werdykt: REJECT (adapter odmawia: to jest "luka pokrycia", nie blad), albo SNR [dB] po dopasowaniu opoznienia.
Referencja i koder sa uzywane WYLACZNIE do testow (zadnej zaleznosci runtime).  Uzycie: aac_corpus_matrix.py [--seconds 2] [--filter <podciag>] [--out <katalog>] [--markdown <plik>]
"""
import argparse, os, shutil, struct, subprocess, sys, wave
import numpy as np

CLI = "build/sauti_aac_adapter_cli"

def make_signal(kind, rate, ch, seconds):
    n = int(rate * seconds); t = np.arange(n) / rate
    rng = np.random.default_rng(1234)
    if kind == "music":                                   # akord z harmonicznymi i zanikiem; kanaly lekko rozne (panorama)
        s = np.zeros(n)
        for f0, a in ((220.0, 0.25), (277.2, 0.2), (329.6, 0.2)):
            for h in range(1, 7):
                if f0 * h < rate * 0.45: s += a / h * np.sin(2 * np.pi * f0 * h * t)
        s *= (0.5 + 0.5 * np.exp(-3 * (t % 1.0)))
        chans = [s * 0.5 * (1.0 - 0.4 * c / max(1, ch - 1)) for c in range(ch)]
    elif kind == "noise":                                 # szum pasmowy: wymusza PNS/szeroki widmowo
        x = rng.standard_normal(n); k = np.ones(8) / 8
        chans = [np.convolve(rng.standard_normal(n), k, mode="same") * 0.2 for _ in range(ch)]
        del x
    else:                                                 # transient: stuki + ton (krotkie okna)
        s = 0.2 * np.sin(2 * np.pi * 523.0 * t)
        for p in range(0, n, rate // 4):
            m = min(64, n - p); s[p:p + m] += 0.8 * np.hanning(m) * rng.standard_normal(m)
        chans = [s if c % 2 == 0 else np.roll(s, 37) for c in range(ch)]
    x = np.stack(chans, axis=1)
    return np.clip(x, -0.99, 0.99)

def write_wav(path, x, rate):
    pcm = (x * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(x.shape[1]); w.setsampwidth(2); w.setframerate(rate); w.writeframes(pcm.tobytes())

def read_wav_f32(path):
    d = open(path, "rb").read()
    assert d[:4] == b"RIFF"
    pos = 12; rate = ch = None
    while pos + 8 <= len(d):
        cid = d[pos:pos + 4]; size = struct.unpack("<I", d[pos + 4:pos + 8])[0]
        if cid == b"fmt ": _, ch, rate = struct.unpack("<HHI", d[pos + 8:pos + 16])
        if cid == b"data":
            x = np.frombuffer(d[pos + 8:pos + 8 + size], dtype="<f4")
            return rate, ch, x.reshape(-1, ch)
        pos += 8 + size + (size & 1)
    raise ValueError("brak chunku data")

def snr_db(ref, test, max_lag=4096):
    """SNR po dopasowaniu opoznienia (+-max_lag) i kanalow; zwraca (snr, lag) albo (None, None) gdy brak materialu."""
    n = min(len(ref), len(test))
    if n < 4096: return None, None
    r = ref[:n].mean(axis=1); t = test[:n].mean(axis=1)
    seg = slice(n // 4, n // 4 + min(16384, n // 2))
    best = (-1.0, 0)
    for lag in range(-max_lag, max_lag + 1, 1):
        a = r[seg]; b = np.roll(t, lag)[seg]
        den = np.sqrt((a * a).sum() * (b * b).sum()) + 1e-30
        c = float((a * b).sum() / den)
        if c > best[0]: best = (c, lag)
    lag = best[1]                                        # np.roll(test, lag)[i] ~ ref[i]: lag > 0 = test opozniony, lag < 0 = referencja pomija poczatek (np. FFmpeg pomija pierwsza ramke strumienia faac)
    if lag >= 0: ra, ta = ref[lag:n], test[0:n - lag]
    else: ra, ta = ref[0:n + lag], test[-lag:n]
    m = min(len(ra), len(ta)) - 2048
    ra, ta = ra[1024:1024 + m], ta[1024:1024 + m]
    num = float((ra ** 2).sum()); den = float(((ra - ta) ** 2).sum()) + 1e-30
    return 10 * np.log10(num / den + 1e-30), lag

EXPECTED_DIVERGENCE = {"ffmpeg-aac-ms-pns": "PNS+M/S: norma wymaga szumu skorelowanego, FFmpeg dekoduje niezalezny (Sauti: zgodnie z norma)"}

def run(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, **kw)

ENCODERS = [  # (etykieta, argumenty ffmpeg po -i  |  ("faac", opcje faac))
    ("ffmpeg-aac", ["-c:a", "aac", "-b:a", "{br}"]),
    ("ffmpeg-aac-nopns", ["-c:a", "aac", "-aac_pns", "0", "-b:a", "{br}"]),
    ("ffmpeg-aac-nois", ["-c:a", "aac", "-aac_is", "0", "-b:a", "{br}"]),
    ("ffmpeg-aac-notns", ["-c:a", "aac", "-aac_tns", "0", "-b:a", "{br}"]),
    ("ffmpeg-aac-ms", ["-c:a", "aac", "-aac_ms", "1", "-aac_pns", "0", "-b:a", "{br}"]),
    ("ffmpeg-aac-ms-pns", ["-c:a", "aac", "-aac_ms", "1", "-b:a", "{br}"]),       # M/S + PNS: znana rozbieznosc (norma: szum skorelowany; FFmpeg dekoduje niezalezny)
    ("ffmpeg-aac-fast", ["-c:a", "aac", "-aac_coder", "fast", "-b:a", "{br}"]),
    ("ffmpeg-aac-lowbr", ["-c:a", "aac", "-b:a", "{brlow}"]),
    ("fdk-lc", ["-c:a", "libfdk_aac", "-profile:a", "aac_low", "-b:a", "{br}"]),
    ("fdk-lc-vbr", ["-c:a", "libfdk_aac", "-profile:a", "aac_low", "-vbr", "3"]),
    ("fdk-he", ["-c:a", "libfdk_aac", "-profile:a", "aac_he", "-b:a", "{brhe}"]),               # fdk-aac-free (Fedora) nie ma kodera SBR: ENC-NA
    ("fdk-hev2", ["-c:a", "libfdk_aac", "-profile:a", "aac_he_v2", "-b:a", "{brhe2}"]),
    ("faac-lc", ("faac", [])),
    ("faac-joint0", ("faac", ["--joint", "0"])),
    ("faac-ms", ("faac", ["--joint", "1"])),
    ("faac-is", ("faac", ["--joint", "2"])),
    ("faac-pns", ("faac", ["--pns", "5"])),
    ("faac-tns", ("faac", ["--tns"])),
    ("faac-long", ("faac", ["--shortctl", "1"])),
    ("faac-short", ("faac", ["--shortctl", "2"])),
    ("faac-mpeg2", ("faac", ["--mpeg-vers", "2"])),
]

def encode(label, args, wav, out, rate, ch):
    if isinstance(args, tuple):
        return run(["faac", "-o", out, "-q", "100"] + args[1] + [wav])
    br = 48000 * ch if ch <= 2 else 64000 * ch
    a = [x.format(br=str(br), brlow=str(max(8000, 12000 * ch)), brhe=str(24000 * ch), brhe2="32000") for x in args]
    return run(["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error", "-y", "-i", wav] + a + ["-f", "adts", out])

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=2.0)
    ap.add_argument("--filter", default="")
    ap.add_argument("--out", default="build/audio-fixtures/aac-corpus")
    ap.add_argument("--markdown", default="")
    ap.add_argument("--rates", default="8000,16000,22050,32000,44100,48000")
    ap.add_argument("--channels", default="1,2")
    ap.add_argument("--signals", default="music,transient")
    ap.add_argument("--music", action="append", default=[], help="plik audio (np. mp3) uzyty jako zrodlo: fragment od 20 s, przeksztalcony do czestotliwosci i kanalow kombinacji (tylko lokalnie, nic nie jest kopiowane poza build/)")
    ap.add_argument("--require-ok", action="store_true", help="kod wyjscia 1, gdy jakikolwiek wiersz nie ma werdyktu OK (do testow w make)")
    ap.add_argument("--file", action="append", default=[], help="prawdziwy plik ADTS .aac do porownania z ffmpeg (mozna powtarzac); wylacza generowanie korpusu")
    args = ap.parse_args()
    if not os.path.exists(CLI): print("brak", CLI, "(make aac-adapter-cli)"); return 2
    os.makedirs(args.out, exist_ok=True)
    rows = []
    for path in args.file:                                 # prawdziwe pliki: tylko dekodowanie i porownanie, bez kodowania
        tag = os.path.splitext(os.path.basename(path))[0]
        ref = f"{args.out}/{tag}.ref.wav"; nat = f"{args.out}/{tag}.nat.f32"
        r = run(["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error", "-y", "-i", path, "-c:a", "pcm_f32le", ref])
        if not os.path.exists(ref): rows.append((tag, 0, 0, "file", "REF-FAIL", r.stdout.strip()[:60])); continue
        rrate, rch, rx = read_wav_f32(ref)
        c = run([CLI, path, nat]); line = c.stdout.strip().splitlines()[-1] if c.stdout.strip() else ""
        if c.returncode != 0 or line == "REJECT": rows.append((tag, rrate, rch, "file", "REJECT", line)); continue
        toks = dict(t.split("=") for t in line.split()[2:]); nrate, nch = int(toks["rate"]), int(toks["channels"])
        nx = np.fromfile(nat, dtype="<f4").reshape(-1, nch)
        if (nrate, nch) != (rrate, rch): rows.append((tag, rrate, rch, "file", "FORMAT", f"ref {rrate} Hz x{rch}, sauti {nrate} Hz x{nch}")); continue
        sn, lag = snr_db(rx, nx)
        rows.append((tag, rrate, rch, "file", "OK" if sn is not None and sn >= 60 else "MISMATCH", "SNR %s dB, lag %s, %d/%d probek" % ("%.1f" % sn if sn is not None else "?", lag, len(nx), len(rx))))
    for label, enc in ([] if args.file else ENCODERS):
        if args.filter and args.filter not in label: continue
        if isinstance(enc, tuple) and not shutil.which("faac"): continue
        for rate in map(int, args.rates.split(",")):
            for ch in map(int, args.channels.split(",")):
                if label == "fdk-hev2" and ch != 2: continue
                for kind in (args.signals.split(",") if args.signals else []) + ["music:" + m for m in args.music]:
                    kname = kind if not kind.startswith("music:") else "m" + str(abs(hash(kind)) % 10000)
                    tag = f"{label}_{rate}_{ch}_{kname}"
                    wav = f"{args.out}/{tag}.wav"; aac = f"{args.out}/{tag}.aac"; ref = f"{args.out}/{tag}.ref.wav"; nat = f"{args.out}/{tag}.nat.f32"
                    if kind.startswith("music:"):
                        run(["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "error", "-y", "-ss", "20", "-t", str(args.seconds), "-i", kind[6:], "-ac", str(ch), "-ar", str(rate), "-c:a", "pcm_s16le", wav])
                        kind = "m:" + os.path.basename(kind[6:])[:14]
                    else:
                        write_wav(wav, make_signal(kind, rate, ch, args.seconds), rate)
                    e = encode(label, enc, wav, aac, rate, ch)
                    if not os.path.exists(aac) or os.path.getsize(aac) == 0:
                        na = "Unable to set the AOT" in e.stdout
                        rows.append((label, rate, ch, kind, "ENC-NA" if na else "ENC-FAIL", "koder nie obsluguje tej konfiguracji" if na else (e.stdout.strip().splitlines()[-1][:60] if e.stdout.strip() else ""))); continue
                    r = run(["ffmpeg", "-hide_banner", "-nostdin", "-loglevel", "warning", "-y", "-i", aac, "-c:a", "pcm_f32le", ref])
                    if not os.path.exists(ref): rows.append((label, rate, ch, kind, "REF-FAIL", r.stdout.strip()[:60])); continue
                    ref_errors = r.stdout.count("Error submitting packet")
                    rrate, rch, rx = read_wav_f32(ref)
                    c = run([CLI, aac, nat])
                    line = c.stdout.strip().splitlines()[-1] if c.stdout.strip() else ""
                    if c.returncode == 1 or line == "REJECT":
                        rows.append((label, rate, ch, kind, "REJECT", f"ref: {rrate} Hz x{rch}")); continue
                    if c.returncode != 0: rows.append((label, rate, ch, kind, "CLI-ERR", line)); continue
                    toks = dict(t.split("=") for t in line.split()[2:])
                    nrate, nch = int(toks["rate"]), int(toks["channels"])
                    nx = np.fromfile(nat, dtype="<f4").reshape(-1, nch)
                    if (nrate, nch) != (rrate, rch):
                        rows.append((label, rate, ch, kind, "FORMAT", f"ref {rrate} Hz x{rch}, sauti {nrate} Hz x{nch}")); continue
                    if ref_errors:                                  # FFmpeg odrzucil ramki (np. rzad TNS > 12 w LC): zgodnosc PCM nie ma sensu, raportujemy osobno
                        first = [l for l in r.stdout.splitlines() if "aac @" in l][:1]
                        rows.append((label, rate, ch, kind, "REF-ERR", "FFmpeg odrzucil %d ramek (%s); Sauti dekoduje %d probek, ref %d" % (ref_errors, first[0].split("] ")[-1][:48] if first else "?", len(nx), len(rx)))); continue
                    s, lag = snr_db(rx, nx)
                    good = s is not None and s >= 60
                    verdict = "OK" if good else ("DIVERGE" if label in EXPECTED_DIVERGENCE else "MISMATCH")
                    rows.append((label, rate, ch, kind, verdict, "SNR %s dB, lag %s, %d/%d probek" % ("%.1f" % s if s is not None else "?", lag, len(nx), len(rx)) + ("" if good or label not in EXPECTED_DIVERGENCE else " -- " + EXPECTED_DIVERGENCE[label])))
    w = max(len(r[0]) for r in rows) if rows else 10
    out = ["%-*s %6s %2s %-9s %-9s %s" % (w, "koder", "Hz", "ch", "sygnal", "werdykt", "szczegoly")]
    for r in rows: out.append("%-*s %6d %2d %-9s %-9s %s" % (w, r[0], r[1], r[2], r[3], r[4], r[5]))
    by = {}
    for r in rows: by.setdefault(r[0], {}).setdefault(r[4], 0); by[r[0]][r[4]] += 1
    out.append("")
    out.append("PODSUMOWANIE wg kodera: " + "; ".join("%s: %s" % (k, ", ".join("%s=%d" % kv for kv in sorted(v.items()))) for k, v in by.items()))
    print("\n".join(out))
    if args.markdown:
        open(args.markdown, "w").write("```\n" + "\n".join(out) + "\n```\n")
    if args.require_ok and (not rows or any(r[4] != "OK" for r in rows)):
        print("FAIL: nie wszystkie wiersze maja werdykt OK"); return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
