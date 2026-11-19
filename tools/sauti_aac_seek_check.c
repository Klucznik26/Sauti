/* SBR-86 tool (not a product): opens a file through the native AAC adapters, reads it completely, then seeks to many positions (frame starts,
 * offsets inside a frame, the last frame, the end, backwards and forwards) and requires the samples read after each seek to be bit-identical to
 * the sequential read.  usage: sauti_aac_seek_check FILE.aac [SEEKS=200] */
#include "sauti_decoder.h"
#include "sauti_decoder_aac_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv){
    if(argc<2)return 2;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror(argv[1]);return 2;}
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t *bytes=malloc((size_t)n);
    if(!bytes || fread(bytes,1,(size_t)n,f)!=(size_t)n)return 2;
    fclose(f);
    const SautiDecoderOps *const registry[]={&sauti_decoder_aac_native_ops,&sauti_decoder_aac_mono_native_ops,&sauti_decoder_aac_multi_native_ops,
        &sauti_decoder_aac_mp4_native_ops,&sauti_decoder_aac_latm_native_ops,&sauti_decoder_aac_adif_native_ops,&sauti_decoder_aac_he_native_ops,&sauti_decoder_aac_he_cpe_native_ops};
    SautiDecoder d;
    if(!sauti_decoder_open(&d,registry,sizeof registry/sizeof registry[0],bytes,(size_t)n)){puts("REJECT");return 1;}
    const unsigned ch=d.info.channels;
    const uint64_t total=d.info.total_frames;
    float *all=malloc((size_t)total*ch*sizeof(float));
    if(!all)return 2;
    size_t got=0,k;
    static float buf[4096*8];
    while((k=sauti_decoder_read(&d,buf,4096u))>0u){memcpy(all+got*ch,buf,k*ch*sizeof(float));got+=k;}
    if(got!=total){printf("FAIL: sequential read gave %zu of %llu frames\n",got,(unsigned long long)total);return 1;}
    const int seeks=argc>2?atoi(argv[2]):200;
    unsigned bad=0;unsigned long long rng=0x9e3779b97f4a7c15ull;
    for(int i=0;i<seeks;i++){
        rng=rng*6364136223846793005ull+1442695040888963407ull;
        uint64_t pos;
        switch(i%5){
            case 0:pos=(rng>>20)%total;break;                          /* anywhere */
            case 1:pos=((rng>>20)%(total/2048u))*2048u;break;          /* a frame start */
            case 2:pos=total>5000u?total-1u-((rng>>20)%4000u):0u;break;/* near the end */
            case 3:pos=(rng>>20)%(total<30000u?total:30000u);break;    /* near the start (backwards after a far seek) */
            default:pos=((rng>>20)%(total/256u+1u))*256u+((rng>>12)&255u);break;
        }
        size_t want=1500u;if(pos+want>total)want=(size_t)(total-pos);
        if(!sauti_decoder_seek(&d,pos)){printf("FAIL: seek(%llu) refused\n",(unsigned long long)pos);bad++;continue;}
        size_t r=sauti_decoder_read(&d,buf,want);
        if(r!=want || memcmp(buf,all+pos*ch,want*ch*sizeof(float))!=0){printf("FAIL: pos %llu read %zu of %zu, samples differ\n",(unsigned long long)pos,r,want);bad++;}
    }
    if(sauti_decoder_seek(&d,total)==0 || sauti_decoder_read(&d,buf,16u)!=0u){puts("FAIL: seek to the end");bad++;}
    printf("%s: %llu frames, %u channels, %d seeks, %u failures\n",d.ops->name,(unsigned long long)total,ch,seeks,bad);
    sauti_decoder_close(&d);
    return bad?1:0;
}
