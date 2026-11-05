/* SBR-54w: diagnostic-only complex source QMF exporter from shared core PCM.
 * Input: exact finite f32le mono AAC-LC core PCM, 1024 samples/frame.
 * Output: 32 slots x 4 bands x (Re,Im) float32 LE per frame.
 * QMF source W[t-6], including previous-frame 6-slot lookback.
 * Initial missing preroll explicitly emitted as zeros.
 * Not an FFmpeg exporter, PCM decoder, or reference conformance proof.
 */
#include "sauti_aac_sbr_qmf_analysis.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if !defined(__STDC_IEC_559__)
/* Build still validates float size and endianness at runtime. */
#endif
#define FRAME 1024u
#define SLOTS 32u
#define BANDS 32u
#define WORDS (SLOTS*4u*2u)
static int little_endian(void){
    const uint32_t x=1u;
    return ((const unsigned char*)&x)[0]==1u;
}
int main(int argc,char **argv){
    if(argc!=3){
        fprintf(stderr,"usage: %s core_pcm.f32le output.qmf.f32le\n",argv[0]);
        return 2;
    }
    if(sizeof(float)!=4u || !little_endian()){
        fputs("requires IEEE-754 compatible 32-bit float LE host\n",stderr);
        return 2;
    }
    FILE *in=fopen(argv[1],"rb");
    if(!in){perror(argv[1]);return 2;}
    if(fseek(in,0,SEEK_END)!=0){perror("seek");fclose(in);return 2;}
    long size=ftell(in);
    if(size<0 || size%(long)(FRAME*sizeof(float))!=0 ||
       size<(long)(2u*FRAME*sizeof(float)) ||
       fseek(in,0,SEEK_SET)!=0){
        fputs("input must contain at least two complete 1024-sample f32le frames\n",stderr);
        fclose(in);return 2;
    }
    /* Never truncate the input via identical input/output pathname. */
    if(!strcmp(argv[1],argv[2])){
        fputs("input and output must differ\n",stderr);
        fclose(in);return 2;
    }
    FILE *out=fopen(argv[2],"wb");
    if(!out){perror(argv[2]);fclose(in);return 2;}
    SautiAacSbrQmfAnalysisState state={0};
    SautiAacSbrComplex tail[6u*32u]={0};
    uint64_t frames=0u;
    int ok=1;
    float pcm[FRAME],words[WORDS];
    SautiAacSbrComplex now[SLOTS*BANDS];
    while(ok && fread(pcm,sizeof(float),FRAME,in)==FRAME){
        for(unsigned j=0;j<FRAME;j++)
            if(!isfinite(pcm[j])){ok=0;break;}
        if(!ok)break;
        if(!sauti_aac_sbr_qmf_analyze_32(
                pcm,FRAME,SLOTS,BANDS,now,SLOTS*BANDS,&state)){
            ok=0;break;
        }
        for(unsigned slot=0;slot<SLOTS;slot++)
            for(unsigned b=0;b<4u;b++){
                SautiAacSbrComplex z={0};
                if(slot<6u){
                    if(frames)z=tail[slot*32u+b+2u];
                }else z=now[(slot-6u)*32u+b+2u];
                words[(slot*4u+b)*2u]=z.re;
                words[(slot*4u+b)*2u+1u]=z.im;
            }
        if(fwrite(words,sizeof(float),WORDS,out)!=WORDS){ok=0;break;}
        memcpy(tail,now+(SLOTS-6u)*BANDS,sizeof(tail));
        frames++;
    }
    if(ferror(in) || fclose(out)!=0)ok=0;
    fclose(in);
    if(!ok){
        (void)remove(argv[2]);
        fputs("SBR-54w export aborted; partial output removed\n",stderr);
        return 2;
    }
    printf("SBR-54w Sauti W[i-6] LOW 2..5 exported frames=%llu, samples=%llu (NOT FFmpeg/PCM)\n",
           (unsigned long long)frames,
           (unsigned long long)(frames*SLOTS));
    return 0;
}
