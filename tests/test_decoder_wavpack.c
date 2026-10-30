/* Focused WavPack probe and optional genuine-file decode/seek test.
 * Usage: ./build/test_decoder_wavpack [recording.wv] */
#include "sauti_decoder.h"
#include "sauti_decoder_wavpack.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
 const SautiDecoderOps *registry[]={&sauti_decoder_wavpack_ops};
 const uint8_t invalid[32]={'w','v','p','k'};
 SautiDecoder d={0};
 assert(sauti_decoder_wavpack_ops.probe(invalid,sizeof(invalid)));
 assert(!sauti_decoder_wavpack_ops.probe((const uint8_t*)"wvpk",3));
 assert(!sauti_decoder_wavpack_ops.probe((const uint8_t*)"RIFF",4));
 assert(!sauti_decoder_open(&d,registry,1,invalid,sizeof(invalid)));
 if(argc<2){puts("PASS: WavPack probe and malformed input rejection");return 0;}
 FILE *f=fopen(argv[1],"rb");if(!f){perror(argv[1]);return 1;}
 if(fseek(f,0,SEEK_END)!=0){fclose(f);return 1;}
 long len=ftell(f);
 if(len<32||len>64*1024*1024L||fseek(f,0,SEEK_SET)!=0){fclose(f);return 1;}
 uint8_t *data=malloc((size_t)len);if(!data){fclose(f);return 1;}
 if(fread(data,1,(size_t)len,f)!=(size_t)len){fclose(f);free(data);return 1;}
 fclose(f);
 if(!sauti_decoder_open(&d,registry,1,data,(size_t)len)){
  fprintf(stderr,"FAIL: WavPack stream could not be decoded\n");free(data);return 1;
 }
 float samples[4096*2];
 size_t got=sauti_decoder_read(&d,samples,4096);
 if(!got||!sauti_decoder_seek(&d,0)||sauti_decoder_read(&d,samples,4096)!=got){
  fprintf(stderr,"FAIL: WavPack read/seek\n");sauti_decoder_close(&d);free(data);return 1;
 }
 printf("PASS: WavPack %u Hz, %u ch, %llu frames, %zu frames decoded; seek OK\n",
  d.info.sample_rate,d.info.channels,(unsigned long long)d.info.total_frames,got);
 sauti_decoder_close(&d);free(data);return 0;
}
