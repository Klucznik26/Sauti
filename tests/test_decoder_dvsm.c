#include "sauti_decoder.h"
#include "sauti_decoder_dvsm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void be16(unsigned char *p,unsigned n){p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n;}
int main(void){
    unsigned char b[40]={0};
    memcpy(b,"DVSM",4);be16(b+6,16);be16(b+8,7);b[10]=0;b[11]=1;
    b[16]=0x80;b[17]=0;b[18]=0x7f;b[19]=0xff;
    const SautiDecoderOps *ops[]={&sauti_decoder_dvsm_ops};
    SautiDecoder d={0};float pcm[4]={0};
    assert(sauti_decoder_open(&d,ops,1,b,20));
    assert(d.info.channels==2 && d.info.sample_rate==49170 && d.info.total_frames==1);
    assert(sauti_decoder_read(&d,pcm,2)==1);
    assert(pcm[0]==-1.0f && pcm[1]==32767.0f/32768.0f);
    assert(sauti_decoder_seek(&d,0));sauti_decoder_close(&d);
    b[11]=3;b[16]=0x80;b[17]=0x00;
    assert(sauti_decoder_open(&d,ops,1,b,18));
    assert(d.info.channels==1 && d.info.total_frames==1);
    assert(sauti_decoder_read(&d,pcm,1)==1&&pcm[0]==-1.0f);
    sauti_decoder_close(&d);
    b[11]=2;b[16]=0x80;b[17]=0x7f;
    assert(sauti_decoder_open(&d,ops,1,b,18));
    assert(sauti_decoder_read(&d,pcm,1)==1&&pcm[0]==-1.0f);
    sauti_decoder_close(&d);
    b[10]=2;assert(!sauti_decoder_open(&d,ops,1,b,18));
    b[10]=0;b[0]='X';assert(!sauti_decoder_open(&d,ops,1,b,18));
    b[0]='D';be16(b+6,40);assert(!sauti_decoder_open(&d,ops,1,b,18));
    /* Pack-2 mono: big-endian seed 0, +1, +1, -1, -32768, +32767.
     * Delta 1 = +1 (round(1.0846)); delta 255 = -1, delta 0 = 0.
     */
    memset(b,0,sizeof(b));memcpy(b,"DVSM",4);
    be16(b+6,16);be16(b+8,7);b[10]=2;b[11]=3;
    b[16]=0;b[17]=0;
    b[18]=1;b[19]=1;b[20]=255;b[21]=128;b[22]=0;
    assert(sauti_decoder_open(&d,ops,1,b,23));
    assert(d.info.channels==1 && d.info.total_frames==6);
    assert(sauti_decoder_read(&d,pcm,3)==3);
    assert(pcm[0]==0 && pcm[1]==1.0f/32768.0f && pcm[2]==2.0f/32768.0f);
    assert(sauti_decoder_read(&d,pcm,3)==3);
    assert(pcm[0]==1.0f/32768.0f && pcm[1]==-32767.0f/32768.0f && pcm[2]==-32767.0f/32768.0f);
    assert(sauti_decoder_seek(&d,3));
    assert(sauti_decoder_read(&d,pcm,1)==1 && pcm[0]==1.0f/32768.0f);
    sauti_decoder_close(&d);
    /* Two stereo blocks: each 4-byte seeds and two LR delta pairs. */
    memset(b,0,sizeof(b));memcpy(b,"DVSM",4);
    be16(b+6,16);be16(b+8,7);b[10]=2;b[11]=1;
    b[15]=8; /* big-endian block length */
    b[16]=0;b[17]=0;b[18]=0;b[19]=100;
    b[20]=1;b[21]=255;b[22]=1;b[23]=255;
    b[24]=0x80;b[25]=0;b[26]=0x7f;b[27]=0xff;
    b[28]=128;b[29]=127;b[30]=1;b[31]=255;
    assert(sauti_decoder_open(&d,ops,1,b,32));
    assert(d.info.channels==2 && d.info.total_frames==6);
    float stereo[12]={0};
    assert(sauti_decoder_read(&d,stereo,6)==6);
    assert(stereo[0]==0 && stereo[1]==100.0f/32768.0f);
    assert(stereo[2]==1.0f/32768.0f && stereo[3]==99.0f/32768.0f);
    assert(stereo[4]==2.0f/32768.0f && stereo[5]==98.0f/32768.0f);
    assert(stereo[6]==-1.0f && stereo[7]==32767.0f/32768.0f);
    assert(stereo[8]==-1.0f && stereo[9]==32767.0f/32768.0f);
    assert(stereo[10]==-32767.0f/32768.0f && stereo[11]==32766.0f/32768.0f);
    assert(sauti_decoder_seek(&d,3));
    assert(sauti_decoder_read(&d,stereo,1)==1 && stereo[0]==-1.0f);
    sauti_decoder_close(&d);
    assert(!sauti_decoder_open(&d,ops,1,b,31));
    b[15]=3;assert(!sauti_decoder_open(&d,ops,1,b,32));
    b[15]=8;assert(!sauti_decoder_open(&d,ops,1,b,19));

    b[11]=3;b[10]=5;assert(!sauti_decoder_open(&d,ops,1,b,23));
    /* Voice pack 4 mono: seed=0, high nibble 8 -> +64, low 6 -> -64. */
    memset(b,0,sizeof(b));memcpy(b,"DVSM",4);
    be16(b+6,16);be16(b+8,7);b[10]=4;b[11]=3;
    b[16]=0;b[17]=0;b[18]=0x86;
    assert(sauti_decoder_open(&d,ops,1,b,19));
    assert(d.info.channels==1 && d.info.total_frames==3);
    assert(sauti_decoder_read(&d,pcm,3)==3);
    assert(pcm[0]==0 && pcm[1]==64.0f/32768.0f && pcm[2]==0);
    assert(sauti_decoder_seek(&d,1));
    assert(sauti_decoder_read(&d,pcm,1)==1 && pcm[0]==64.0f/32768.0f);
    sauti_decoder_close(&d);
    /* Voice stereo: one block, 2 seeds, high nibble L and low R. */
    memset(b,0,sizeof(b));memcpy(b,"DVSM",4);
    be16(b+6,16);be16(b+8,7);b[10]=4;b[11]=1;b[15]=6;
    b[16]=0;b[17]=0;b[18]=0;b[19]=0;b[20]=0x8a;b[21]=0x67;
    assert(sauti_decoder_open(&d,ops,1,b,22));
    assert(d.info.channels==2 && d.info.total_frames==3);
    float voice[6]={0};
    assert(sauti_decoder_read(&d,voice,3)==3);
    assert(voice[0]==0 && voice[1]==0);
    assert(voice[2]==64.0f/32768.0f && voice[3]==512.0f/32768.0f);
    assert(voice[4]==0 && voice[5]==512.0f/32768.0f);
    sauti_decoder_close(&d);
    b[15]=3;assert(!sauti_decoder_open(&d,ops,1,b,22));
    b[15]=6;assert(!sauti_decoder_open(&d,ops,1,b,19));
    b[10]=5;assert(!sauti_decoder_open(&d,ops,1,b,22));
    /* Real-world prefix: digital.dvs from Sembiance DVSM sample archive.
     * Only the 16 header bytes + first 16 compressed bytes are used;
     * expected PCM computed separately from signed-step definition.
     * This tests actual file bytes, not an invented delta sequence.
     */
    static const unsigned char digital_prefix[16]={
        0x02,0x72,0xc4,0xbf,0x3c,0x2f,0xea,0x42,
        0x38,0xc0,0xcf,0x45,0x3f,0x39,0x48,0x3f
    };
    static const int expected[15]={
        626,495,299,430,476,470,683,778,597,543,815,982,1085,1432,1599
    };
    memset(b,0,sizeof(b));memcpy(b,"DVSM",4);
    be16(b+6,16);be16(b+8,5);b[10]=2;b[11]=3;
    b[12]=0;b[13]=0;b[14]=0x7d;b[15]=0x01;
    memcpy(b+16,digital_prefix,sizeof(digital_prefix));
    assert(sauti_decoder_open(&d,ops,1,b,32));
    assert(d.info.sample_rate==24585 && d.info.channels==1);
    assert(d.info.total_frames==15);
    float real_pcm[15]={0};
    assert(sauti_decoder_read(&d,real_pcm,15)==15);
    for(size_t i=0;i<15;i++){
        assert(real_pcm[i]==(float)expected[i]/32768.0f);
    }
    assert(sauti_decoder_seek(&d,13));
    assert(sauti_decoder_read(&d,real_pcm,2)==2);
    assert(real_pcm[0]==1432.0f/32768.0f);
    assert(real_pcm[1]==1599.0f/32768.0f);
    sauti_decoder_close(&d);
    /* Falcon 030 49.17kHz 16-bit stereo PCM: multiple interleaved
     * big-endian samples, cross-buffer reads, exact seek and EOF. */
    {
        unsigned char recording[16+4*5]={0};
        memcpy(recording,"DVSM",4);
        be16(recording+6,16);
        be16(recording+8,7); /* Falcon rate selector: 49170 Hz */
        recording[10]=0;recording[11]=1; /* unpacked 16-bit stereo */
        static const unsigned char samples[20]={
            0x80,0x00, 0x7f,0xff,
            0x40,0x00, 0xc0,0x00,
            0x00,0x00, 0x00,0x00,
            0xff,0xff, 0x00,0x01,
            0x7f,0xff, 0x80,0x00
        };
        memcpy(recording+16,samples,sizeof(samples));
        static const int16_t expected[10]={
            -32768,32767,16384,-16384,0,0,-1,1,32767,-32768
        };
        float whole[10]={0},pieces[10]={0};
        assert(sauti_decoder_open(&d,ops,1,recording,sizeof(recording)));
        assert(d.info.channels==2 && d.info.sample_rate==49170);
        assert(d.info.total_frames==5);
        assert(sauti_decoder_read(&d,whole,5)==5);
        for(unsigned i=0;i<10;i++)
            assert(whole[i]==(float)expected[i]/32768.0f);
        assert(sauti_decoder_read(&d,pieces,1)==0);
        assert(sauti_decoder_seek(&d,0));
        assert(sauti_decoder_read(&d,pieces,2)==2);
        assert(sauti_decoder_read(&d,pieces+4,1)==1);
        assert(sauti_decoder_read(&d,pieces+6,2)==2);
        assert(memcmp(whole,pieces,sizeof(whole))==0);
        assert(sauti_decoder_seek(&d,3));
        assert(sauti_decoder_read(&d,pieces,2)==2);
        assert(memcmp(pieces,whole+6,4*sizeof(float))==0);
        assert(sauti_decoder_seek(&d,5));
        assert(sauti_decoder_read(&d,pieces,1)==0);
        assert(!sauti_decoder_seek(&d,6));
        sauti_decoder_close(&d);
        /* No partial sample or partial stereo frame is legal. */
        assert(!sauti_decoder_open(&d,ops,1,recording,sizeof(recording)-1));
    }
    puts("PASS: Atari Falcon DVSM PCM, Delta/Voice, real digital.dvs prefix, malformed headers");
    return 0;
}
