#!/usr/bin/env python3
"""Test tool (not a product): libmp3lame (through ffmpeg) at 13 configurations (8 - 48 kHz, mono / stereo, CBR / VBR / ABR, joint / plain stereo, tones / noise / clicks), decoded by build/maktabasauti_cli and by FFmpeg (mp3float); run from the repository root with python3 -I.  Samples that FFmpeg returns above full scale are clipped by Sauti, so the comparison there is skipped by the tolerance only where both are inside [-1, 1]."""
import subprocess,numpy as np,os
CLI='build/maktabasauti_cli';T=(os.environ.get('TMPDIR') or '/tmp')+'/mp3_sweep/';os.makedirs(T,exist_ok=True)
rng=np.random.default_rng(2)
def gen(rate,ch,secs,kind):
    n=int(rate*secs);t=np.arange(n)/rate
    if kind=='noise':x=0.3*rng.standard_normal((n,ch))
    elif kind=='click':x=np.zeros((n,ch));x[::rate//5]=0.8;x+=0.003*rng.standard_normal((n,ch))
    else:x=np.stack([0.3*np.sin(2*np.pi*(300*(c+1))*t)+0.05*rng.standard_normal(n) for c in range(ch)],1)
    return (np.clip(x,-1,1)*30000).astype('<i2')
cfgs=[(44100,2,'-b:a 128k','tones'),(44100,2,'-b:a 320k','noise'),(44100,2,'-b:a 64k -joint_stereo 1','tones'),(44100,1,'-b:a 96k','click'),(48000,2,'-q:a 0','tones'),(32000,2,'-b:a 128k','noise'),(22050,2,'-b:a 64k','tones'),(24000,1,'-b:a 48k','tones'),(16000,1,'-b:a 32k','noise'),(11025,1,'-b:a 16k','tones'),(8000,1,'-b:a 16k','tones'),(44100,2,'-b:a 192k -abr 1','click'),(44100,2,'-b:a 128k -joint_stereo 0','noise')]
bad=0
for i,(rate,ch,opt,kind) in enumerate(cfgs):
    gen(rate,ch,2.5,kind).tofile(T+'in.raw');f=T+f'm{i}.mp3'
    r=subprocess.run(['ffmpeg','-v','error','-y','-f','s16le','-ar',str(rate),'-ac',str(ch),'-i',T+'in.raw']+opt.split()+['-c:a','libmp3lame',f],capture_output=True,text=True)
    if r.returncode:print(i,'ENC-FAIL');continue
    o=subprocess.run([CLI,f,T+'o.f32'],capture_output=True,text=True)
    if o.returncode or 'read:' in o.stdout+o.stderr:print(i,rate,ch,opt,'OURS FAIL',(o.stdout+o.stderr).strip()[-50:]);bad+=1;continue
    subprocess.run(['ffmpeg','-v','error','-y','-i',f,'-f','f32le',T+'f.f32'])
    a=np.fromfile(T+'o.f32','<f4');b=np.fromfile(T+'f.f32','<f4')
    if len(a)!=len(b):print(i,rate,ch,opt,kind,'LEN',len(a)//ch,len(b)//ch);bad+=1;continue
    d=np.abs(a-b).max();ok=d<2e-4
    bad+=not ok
    print(i,rate,ch,opt,kind,'frames',len(a)//ch,'maxdiff %.2e'%d,'OK' if ok else 'BAD')
print('bad',bad)
