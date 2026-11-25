#!/usr/bin/env python3
"""Real audio through the ORIGINAL encoders and decoders of WinRec Pro 2.11H (M68K emulation, as tests/data/make_dvsm_adpcm_fixture.py), compared with the Sauti decoder.
For Pack 5 (ADPCM, routine 0x8586 / 0x866e), Pack 4 (Voice, 0x4d1e / 0x7252) and Pack 2 (Deltapack, 0x4bf0 / 0x716a) the script
  1. cuts SECONDS of a 16-bit stereo WAV (any real recording; the music stays local, nothing is stored in the repository),
  2. encodes it with the program's own encoder, writes OUT/<name>_<pack>.dvs,
  3. decodes it with the program's own decoder (the expected PCM) and with build/maktabasauti_cli, and requires the two to be IDENTICAL,
  4. prints the SNR of the program's round trip (how good the packing is) and of ours.
It needs unicorn (pip install unicorn) and numpy; WIN_REC.PRG (copyrighted) stays outside the repository.
usage: scripts/verify_dvsm_real_audio.py IN.wav [SECONDS=20] [OUTDIR]     (default WIN_REC.PRG: ~/Sauti-tools/winrec-pro2/x/WINRECUT.PRO/WINREC.PRO/WIN_REC.PRG, or $WIN_REC_PRG)"""
import os, struct, subprocess, sys, wave
import numpy as np
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
from unicorn.m68k_const import UC_CPU_M68K_M68040, UC_M68K_REG_A0, UC_M68K_REG_A1, UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_D1

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRG = os.environ.get('WIN_REC_PRG', os.path.expanduser('~/Sauti-tools/winrec-pro2/x/WINRECUT.PRO/WINREC.PRO/WIN_REC.PRG'))
WAV = sys.argv[1]; SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0
OUT = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.environ.get('TMPDIR', '/tmp'), 'dvsm_real'); os.makedirs(OUT, exist_ok=True)
CLI = os.path.join(ROOT, 'build', 'maktabasauti_cli')

def load(fn):
    d = open(fn, 'rb').read(); _, tl, dl, bl, sl, _, _, _ = struct.unpack('>HIIIIIIH', d[:28])
    img = bytearray(d[28:28 + tl + dl]); rel = d[28 + tl + dl + sl:]
    off = struct.unpack('>I', rel[:4])[0]; p = 4
    if off:
        a = off
        while True:
            struct.pack_into('>I', img, a, struct.unpack_from('>I', img, a)[0] & 0xffffffff)
            while True:
                b = rel[p]; p += 1
                if b == 0: a = None; break
                if b == 1: a += 254; continue
                a += b; break
            if a is None: break
    return img
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_M68040)
uc.mem_map(0, 0x600000); uc.mem_write(0, bytes(load(PRG)))
STACK, SENT, ST, IN, OUTM, TAB = 0x500000, 0x4f0000, 0x297de, 0x300000, 0x400000, 0x297e6
uc.mem_write(SENT, b'\x4e\x71')

def call(entry, d0, d1, a0, a1, count=400_000_000):
    sp = STACK - 4; uc.mem_write(sp, struct.pack('>I', SENT))
    uc.reg_write(UC_M68K_REG_A7, sp); uc.reg_write(UC_M68K_REG_D0, d0); uc.reg_write(UC_M68K_REG_D1, d1)
    uc.reg_write(UC_M68K_REG_A0, a0); uc.reg_write(UC_M68K_REG_A1, a1); uc.emu_start(entry, SENT, count=count)
def call_state(entry, count, a0, a1, chan, state):                              # ADPCM routines: state pointer on the stack
    uc.mem_write(ST, state); sp = STACK - 4; uc.mem_write(sp, struct.pack('>I', ST)); sp -= 4; uc.mem_write(sp, struct.pack('>I', SENT))
    uc.reg_write(UC_M68K_REG_A7, sp); uc.reg_write(UC_M68K_REG_D0, count); uc.reg_write(UC_M68K_REG_D1, chan)
    uc.reg_write(UC_M68K_REG_A0, a0); uc.reg_write(UC_M68K_REG_A1, a1); uc.emu_start(entry, SENT, count=400_000_000)
    return bytes(uc.mem_read(ST, 4))
def run_init(d1):
    uc.mem_write(TAB, b'\0' * 512); call(0x4b14, 1, d1, 0, 0)

