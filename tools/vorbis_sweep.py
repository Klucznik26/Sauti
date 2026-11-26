#!/usr/bin/env python3
"""libvorbis (through the ffmpeg encoder) at many sample rates, channel counts, qualities and signals, decoded by build/maktabasauti_cli and libvorbisfile: the maximum difference must be below 2e-6.
Test tool (not a product) of the Vorbis audit, see docs/VORBIS.md.  Needs libvorbisfile.so.3 (the reference decoder, through ctypes); run from the repository root with `python3 -I`."""
import subprocess,sys,os,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)));import vorbis_ref as vf
CLI='build/maktabasauti_cli';T=(os.environ.get('TMPDIR') or '/tmp')+'/vorbis_sweep/';os.makedirs(T,exist_ok=True)
layouts={1:'mono',2:'stereo',3:'3.0',4:'quad',5:'5.0',6:'5.1',7:'6.1',8:'7.1'}
rng=np.random.default_rng(1)
def gen(rate,ch,secs,kind):
    n=int(rate*secs);t=np.arange(n)/rate
    if kind=='noise': x=0.3*rng.standard_normal((n,ch))
    elif kind=='tones': x=np.stack([0.3*np.sin(2*np.pi*(220*(c+1))*t)+0.1*np.sin(2*np.pi*(3000+100*c)*t)*(np.sin(2*np.pi*1.3*t)>0) for c in range(ch)],1)
    elif kind=='click': x=np.zeros((n,ch));x[::max(1,rate//7)]=0.8;x+=0.002*rng.standard_normal((n,ch))
    elif kind=='silence': x=np.zeros((n,ch))
    else: x=0.2*np.stack([np.sin(2*np.pi*(100+50*c)*t*(1+0.5*np.sin(t))) for c in range(ch)],1)+0.02*rng.standard_normal((n,ch))
    return (np.clip(x,-1,1)*32000).astype('<i2')
rows=[];bad=0
cfgs=[]
for rate in (8000,11025,22050,32000,44100,48000,96000):
    for ch in (1,2): cfgs.append((rate,ch,'-q:a 3','tones'))
for ch in range(3,9): cfgs.append((44100,ch,'-q:a 4','tones'))
for q in ('-q:a -1','-q:a 0','-q:a 6','-q:a 10','-b:a 32k','-b:a 192k','-b:a 500k'):
    cfgs.append((44100,2,q,'noise'));cfgs.append((44100,2,q,'click'))
cfgs.append((44100,2,'-q:a 5','silence'));cfgs.append((48000,6,'-q:a 5','noise'));cfgs.append((16000,1,'-q:a 5','mixed'))
for i,(rate,ch,q,kind) in enumerate(cfgs):
    raw=T+'in.raw';gen(rate,ch,3.1,kind).tofile(raw);ogg=T+f'c{i}.ogg'
    r=subprocess.run(['ffmpeg','-v','error','-y','-f','s16le','-ar',str(rate),'-ac',str(ch),'-i',raw]+q.split()+['-c:a','libvorbis',ogg],capture_output=True,text=True)
    if r.returncode or not os.path.exists(ogg): print(i,rate,ch,q,kind,'ENC-FAIL',r.stderr.strip()[-60:]);continue
    try: x,_,tot=vf.decode(ogg)
    except Exception as e: print(i,'vf fail',e);continue
    o=subprocess.run([CLI,ogg,T+'o.f32'],capture_output=True,text=True)
    if o.returncode: print(i,rate,ch,q,kind,'OURS-FAIL',(o.stdout+o.stderr).strip()[-80:]);bad+=1;continue
    a=np.fromfile(T+'o.f32','<f4').reshape(-1,ch)
    d=np.abs(x-a).max() if len(a)==len(x) else 99
    flag='OK' if d<2e-6 else 'BAD';bad+=flag=='BAD'
    print(f'{i:2d} {rate:6d} {ch}ch {q:9s} {kind:8s} frames {len(a)}/{len(x)} maxdiff {d:.2e} {flag}',flush=True)
print('bad',bad)
