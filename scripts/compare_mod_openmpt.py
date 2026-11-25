#!/usr/bin/env python3
"""MOD player (src/decoders/retro/amiga/maktabasauti_mod.c) against libopenmpt (openmpt123) on small synthetic modules, one effect group per module.
Independent yardstick, NOT ground truth: libopenmpt and ProTracker differ in details, and our mixer is plain nearest-sample Paula (floor(i*clock/(period*rate))), so only low-frequency
content (a sine sample) compares sample-accurately; broadband samples differ in the sub-sample phase of the held steps.  Settings: --stereo 200 (hard Amiga panning), --filter 1, --ramping 0, no
Amiga resampler.  Metric: normalised correlation at the best lag (+-400 frames), gain-free.  Cases listed in AGREE must reach 0.9990, the rest (KNOWN) are printed for information.
usage: scripts/compare_mod_openmpt.py [OUTDIR]     (needs openmpt123, numpy, build/maktabasauti_cli; SKIP without them)"""
import os, shutil, struct, subprocess, sys, math, random
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))); CLI = os.path.join(ROOT, 'build', 'maktabasauti_cli')
if not shutil.which('openmpt123') or not os.path.exists(CLI): print('SKIP: no openmpt123 or no build/maktabasauti_cli'); sys.exit(0)
W = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('TMPDIR', '/tmp'), 'modcmp'); os.makedirs(W, exist_ok=True)
def sample(kind, n=256):
    if kind=='sine': return bytes(int(100*math.sin(2*math.pi*i/n))&255 for i in range(n))
    if kind=='saw': return bytes(int(-120+240*i/n)&255 for i in range(n))
    if kind=='square': return bytes((100 if i<n//2 else -100)&255 for i in range(n))
    r=random.Random(5); return bytes(r.randrange(-90,90)&255 for i in range(n))
PER=[856,808,762,720,678,640,604,570,538,508,480,453,428,404,381,360,339,320,302,285,269,254,240,226,214,202,190,180,170,160,151,143,135,127,120,113]
def mod(name, rows, nsamp=4, order=[0], magic=b'M.K.'):
    # rows: list of patterns, each 64 rows x 4 channels of (sample, noteidx or None, effect, param)
    h=bytearray(20)
    kinds=['sine','saw','square','noise']
    sd=b''
    for i in range(31):
        if i<nsamp:
            d=sample(kinds[i%4]); h+=bytes(22)+struct.pack('>HBBHH',len(d)//2,0,64,0,len(d)//2); sd+=d
        else: h+=bytes(22)+struct.pack('>HBBHH',0,0,0,0,1)
    h+=bytes([len(order),127])+bytes(order+[0]*(128-len(order)))+magic
    pat=b''
    for p in rows:
        for r in p:
            for (s,n,e,pm) in r:
                per=PER[n] if n is not None else 0
                pat+=bytes([(s&0xf0)|(per>>8),per&255,((s&15)<<4)|e,pm])
    open(name,'wb').write(bytes(h)+pat+sd)
def empty(): return [[(0,None,0,0)]*4 for _ in range(64)]
def pat(fn):
    p=empty(); p=[list(r) for r in p]; fn(p); return p

def corr(a, b):
    n = min(len(a), len(b)); a = a[:n]; b = b[:n]; best = -1.0
    for lag in range(-400, 401):
        x = a[max(0, lag):][:200000]; y = b[max(0, -lag):][:len(x)]; m = min(len(x), len(y))
        c = float((x[:m] * y[:m]).sum() / np.sqrt(((x[:m] ** 2).sum() * (y[:m] ** 2).sum()) + 1e-30)); best = max(best, c)
    return best
def run(name, pats, order=None):
    p = os.path.join(W, name + '.mod'); mod(p, pats, order=order or list(range(len(pats))))
    subprocess.run([CLI, p, p + '.f32', '--opt', 'mod.sample_rate=48000'], capture_output=True)
    subprocess.run(['openmpt123', '--render', '--samplerate', '48000', '--output-type', 'raw', '--force', '--ramping', '0', '--filter', '1', '--stereo', '200',
                    '--ctl', 'render.resampler.emulate_amiga=0', p], capture_output=True)
    a = np.fromfile(p + '.f32', '<f4').reshape(-1, 2); b = np.fromfile(p + '.raw', '<f4').reshape(-1, 2)
    return min(corr(a[:, 0], b[:, 0]), corr(a[:, 1], b[:, 1])) if a[:, 1].any() and b[:, 1].any() else corr(a[:, 0], b[:, 0])
def fx(e, pm, rows=range(4, 60, 8)):
    def f(p):
        p[0][0] = (1, 14, 0, 0)
        for r in rows: p[r][0] = (0, None, e, pm)
    return f
def tone(p):
    p[0][0] = (1, 12, 0, 0); p[8][0] = (0, 20, 3, 0x10)
    for r in range(9, 32, 2): p[r][0] = (0, None, 3, 0x10)
    p[40][0] = (0, 12, 3, 0x30)
def ext(x, y, per_row=False):
    def f(p):
        p[0][0] = (1, 14, 0, 0)
        for r in range(4, 60, 8): p[r][0] = (0, None, 0xE, (x << 4) | y)
    return f
def retrig(p):
    for r in range(0, 64, 8): p[r][0] = (1, 12 + (r // 8) % 4 * 2, 0xE, 0x92)
def delay(p):
    p[0][0] = (1, 14, 0, 0)
    for r in range(4, 60, 8): p[r][0] = (1, 12 + (r // 8) * 2, 0xE, 0xD2)
def loop(p): p[0][0] = (1, 14, 0, 0); p[8][0] = (1, 18, 0xE, 0x60); p[16][0] = (1, 21, 0xE, 0x62)
def patdelay(p): p[0][0] = (1, 14, 0, 0); p[8][0] = (1, 18, 0xE, 0xE2); p[20][0] = (1, 21, 0, 0)
def vibw(w, e):
    def f(p):
        p[0][0] = (1, 14, 0xE, (e << 4) | w)
        for r in range(1, 48): p[r][0] = (0, None, e, 0x58)
    return f
AGREE = {'arpeggio': fx(0, 0x37), 'porta_down': fx(2, 0x08), 'tone_porta': tone, 'vibrato_sine': vibw(0, 4), 'vibrato_square': vibw(2, 4), 'volume_slide': fx(0xA, 0x04),
         'set_volume': fx(0xC, 0x20), 'speed': fx(0xF, 0x03), 'tempo': fx(0xF, 0x90), 'sample_offset': fx(9, 0x02), 'fine_up': ext(1, 3), 'fine_down': ext(2, 3), 'finetune': ext(5, 3),
         'fine_vol_up': ext(0xA, 3), 'fine_vol_down': ext(0xB, 2), 'note_cut': ext(0xC, 2), 'retrigger': retrig, 'note_delay': delay, 'pattern_loop': loop, 'pattern_delay': patdelay}
KNOWN = {'vibrato_ramp': vibw(1, 4), 'vibrato_random': vibw(3, 4), 'tremolo_sine': vibw(0, 7), 'tremolo_square': vibw(2, 7), 'pan_8xx': fx(8, 0x30)}
bad = 0
for k, f in AGREE.items():
    c = run('a_' + k, [pat(f)]); ok = c >= 0.999; bad += not ok; print('%-16s %.4f %s' % (k, c, 'ok' if ok else 'FAIL'))
for k, f in KNOWN.items(): print('%-16s %.4f (known difference, informational)' % (k, run('k_' + k, [pat(f)])))
sys.exit(1 if bad else 0)