w = wave.open(WAV); assert w.getsampwidth() == 2 and w.getnchannels() == 2, 'need 16-bit stereo WAV'
frames = np.frombuffer(w.readframes(int(w.getframerate() * SECONDS)), '<i2').reshape(-1, 2); n = len(frames)
print('input: %s, %d frames (%.1f s) at %d Hz' % (os.path.basename(WAV), n, n / w.getframerate(), w.getframerate()))
be = frames.astype('>i2').tobytes()
def snr(x, y): x = x.astype(float); y = y.astype(float); e = ((x - y) ** 2).sum(); return 999.0 if e == 0 else 10 * np.log10((x ** 2).sum() / e)

def pack5():
    CH = 4096; out = bytearray(); dec = bytearray(); st = [b'\0\0\0\0', b'\0\0\0\0']; dst = [b'\0\0\0\0', b'\0\0\0\0']
    for s in range(0, n, CH):
        m = min(CH, n - s); uc.mem_write(IN, be[s * 4:(s + m) * 4]); uc.mem_write(OUTM, b'\0' * (m + 8))
        for c in (0, 1): st[c] = call_state(0x8586, m, IN + 2 * c, OUTM, c, st[c])
        blk = bytes(uc.mem_read(OUTM, m)); out += blk
        uc.mem_write(IN, blk); uc.mem_write(OUTM, b'\0' * (m * 4 + 8))
        for c in (0, 1): dst[c] = call_state(0x866e, m, IN, OUTM + 2 * c, c, dst[c])
        dec += bytes(uc.mem_read(OUTM, m * 4))
    return b'DVSM\0\0' + struct.pack('>HHBBI', 16, 7, 5, 1, 0) + bytes(out), bytes(dec)
def blocked(pack, enc, dec_entry, init_d1, fpb, enc_bytes):
    run_init(init_d1); out = bytearray(); dec = bytearray()
    for s in range(0, n, fpb):
        m = min(fpb, n - s); uc.mem_write(IN, be[s * 4:(s + m) * 4]); uc.mem_write(OUTM, b'\0' * (8 * m + 64))
        uc.mem_write(0x4aa68, b'\0\0\0\0'); uc.mem_write(0x4aae8, struct.pack('>I', 1 << 24))
        call(enc, 2 * m, 0, IN, OUTM); blk = bytes(uc.mem_read(OUTM, enc_bytes(m))); out += blk
        uc.mem_write(IN, blk); uc.mem_write(0x400000, b'\0' * (8 * m + 64))
        uc.mem_write(0x4aa68, b'\0\0\0\0'); uc.mem_write(0x4aae8, struct.pack('>I', 1 << 24))
        call(dec_entry, 0, len(blk), IN, 0x400000); dec += bytes(uc.mem_read(0x400000, 4 * m))
    return b'DVSM\0\0' + struct.pack('>HHBBI', 16, 7, pack, 1, 1004) + bytes(out), bytes(dec)

x = frames.reshape(-1)
bad = 0
for name, make in (('pack5', pack5),
                   ('pack4', lambda: blocked(4, 0x4d1e, 0x7252, 4, 1001, lambda m: 4 + (m - 1))),
                   ('pack2', lambda: blocked(2, 0x4bf0, 0x716a, 2, 501, lambda m: 4 + 2 * (m - 1)))):
    dvs, ref = make(); path = os.path.join(OUT, name + '.dvs'); open(path, 'wb').write(dvs)
    ours = subprocess.run([CLI, path, path + '.f32'], capture_output=True, text=True)
    if ours.returncode: print(name, 'CLI FAILED', ours.stdout[-200:], ours.stderr[-200:]); bad += 1; continue
    got = np.round(np.fromfile(path + '.f32', '<f4') * 32768.0).astype('<i2'); want = np.frombuffer(ref, '>i2').astype('<i2')
    same = len(got) == len(want) and bool((got == want).all())
    print('%-6s %8d bytes (%.0f%% of PCM)  program round trip %.1f dB  ours %s the program decoder%s' % (
        name, len(dvs), 100.0 * len(dvs) / (n * 4), snr(x, want[:len(x)]), 'IDENTICAL to' if same else 'DIFFERS from',
        '' if same else '  (lengths %d / %d, first difference at %s)' % (len(got), len(want), int(np.argmax(got[:min(len(got), len(want))] != want[:min(len(got), len(want))])) if len(got) and len(want) else -1)))
    bad += 0 if same else 1
sys.exit(1 if bad else 0)
