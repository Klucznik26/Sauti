/* Test oracle (tool, not a product): the reference decoder FAAD2 (libfaad.so.2, dlopen) on raw access units with an AudioSpecificConfig (AAC-LD / ELD / ELD+SBR ...), float32 interleaved out.
 * usage: sauti_faad_raw_oracle ASC.bin AUS.bin OUT.f32   (inputs from tools/mp4_dump_aus.py; build: cc -O2 -o build/sauti_faad_raw_oracle tools/sauti_faad_raw_oracle.c -ldl). */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void *H;
typedef struct { unsigned long bytesconsumed,samples; unsigned char channels,error; unsigned long samplerate; unsigned char sbr,object_type,header_type,nfront,nside,nback,nlfe,chpos[64],ps; } Info;
typedef struct { unsigned char defObjectType; unsigned long defSampleRate; unsigned char outputFormat,downMatrix,useOldADTSFormat,dontUpSampleImplicitSBR; } Cfg;
static unsigned char *slurp(const char *p,long *n){FILE *f=fopen(p,"rb");unsigned char *b;if(!f)return NULL;fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);b=malloc((size_t)*n+1);if(fread(b,1,(size_t)*n,f)!=(size_t)*n)return NULL;fclose(f);return b;}
int main(int argc,char **argv){
    void *lib=dlopen("libfaad.so.2",RTLD_NOW);long asc_n,aus_n;unsigned char *asc,*aus;
    if(argc!=4||!lib){fprintf(stderr,"usage / no libfaad\n");return 2;}
    H (*open_)(void)=dlsym(lib,"NeAACDecOpen");
    Cfg *(*getc_)(H)=dlsym(lib,"NeAACDecGetCurrentConfiguration");
    unsigned char (*setc)(H,Cfg*)=dlsym(lib,"NeAACDecSetConfiguration");
    char (*init2)(H,unsigned char*,unsigned long,unsigned long*,unsigned char*)=dlsym(lib,"NeAACDecInit2");
    void *(*dec)(H,Info*,unsigned char*,unsigned long)=dlsym(lib,"NeAACDecDecode");
    asc=slurp(argv[1],&asc_n);aus=slurp(argv[2],&aus_n);if(!asc||!aus)return 2;
    H h=open_();Cfg *c=getc_(h);c->outputFormat=4;c->downMatrix=0;c->dontUpSampleImplicitSBR=0;setc(h,c);
    unsigned long rate=0;unsigned char ch=0;
    if(init2(h,asc,(unsigned long)asc_n,&rate,&ch)<0){fprintf(stderr,"init2 failed\n");return 3;}
    FILE *o=fopen(argv[3],"wb");long pos=0,frames=0,errs=0;
    while(pos+4<=aus_n){
        unsigned long len=((unsigned long)aus[pos]<<24)|((unsigned long)aus[pos+1]<<16)|((unsigned long)aus[pos+2]<<8)|aus[pos+3];Info fi;float *pcm;
        pos+=4;if(pos+(long)len>aus_n)break;
        memset(&fi,0,sizeof fi);pcm=dec(h,&fi,aus+pos,len);
        if(fi.error){errs++;if(errs<4)fprintf(stderr,"frame %ld error %u\n",frames,fi.error);}
        else if(pcm&&fi.samples){fwrite(pcm,sizeof(float),fi.samples,o);if(!frames)fprintf(stderr,"rate %lu ch %u samples/frame %lu sbr %u object %u\n",fi.samplerate,fi.channels,fi.samples,fi.sbr,fi.object_type);frames++;}
        pos+=(long)len;
    }
    fclose(o);fprintf(stderr,"frames %ld errors %ld\n",frames,errs);return errs?1:0;}
