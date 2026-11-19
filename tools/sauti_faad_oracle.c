/* Test oracle (tool, not a product): the reference decoder FAAD2 (libfaad.so.2, loaded with dlopen, no header needed): ADTS in, float32 interleaved out.
 * usage: sauti_faad_oracle IN.aac OUT.f32   (build: cc -O2 -o build/sauti_faad_oracle tools/sauti_faad_oracle.c -ldl).  Its PNS noise generator and PS decorrelator
 * differ from FFmpeg's and Sauti's, so it is used for deterministic features only (PS phase rotation with ICC = 1, see scripts/test_he_ps_phase_vs_faad2.sh). */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void *H;
typedef struct { unsigned long bytesconsumed,samples; unsigned char channels,error; unsigned long samplerate; unsigned char sbr,object_type,header_type,nfront,nside,nback,nlfe,chpos[64],ps; } Info;
typedef struct { unsigned char defObjectType; unsigned long defSampleRate; unsigned char outputFormat,downMatrix,useOldADTSFormat,dontUpSampleImplicitSBR; } Cfg;
int main(int argc,char **argv){(void)argc;
    void *lib=dlopen("libfaad.so.2",RTLD_NOW);if(!lib){fprintf(stderr,"no libfaad\n");return 2;}
    H (*open_)(void)=dlsym(lib,"NeAACDecOpen");
    Cfg *(*getc_)(H)=dlsym(lib,"NeAACDecGetCurrentConfiguration");
    unsigned char (*setc)(H,Cfg*)=dlsym(lib,"NeAACDecSetConfiguration");
    long (*init)(H,unsigned char*,unsigned long,unsigned long*,unsigned char*)=dlsym(lib,"NeAACDecInit");
    void *(*dec)(H,Info*,unsigned char*,unsigned long)=dlsym(lib,"NeAACDecDecode");
    FILE *f=fopen(argv[1],"rb");fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    unsigned char *b=malloc((size_t)n);if(fread(b,1,(size_t)n,f)!=(size_t)n)return 2;fclose(f);
    H h=open_();Cfg *c=getc_(h);c->outputFormat=4;c->downMatrix=0;c->dontUpSampleImplicitSBR=0;setc(h,c);
    unsigned long rate=0;unsigned char ch=0;
    long used=init(h,b,(unsigned long)n,&rate,&ch);if(used<0){fprintf(stderr,"init failed\n");return 3;}
    FILE *o=fopen(argv[2],"wb");size_t pos=(size_t)used;long frames=0,errs=0;
    while(pos<(size_t)n){
        Info fi;memset(&fi,0,sizeof fi);
        float *pcm=dec(h,&fi,b+pos,(unsigned long)(n-pos));
        if(fi.error){errs++;if(errs>5)break;if(fi.bytesconsumed==0)break;}
        pos+=fi.bytesconsumed?fi.bytesconsumed:1;
        if(pcm&&fi.samples){fwrite(pcm,sizeof(float),fi.samples,o);frames++;if(frames==1)fprintf(stderr,"rate %lu ch %u samples/frame %lu sbr %u ps %u\n",fi.samplerate,fi.channels,fi.samples,fi.sbr,fi.ps);}
    }
    fclose(o);fprintf(stderr,"frames %ld errors %ld\n",frames,errs);return 0;}
