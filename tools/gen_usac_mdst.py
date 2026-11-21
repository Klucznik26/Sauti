#!/usr/bin/env python3
"""Derives the MDST estimation filters of USAC complex stereo prediction (ISO/IEC 23003-3, 7.7.2) from their definition and prints src/decoders/modern/aac/sauti_usac_mdst.c.
NO table of any other decoder is read: the coefficients follow from the transforms and the windows.

Definition.  The decoder knows the MDCT spectrum C of the downmix of the current frame (and of the previous frame) but not its MDST S.  The estimate takes the time signal that the frame's
own MDCT stands for (the inverse transform, windowed: x = w * (1/N) A^T C, which still carries the time-domain aliasing, as the lapped transform leaves it before overlap-add) and
analyses it again with the MDST: S_est = B (w * x) = M C, with the N x N matrix M = B diag(w) diag(w) A^T / N, A / B the MDCT / MDST matrices of one frame (2N samples, phase n0 = 1/2 + N/2)
and w the 2N-point window of the frame (rising half: shape of the previous frame, falling half: shape of this frame; start / stop / stop-start windows with their flat and short parts).  For
the previous frame the same is done with ITS window; only its second half overlaps the current frame, so  S_prev_est = B[:, :N] diag(w_cur[:N]) (diag(w_prev[N:]) (A^T / N)[N:, :]) C_prev.
Away from the ends of the spectrum M is a banded Toeplitz matrix (the entries decay ~1/distance and the standard keeps seven taps): current frame: antisymmetric taps h(+j) = -h(-j), h(0) = 0;
previous frame: symmetric taps whose sign alternates with the line number.  The tables are the taps of the middle of the spectrum.
NORMALISATION: the derived taps are half of the published ones in every case (the standard's MDST estimate is scaled by two; here this is fixed once, by the published value 0.5 of the
sine window), and the sign of the current-frame taps is that of the published ones; everything else (15 + 16 other numbers) then follows from the definition: the script compares them with the
published values (FFmpeg aactab.c ff_aac_usac_mdst_filt_cur, FDK mdst_filt_coef_prev) when given the path of aactab.c, and prints the largest difference.
usage: gen_usac_mdst.py [PATH/TO/libavcodec/aactab.c]  > src/decoders/modern/aac/sauti_usac_mdst.c   (the comparison goes to stderr)"""
import re, sys
import numpy as np
N = 1024
NS = N // 8
LEAD = (N - NS) // 2
SCALE = 2.0
def sine(n): return np.sin(np.pi * (np.arange(n) + 0.5) / (2 * n))
def kbd(n, alpha):
    w = np.kaiser(n + 1, np.pi * alpha); c = np.cumsum(w); return np.sqrt(c[:n] / c[n])
def slope(shape, n):                                        # rising half of a window of 2n points
    if shape == 0: return sine(n)
    return kbd(n, 4.0)                                      # KBD alpha 4 for the long slope and for the short slope inside start / stop windows
n0 = 0.5 + N / 2
nn = np.arange(2 * N)[None, :]; kk = np.arange(N)[:, None]
A = np.cos(np.pi / N * (nn + n0) * (kk + 0.5))             # MDCT of one frame
B = np.sin(np.pi / N * (nn + n0) * (kk + 0.5))             # MDST of one frame
def left(kind, shape):                                      # first half of the window of a frame
    if kind in ('stop', 'stopstart'): return np.concatenate([np.zeros(LEAD), slope(shape, NS), np.ones(N - LEAD - NS)])
    return slope(shape, N)
def right(kind, shape):                                     # second half
    if kind in ('start', 'stopstart'): return np.concatenate([np.ones(LEAD), slope(shape, NS)[::-1], np.zeros(N - LEAD - NS)])
    return slope(shape, N)[::-1]
