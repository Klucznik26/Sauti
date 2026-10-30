#include "sauti_wav.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned read_le32(const unsigned char *p){
    return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
}
int main(void){
    FILE *fp=tmpfile();assert(fp);
    SautiWav w={0};
    assert(sauti_wav_begin(&w,fp,48000));
    float l[3]={0.f,1.f,-1.f},r[3]={0.5f,-0.5f,0.f};
    assert(sauti_wav_append(&w,l,r,3));
    assert(w.frames==3);
    /* Inspect the actual little-endian bytes, not just the API return. */
    unsigned char bytes[56]={0};
    assert(fseek(fp,0,SEEK_SET)==0);
    assert(fread(bytes,1,sizeof(bytes),fp)==sizeof(bytes));
    assert(memcmp(bytes,"RIFF",4)==0);
    assert(memcmp(bytes+8,"WAVEfmt ",8)==0);
    assert(read_le32(bytes+24)==48000);
    assert(read_le32(bytes+28)==192000);
    assert(memcmp(bytes+36,"data",4)==0);
    assert(bytes[44]==0&&bytes[45]==0); /* silence: left channel */
    assert(bytes[48]==255&&bytes[49]==127); /* clipped positive sample */
    assert(bytes[52]==0&&bytes[53]==128); /* clipped negative sample */
    assert(sauti_wav_finish(&w));
    assert(!w.file&&!w.failed);
    fp=tmpfile();assert(fp);
    assert(!sauti_wav_begin(&w,fp,0));
    fclose(fp);
    puts("Sauti portable WAV writer tests passed");
    return 0;
}
