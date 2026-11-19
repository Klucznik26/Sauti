#!/usr/bin/env python3
"""SBR-78: porownanie LPC (alpha0/alpha1) i chirpu (bw) sondy Sauti z instrumentowanym FFmpeg.

Wejscie: zrzut FFmpeg SAUTI_SBR73_FFMPEG_HFGEN (call,k,src,g,bw,a0r,a0i,a1r,a1i,...), zrzut Sauti SAUTI_SBR75_ALPHA (frame,hp,a0r,a0i,a1r,a1i)
i opcjonalnie SAUTI_SBR78_BW (frame,k,bw).  Ramka Sauti f odpowiada wywolaniu FFmpeg f+1.  Kod wyjscia 0 = zgodne w progach, 1 = niezgodne,
2 = brak danych do porownania (np. zero ramek z filtrowaniem odwrotnym: wtedy test niczego nie dowodzi).
"""
import csv, sys, collections, math


def rows(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def main():
    if len(sys.argv) < 3:
        print("uzycie: sauti_he_lpc_vs_ffmpeg.py ff_hfgen.csv sauti_alpha.csv [sauti_bw.csv]")
        return 2
    ff = collections.defaultdict(dict)       # call -> src -> (a0, a1)
    ffbw = collections.defaultdict(dict)     # call -> k -> bw
    for r in rows(sys.argv[1]):
        c, src, k = int(r["call"]), int(r["src"]), int(r["k"])
        ff[c].setdefault(src, (complex(float(r["a0r"]), float(r["a0i"])), complex(float(r["a1r"]), float(r["a1i"]))))
        ffbw[c][k] = float(r["bw"])
    errs, nonzero, frames = [], 0, set()
    worst = 0.0
    for r in rows(sys.argv[2]):
        f, hp = int(r["frame"]), int(r["hp"])
        if f + 1 not in ff or hp not in ff[f + 1]:
            continue
        a0 = complex(float(r["a0r"]), float(r["a0i"]))
        a1 = complex(float(r["a1r"]), float(r["a1i"]))
        for mine, ref in ((a0, ff[f + 1][hp][0]), (a1, ff[f + 1][hp][1])):
            if abs(ref) < 1e-6 and abs(mine) < 1e-6:
                continue
            nonzero += 1
            frames.add(f)
            e = abs(mine - ref) / max(abs(ref), 1e-6)
            errs.append(e)
            worst = max(worst, e)
    errs.sort()
    if not errs:
        print("BRAK danych: zadnego niezerowego wspolczynnika LPC (plik bez filtrowania odwrotnego?)")
        return 2
    med, p99 = errs[len(errs) // 2], errs[int(len(errs) * 0.99)]
    print("alpha: wspolczynniki=%d ramki=%d  blad wzgledny: mediana %.2e  p99 %.2e  max %.2e" % (nonzero, len(frames), med, p99, worst))
    ok = med < 1e-5 and p99 < 1e-3 and worst < 5e-2   # float w FFmpeg przy prawie osobliwych ukladach daje ogon (SBR-78)
    if len(sys.argv) > 3:
        bwerr, n = 0.0, 0
        for r in rows(sys.argv[3]):
            f, k = int(r["frame"]), int(r["k"])
            ref = ffbw.get(f + 1, {}).get(k)
            if ref is None:
                continue
            n += 1
            bwerr = max(bwerr, abs(float(r["bw"]) - ref))
        print("chirp bw: bin-ramki=%d  max |roznica| = %.2e" % (n, bwerr))
        ok = ok and n > 0 and bwerr < 1e-6
    print("WYNIK:", "ZGODNE" if ok else "NIEZGODNE")
    return 0 if ok else 1


sys.exit(main())