def window(kind, ps, cs): return np.concatenate([left(kind, ps), right(kind, cs)])
def banded(M, parity=None):                                 # taps -3..3 from the middle of the spectrum
    ks = range(N // 4, 3 * N // 4) if parity is None else range(N // 4 + parity, 3 * N // 4, 2)
    return np.array([np.mean([M[k, k + m] for k in ks]) for m in range(-3, 4)])
KINDS = ['long', 'start', 'stop', 'stopstart']
PAIRS = [(0, 0), (1, 1), (0, 1), (1, 0)]                    # (previous shape, current shape): sine/sine, kbd/kbd, sine/kbd, kbd/sine
cur = np.zeros((4, 4, 7))
for wi, kind in enumerate(KINDS):
    for si, (ps, cs) in enumerate(PAIRS):
        w = window(kind, ps, cs)
        M = B @ np.diag(w) @ np.diag(w) @ A.T / N
        cur[wi, si] = -SCALE * banded(M)                    # sign: the published taps are +c at the higher lines
prev = np.zeros((4, 4))
for row, (pkind, ckind, shape) in enumerate([('long', 'long', 0), ('long', 'long', 1), ('start', 'stop', 0), ('start', 'stop', 1)]):
    wp = window(pkind, shape, shape); wc = window(ckind, shape, shape)
    M = B[:, :N] @ np.diag(wc[:N]) @ (np.diag(wp[N:]) @ (A.T / N)[N:, :])
    t = SCALE * banded(M, 1)                                # odd lines: positive
    assert np.allclose(t, t[::-1], atol=1e-6), t
    prev[row] = t[3:]                                       # centre, +1, +2, +3 -> published order is (outer ... centre)
    prev[row] = t[::-1][:4][::-1] if False else np.array([t[0], t[1], t[2], t[3]])
cur = np.round(cur, 6); prev = np.round(prev, 6)
if len(sys.argv) > 1:
    src = open(sys.argv[1]).read()
    i = src.index("ff_aac_usac_mdst_filt_cur"); j = src.index('{', src.index('=', i)); k = src.index('};', j)
    pub = np.array([float(x) for x in re.findall(r'[-+]?\d+\.\d+', src[j:k])]).reshape(4, 4, 7)
    sys.stderr.write("largest difference to the published current-frame taps: %.2e (%d values)\n" % (np.abs(pub - cur).max(), pub.size))
    fdk = np.array([[0, .106103, .25, .31831], [.059509, .123714, .186579, .213077], [.038498, .039212, .039645, .03979], [.026142, .026413, .026577, .026631]])
    sys.stderr.write("largest difference to the published previous-frame taps: %.2e\n" % np.abs(fdk - prev).max())
    sys.stderr.write("derived previous-frame taps:\n%s\n" % prev)
print("""/* USAC complex stereo prediction: the MDST estimation filters (ISO/IEC 23003-3, 7.7.2).  GENERATED by tools/gen_usac_mdst.py, which DERIVES the numbers from the definition of the MDCT / MDST
 * and the windows (see the header of that script); no table of another decoder is the source.  Both tables are rounded to the six decimals of the standard's own tables. */
#include "sauti_usac_tables.h"
""")
print("/* [window sequence: long / eight short, start, stop, stop-start][window shapes: previous / current = sine / sine, KBD / KBD, sine / KBD, KBD / sine][taps at line offsets +3 .. -3] */")
print("const float sauti_usac_mdst_filt_cur[4][4][7] = {")
for w in range(4):
    print("    {")
    for s in range(4): print("        { " + ", ".join("%.6ff" % x for x in cur[w, s]) + " },")
    print("    },")
print("};")
print("/* previous frame: [0] long / start sine, [1] long / start KBD, [2] stop / stop-start sine, [3] KBD; the taps at distance 3, 2, 1, 0 from the line (symmetric; the sign alternates with the line) */")
print("const float sauti_usac_mdst_filt_prev[4][4] = {")
for r in range(4): print("    { " + ", ".join("%.6ff" % x for x in prev[r]) + " },")
print("};")
