/* SBR-88 tool (not a product): in-process mutation fuzzer for the native AAC adapters (build it with -fsanitize=address,undefined).
 * Every iteration mutates a seed file (bit flips, byte overwrites, a zeroed or random-filled block, a duplicated block, a truncation), opens it through the
 * registry, reads it completely (bounded), seeks a few times and requires: no sanitizer report, no crash, finite samples, no endless loop (a global
 * alarm), and seek/read consistency is NOT checked here (the file is corrupt on purpose).
 * usage: sauti_aac_fuzz ITERATIONS SEED FILE...    */
#include "sauti_decoder.h"
#include "sauti_decoder_aac_native.h"
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned long long rng;
static unsigned long long rnd(void){rng=rng*6364136223846793005ull+1442695040888963407ull;return rng>>16;}
static void on_alarm(int s){(void)s;static const char m[]="FUZZ TIMEOUT: a decode did not finish\n";if(write(2,m,sizeof m-1u)<0){}_exit(3);}

int main(int argc,char **argv){
    if(argc<4)return 2;
    const long iterations=atol(argv[1]);rng=(unsigned long long)atoll(argv[2])*0x9e3779b97f4a7c15ull+1u;
    const int nfiles=argc-3;
    uint8_t **seed=calloc((size_t)nfiles,sizeof(*seed));size_t *len=calloc((size_t)nfiles,sizeof(*len));
    for(int i=0;i<nfiles;i++){
        FILE *f=fopen(argv[3+i],"rb");if(!f){perror(argv[3+i]);return 2;}
        fseek(f,0,SEEK_END);len[i]=(size_t)ftell(f);fseek(f,0,SEEK_SET);
        seed[i]=malloc(len[i]);if(!seed[i]||fread(seed[i],1,len[i],f)!=len[i])return 2;fclose(f);
    }
    const SautiDecoderOps *const registry[]={&sauti_decoder_aac_native_ops,&sauti_decoder_aac_mono_native_ops,&sauti_decoder_aac_multi_native_ops,
        &sauti_decoder_aac_mp4_native_ops,&sauti_decoder_aac_latm_native_ops,&sauti_decoder_aac_he_native_ops,&sauti_decoder_aac_he_cpe_native_ops};
    signal(SIGALRM,on_alarm);
    static float buf[4096*2];
    long opened=0,rejected=0;unsigned long long frames_read=0;
    for(long it=0;it<iterations;it++){
        const int si=(int)(rnd()%(unsigned)nfiles);
        size_t n=len[si];uint8_t *m=malloc(n+4096u);
        memcpy(m,seed[si],n);
        const unsigned kind=(unsigned)(rnd()%6u);
        const unsigned count=1u+(unsigned)(rnd()%24u);
        for(unsigned k=0;k<count;k++){
            const size_t at=(size_t)(rnd()%n);
            switch(kind){
                case 0:m[at]^=(uint8_t)(1u<<(rnd()&7u));break;                                   /* bit flips */
                case 1:m[at]=(uint8_t)rnd();break;                                               /* byte overwrites */
                case 2:{size_t l=1u+(size_t)(rnd()%64u);if(at+l>n)l=n-at;memset(m+at,0,l);}break;/* zeroed block */
                case 3:{size_t l=1u+(size_t)(rnd()%64u);if(at+l>n)l=n-at;for(size_t j=0;j<l;j++)m[at+j]=(uint8_t)rnd();}break;
                case 4:{size_t l=1u+(size_t)(rnd()%512u);size_t to=(size_t)(rnd()%n);if(at+l>n)l=n-at;if(to+l>n)l=n-to;memmove(m+to,m+at,l);}break;
                default:if(n>16u)n=16u+(size_t)(rnd()%(n-16u));break;                             /* truncation */
            }
            if(kind>=4u&&k>0u)break;
        }
        /* mutate mostly the frame data, but keep the first frame's header intact half of the time so deeper code is reached */
        if(rnd()&1u&&n>64u)memcpy(m,seed[si],32u);
        SautiDecoder d;
        alarm(20);
        if(sauti_decoder_open(&d,registry,sizeof registry/sizeof registry[0],m,n)){
            opened++;
            const unsigned ch=d.info.channels;
            size_t got,total=0;
            while((got=sauti_decoder_read(&d,buf,2048u))>0u){
                for(size_t i=0;i<got*ch;i++)if(!isfinite(buf[i])){fprintf(stderr,"FUZZ NONFINITE sample, iteration %ld seed %s kind %u\n",it,argv[3+si],kind);return 4;}
                total+=got;frames_read+=got;
                if(total>(size_t)40u*1000u*1000u)break;
            }
            for(int s=0;s<3;s++){
                const uint64_t pos=d.info.total_frames?(rnd()%(d.info.total_frames+1u)):0u;
                if(sauti_decoder_seek(&d,pos)){
                    got=sauti_decoder_read(&d,buf,2048u);
                    for(size_t i=0;i<got*ch;i++)if(!isfinite(buf[i])){fprintf(stderr,"FUZZ NONFINITE sample after seek, iteration %ld\n",it);return 4;}
                }
            }
            sauti_decoder_close(&d);
        }else rejected++;
        alarm(0);
        free(m);
    }
    printf("fuzz: %ld iterations, %ld opened, %ld rejected, %llu frames decoded, no sanitizer report\n",iterations,opened,rejected,frames_read);
    for(int i=0;i<nfiles;i++)free(seed[i]);
    free(seed);free(len);
    return 0;
}
