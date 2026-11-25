#!/usr/bin/env python3
"""Fixtures dvsm_winrec_real_pack2.* and dvsm_winrec_real_pack4.*: the FIRST BLOCKS of files that WinRec Pro 2.11H itself WROTE (recorder, Optionen / Dateiformat "DVS gepackt 50 %" = Pack 2 and
"DVS gepackt 25 %" = Pack 4) in Hatari, with a synthetic stereo signal (a chirp on the left input, a 330 Hz tone on the right, a square burst, noise) fed to the emulated Falcon microphone
(`hatari --mic`, the audio taken from a PipeWire null sink; see docs/ATARI_HW_FIXTURES.md).  The expected PCM is made by the program's own DECODER routines (Pack 2 at 0x716a, Voice at 0x7252) run in a
Unicorn M68K emulation, block by block; the Sauti decoder must reproduce it bit for bit.  Nothing but the OUTPUT of the routines is stored (WIN_REC.PRG stays outside the repository).
Notable: WinRec records Pack 4 with the INPUT RIGHT channel in the high nibble / first seed (Pack 2: input left first), and its own playback gives the same channel order as our decoder.
usage: tests/data/make_dvsm_real_fixtures.py REAL_PACK2.dvs REAL_PACK4.dvs [WIN_REC.PRG]   (writes next to the script)"""
import os, struct, sys
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
from unicorn.m68k_const import UC_CPU_M68K_M68040, UC_M68K_REG_A0, UC_M68K_REG_A1, UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_D1
here = os.path.dirname(os.path.abspath(__file__)); P2, P4 = sys.argv[1], sys.argv[2]
PRG = sys.argv[3] if len(sys.argv) > 3 else os.path.expanduser('~/Sauti-tools/winrec-pro2/x/WINRECUT.PRO/WINREC.PRO/WIN_REC.PRG')
def load(fn):
    d = open(fn, 'rb').read(); _, tl, dl, bl, sl, _, _, _ = struct.unpack('>HIIIIIIH', d[:28]); img = bytearray(d[28:28 + tl + dl]); rel = d[28 + tl + dl + sl:]
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
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_M68040); uc.mem_map(0, 0x600000); uc.mem_write(0, bytes(load(PRG)))
STACK, SENT, IN, OUT, TAB = 0x500000, 0x4f0000, 0x300000, 0x400000, 0x297e6
uc.mem_write(SENT, b'\x4e\x71')
def call(entry, d0, d1, a0, a1):
    sp = STACK - 4; uc.mem_write(sp, struct.pack('>I', SENT)); uc.reg_write(UC_M68K_REG_A7, sp); uc.reg_write(UC_M68K_REG_D0, d0); uc.reg_write(UC_M68K_REG_D1, d1)
    uc.reg_write(UC_M68K_REG_A0, a0); uc.reg_write(UC_M68K_REG_A1, a1); uc.emu_start(entry, SENT, count=400_000_000)
def make(src, name, nblocks, entry, init_d1, frames_of):
    d = open(src, 'rb').read(); off = struct.unpack('>H', d[6:8])[0]; blk = struct.unpack('>I', d[12:16])[0]; pack = d[10]
    body = d[off:off + nblocks * blk]; uc.mem_write(TAB, b'\0' * 512); call(0x4b14, 1, init_d1, 0, 0); pcm = b''
    for s in range(0, len(body), blk):
        b = body[s:s + blk]; m = frames_of(len(b)); uc.mem_write(IN, b); uc.mem_write(OUT, b'\0' * (4 * m + 64))
        uc.mem_write(0x4aa68, b'\0\0\0\0'); uc.mem_write(0x4aae8, struct.pack('>I', 1 << 24)); call(entry, 0, len(b), IN, OUT); pcm += bytes(uc.mem_read(OUT, 4 * m))
    open(os.path.join(here, name + '.dvs'), 'wb').write(d[:off + len(body)]); open(os.path.join(here, name + '.pcm'), 'wb').write(pcm)
    print(name, 'pack', pack, 'block', blk, 'bytes', off + len(body), 'frames', len(pcm) // 4)
make(P2, 'dvsm_winrec_real_pack2', 2, 0x716a, 2, lambda n: 1 + (n - 4) // 2)
make(P4, 'dvsm_winrec_real_pack4', 3, 0x7252, 4, lambda n: 1 + (n - 4))
