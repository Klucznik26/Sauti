#!/usr/bin/env python3
"""Makes tests/data/hbe_golden.bin: the input and the output of libxaac's harmonic transposer (ixheaacd_qmf_hbe_apply, Apache License 2.0, built OUTSIDE this repository) for the first frames of four of its own
streams, taken from a COPY of libxaac that writes the rows going into and coming out of the transposer into a file (env HBE_DUMP; two fwrite blocks around the call of ixheaacd_qmf_hbe_apply / ixheaacd_hbe_repl_spec in
ixheaacd_sbr_dec.c: the 16 header integers no_bins, start_band, end_band, k_start, synth_size, max_stretch, sbrPitchInBins, patching mode, x_over[6], delay, then no_bins input rows and no_bins output rows of 64 real and
64 imaginary floats).  The streams: h1 (24 kHz mono, pitch 24), k1 (48 kHz, pitch 12: p = 1.0 exactly, the cross products), q1 (generator stream, pitches 0, 10, 0, 64: plain and cross products), f2 (eSBR 4:1, pitch 36).
The file keeps the input of the synthesis bands only and, per frame, four sums of the output (test_usac_hbe.c, test_golden); the frames start at the reset state of the transposer like libxaac's.
usage: make_hbe_golden.py OUT   (the dump files below are written by the instrumented libxaac: paths are those of the session that made the file)"""
import struct, numpy as np, sys
def rec(path, nb, i):
    d = open(path, 'rb').read(); size = 64 + nb * 128 * 4 * 2; b = d[i * size:(i + 1) * size]
    h = struct.unpack('16i', b[:64]); a = np.frombuffer(b[64:], '<f4').reshape(2 * nb, 2, 64)
    return h, a[:nb, 0, :] + 1j * a[:nb, 1, :], a[nb:, 0, :] + 1j * a[nb:, 1, :]
def case(path, nb, idxs):
    out = b''; h0, _, _ = rec(path, nb, idxs[0]); _, start, end, ks, S, ms = h0[1], h0[2], h0[3], h0[4], h0[5], h0[6]; xo = h0[9:15]
    out += struct.pack('<14i', nb, 1 if nb == 64 else 0, S, ks, start, end, ms, *xo, len(idxs))
    for i in idxs:
        h, q, o = rec(path, nb, i)
        out += struct.pack('<i', h[7])
        qq = np.stack([q[:, ks:ks + S].real, q[:, ks:ks + S].imag], -1).astype('<f4')
        out += qq.tobytes()
        ob = o[:, start:end]; N = nb
        f0 = float(ob.real.sum()); f1 = float(ob.imag.sum()); f2 = float((ob.real ** 2 + ob.imag ** 2).sum())
        w = (np.arange(nb)[:, None] * 131 + np.arange(start, end)[None, :] * 7 + 1) / (nb * 64.0)
        f3 = float(((ob.real + 2 * ob.imag) * w).sum())
        out += struct.pack('<4d', f0, f1, f2, f3)
    return out
cases = [('/tmp/claude-1000/hbe/d1.bin', 32, [0, 1, 2]), ('/tmp/claude-1000/hbe/dk1.bin', 32, [0, 1, 2, 3]), ('/tmp/claude-1000/hbe/dq.bin', 32, [0, 1, 2, 3]), ('/tmp/claude-1000/hbe/d4.bin', 64, [0, 1, 2, 3])]
blob = b'HBEG' + struct.pack('<i', len(cases)) + b''.join(case(*c) for c in cases)
open(sys.argv[1], 'wb').write(blob); print(len(blob), 'bytes')
