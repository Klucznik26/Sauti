/* Standalone YM2/YM3/YM5/YM6 -> WAV16 audit renderer. Does not modify SDL playback. */
#include "sauti_decoder.h"
#include "sauti_decoder_ym.h"
#include "sauti_ym_archive.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int put16(FILE *f,uint32_t n){
 unsigned char x[2]={(unsigned char)n,(unsigned char)(n>>8)};
 return fwrite(x,1,2,f)==2;
}
static int put32(FILE *f,uint32_t n){
 return put16(f,n&65535u)&&put16(f,n>>16);
}
static int wav_header(FILE *out,uint32_t rate,uint16_t channels,uint32_t bytes){
 return fwrite("RIFF",1,4,out)==4&&put32(out,bytes+36)&&
        fwrite("WAVEfmt ",1,8,out)==8&&put32(out,16)&&
        put16(out,1)&&put16(out,channels)&&put32(out,rate)&&
        put32(out,rate*channels*2u)&&put16(out,channels*2u)&&
        put16(out,16)&&fwrite("data",1,4,out)==4&&put32(out,bytes);
}
int main(int argc,char **argv){
 if(argc!=3){fprintf(stderr,"usage: %s YM2-YM3-YM5-YM6-or-LHA output.wav\n",argv[0]);return 2;}
 int ok=0,created=0;
 uint8_t *bytes=NULL,*unpacked=NULL;FILE *out=NULL;
 SautiDecoder d={0};
 FILE *in=fopen(argv[1],"rb");
 if(!in){perror(argv[1]);return 1;}
 if(fseek(in,0,SEEK_END)!=0)goto done;
 long size=ftell(in);
 if(size<38||size>128L*1024L*1024L||fseek(in,0,SEEK_SET)!=0)goto done;
 bytes=malloc((size_t)size);
 if(!bytes||fread(bytes,1,(size_t)size,in)!=(size_t)size)goto done;
 fclose(in);in=NULL;
 size_t decoded_size=(size_t)size;
 int archive_result=sauti_ym_unpack_lha(bytes,(size_t)size,&unpacked,&decoded_size);
 if(archive_result<0){
   fprintf(stderr,"SAUTI YM: invalid or unsupported LHA archive\\n");goto done;
 }
 if(archive_result==0)
   decoded_size=(size_t)size; /* Raw YM: unpack helper cleared out_size. */
 if(archive_result==1)
   fprintf(stderr,"SAUTI YM: LHA unpacked %ld -> %zu bytes\\n",size,decoded_size);
 const SautiDecoderOps *ops[]={&sauti_decoder_ym_ops};
 if(!sauti_decoder_open(&d,ops,1,unpacked?unpacked:bytes,decoded_size)){
   fprintf(stderr,"YM2/YM3/YM5/YM6 unsupported or compressed: %s\n",argv[1]);goto done;
 }
 if(d.info.channels!=1||d.info.sample_rate==0||
    d.info.total_frames==0||d.info.total_frames>UINT32_MAX/2u){
   fprintf(stderr,"WAV size unsupported\n");goto done;
 }
 out=fopen(argv[2],"wb");
 if(out)created=1;
 if(!out){perror(argv[2]);goto done;}
 uint32_t pcm_bytes=(uint32_t)d.info.total_frames*2u;
 if(!wav_header(out,d.info.sample_rate,1,pcm_bytes))goto done;
 uint64_t total=0;float pcm[2048];int16_t samples[2048];
 while(total<d.info.total_frames){
  size_t want=(d.info.total_frames-total)<2048?
              (size_t)(d.info.total_frames-total):2048;
  size_t got=sauti_decoder_read(&d,pcm,want);
  if(!got||got>want){fprintf(stderr,"decoder ended early\n");goto done;}
  for(size_t i=0;i<got;i++){
   float v=pcm[i];
   if(!isfinite(v)){fprintf(stderr,"non-finite PCM\n");goto done;}
   if(v>1.0f)v=1.0f;
   if(v< -1.0f)v=-1.0f;
   float scaled=v*32767.0f;
   samples[i]=(int16_t)lrintf(scaled);
  }
  for(size_t i=0;i<got;i++){
   if(!put16(out,(uint16_t)samples[i]))goto done;
  }
  total+=got;
 }
 fprintf(stderr,"SAUTI YM: %u Hz, mono, %llu samples, %.2f seconds\n",
 d.info.sample_rate,(unsigned long long)total,(double)total/d.info.sample_rate);
 ok=1;
done:
 if(in)fclose(in);
 if(out&&fclose(out)!=0)ok=0;
 if(!ok&&created)remove(argv[2]);
 sauti_decoder_close(&d);
 free(bytes);
 free(unpacked);
 return ok?0:1;
}
