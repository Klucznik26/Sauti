#!/usr/bin/env python3
"""Plays a ProTracker module with a REAL Amiga player (BastyPlayer 1.00, m68k, CLI, plain Paula) on the emulated Paula of Amiberry + the AROS ROM, and saves the captured stereo audio.
Local tool, nothing from it is stored in the repository: BastyPlayer (Sebastian Vater, "All Rights Reserved", Aminet mus/play/cdgs_bastyplayer.lha) is downloaded by the caller.
Setup (all user level, no sudo): flatpak install --user flathub com.blitterstudio.amiberry (ships the AROS ROM); WORK/dh0/Utils/BastyPlayer, WORK/dh0/S/Startup-Sequence (written here), WORK/t.uae:
  kickstart_rom_file / kickstart_ext_rom_file = <flatpak>/files/share/amiberry/roms/aros-{rom,ext}.bin, cpu_model=68020, chipset=aga, chipmem_size=4, fastmem_size=8,
  filesystem2=rw,DH0:dh0:WORK/dh0,1, sound_output=exact, sound_channels=stereo, sound_frequency=48000, sound_interpol=none, sound_filter=off, sound_stereo_separation=10, sound_stereo_delay=0.
Audio goes to WORK/sdlaudio.raw via SDL_AUDIODRIVER=disk (no sound device needed); the emulator is run from the flatpak files (LD_LIBRARY_PATH) so that its IPC socket /run/user/UID/amiberry.sock is visible:
commands are TAB-separated (`RESET<TAB>HARD`, `SCREENSHOT<TAB>/path.png` = the emulator window only).
usage: scripts/amiberry_render_mod.py WORK IN.mod SECONDS OUT.npy       (OUT.npy: float stereo, 48 kHz, from the first audible sample)"""
import os, shutil, socket, subprocess, sys, time
import numpy as np
W = os.path.abspath(sys.argv[1]); MOD = sys.argv[2]; SECS = float(sys.argv[3]); OUT = sys.argv[4]
F = subprocess.check_output(['flatpak', 'info', '--user', '--show-location', 'com.blitterstudio.amiberry']).decode().strip() + '/files'
def ipc(cmd):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.settimeout(5); s.connect('/run/user/%d/amiberry.sock' % os.getuid()); s.sendall((cmd + '\n').encode()); r = s.recv(4096).decode(); s.close(); return r
raw = os.path.join(W, 'sdlaudio.raw')
if os.path.exists(raw): os.remove(raw)
env = dict(os.environ, LD_LIBRARY_PATH=F + '/lib', SDL_AUDIODRIVER='disk')
subprocess.Popen([F + '/bin/amiberry', '-f', os.path.join(W, 't.uae'), '-G'], cwd=W, env=env, stdout=open(os.path.join(W, 'amiberry.log'), 'w'), stderr=subprocess.STDOUT)
for _ in range(60):
    time.sleep(1)
    try: ipc('PING'); break
    except Exception: pass
shutil.copy(MOD, os.path.join(W, 'dh0', 't.mod')); open(os.path.join(W, 'dh0', 'S', 'Startup-Sequence'), 'w').write('Utils/BastyPlayer -lt.mod -p -w1\n')
ipc('RESET\tHARD'); time.sleep(2); base = os.path.getsize(raw); t0 = time.time(); onset = None
while time.time() - t0 < SECS + 60:
    time.sleep(2); a = np.fromfile(raw, '<i2')[base // 2:]; a = a[:len(a) // 2 * 2].reshape(-1, 2); nz = np.nonzero(np.abs(a).max(1) > 60)[0]
    if len(nz) and onset is None: onset = nz[0]
    if onset is not None and len(a) - onset > 48000 * (SECS + 1): break
ipc('QUIT')
if onset is None: sys.exit('no audio')
np.save(OUT, a[onset:onset + int(48000 * (SECS + 1))].astype(float) / 32768)
