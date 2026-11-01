#include "sauti_decoder.h"
#include "sauti_decoder_voc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t bytes[1024]; size_t size; } Fixture;
static void init(Fixture *f) {
    static const uint8_t magic[20] = "Creative Voice File\x1a";
    memset(f, 0, sizeof(*f));
    memcpy(f->bytes, magic, 20);
    f->bytes[20] = 26;
    f->bytes[22] = 0x0a; f->bytes[23] = 0x01;
    f->bytes[24] = 0x29; f->bytes[25] = 0x11;
    f->size = 26;
}
static void block(Fixture *f, unsigned type, const uint8_t *body, size_t n) {
    assert(type && n <= 0xffffffu && f->size + n + 4 < sizeof(f->bytes));
    f->bytes[f->size++] = (uint8_t)type;
    f->bytes[f->size++] = (uint8_t)n;
    f->bytes[f->size++] = (uint8_t)(n >> 8);
    f->bytes[f->size++] = (uint8_t)(n >> 16);
    if (n) { memcpy(f->bytes + f->size, body, n); f->size += n; }
}
static void end(Fixture *f) { f->bytes[f->size++] = 0; }
static int open_voc(SautiDecoder *d, const Fixture *f) {
    const SautiDecoderOps *registry[] = {&sauti_decoder_voc_ops};
    return sauti_decoder_open(d, registry, 1, f->bytes, f->size);
}
static void test_mono_and_seek(void) {
    Fixture f; init(&f);
    const uint8_t sound[] = {131, 0, 0, 128, 255};
    const uint8_t marker[] = {7, 0};
    const uint8_t cont[] = {64, 192};
    const uint8_t silent[] = {1, 0, 131};
    block(&f, 1, sound, sizeof(sound));
    block(&f, 4, marker, sizeof(marker));
    block(&f, 2, cont, sizeof(cont));
    block(&f, 3, silent, sizeof(silent));
    end(&f);
    SautiDecoder d={0};
    assert(open_voc(&d,&f));
    assert(d.info.sample_rate == 8000 && d.info.channels == 1 &&
           d.info.total_frames == 7);
    float v[8]={0};
    assert(sauti_decoder_read(&d,v,7)==7);
    assert(v[0] == -1.0f && v[1] == 0.0f && v[2] == 127.0f/128.0f);
    assert(v[3] == -0.5f && v[4] == 0.5f);
    assert(v[5] == 0.0f && v[6] == 0.0f);
    assert(sauti_decoder_read(&d,v,1)==0);
    assert(sauti_decoder_seek(&d,4));
    assert(sauti_decoder_read(&d,v,3)==3 && v[0]==0.5f && v[1]==0.f);
    assert(!sauti_decoder_seek(&d,8));
    assert(sauti_decoder_seek(&d,7) && !sauti_decoder_read(&d,v,1));
    sauti_decoder_close(&d);
}
static void test_stereo_s16(void) {
    Fixture f; init(&f);
    const uint8_t audio[] = {
        0x44,0xac,0,0, 16,2, 4,0, 0,0,0,0,
        0,0x80, 0xff,0x7f, 0,0, 0,0x40
    };
    block(&f,9,audio,sizeof(audio));end(&f);
    SautiDecoder d={0};float v[4]={0};
    assert(open_voc(&d,&f));
    assert(d.info.sample_rate==44100 && d.info.channels==2 &&
           d.info.total_frames==2);
    assert(sauti_decoder_read(&d,v,2)==2);
    assert(v[0]==-1.f && v[1]>0.999f && v[2]==0.f && v[3]==0.5f);
    assert(sauti_decoder_seek(&d,1));
    assert(sauti_decoder_read(&d,v,1)==1 && v[0]==0.f && v[1]==0.5f);
    sauti_decoder_close(&d);
}
static void test_stereo_extended(void) {
    Fixture f;init(&f);
    const uint8_t ext[]={0x80,0xc1,0,1}; /* 256M/(16000*2) = 8000 Hz */
    const uint8_t sound[]={131,0,0,128,255,64};
    block(&f,8,ext,sizeof(ext));
    block(&f,1,sound,sizeof(sound));end(&f);
    SautiDecoder d={0};float v[4];
    assert(open_voc(&d,&f));
    assert(d.info.channels==2 && d.info.sample_rate==8000 &&
           d.info.total_frames==2);
    assert(sauti_decoder_read(&d,v,2)==2);
    assert(v[0]==-1.f && v[1]==0.f && v[2]>0.99f && v[3]==-0.5f);
    sauti_decoder_close(&d);
}
static void test_rejections(void) {
    Fixture f;SautiDecoder d={0};
    const uint8_t legacy[]={131,0,128};
    init(&f);f.bytes[24]^=1;assert(!open_voc(&d,&f));
    init(&f);f.bytes[20]=0xff;assert(!open_voc(&d,&f));
    init(&f);block(&f,2,legacy,sizeof(legacy));end(&f);
    assert(!open_voc(&d,&f)); /* continuation without prior sound */
    init(&f);block(&f,1,legacy,sizeof(legacy));
    f.bytes[27]=0xff;f.bytes[28]=0xff;f.bytes[29]=0xff;
    assert(!open_voc(&d,&f)); /* truncated block */
    init(&f);const uint8_t adpcm[]={131,1,123};
    block(&f,1,adpcm,sizeof(adpcm));end(&f);
    assert(!open_voc(&d,&f));
    init(&f);const uint8_t loop[]={1,0};
    block(&f,6,loop,sizeof(loop));block(&f,1,legacy,sizeof(legacy));end(&f);
    assert(!open_voc(&d,&f)); /* loops deliberately unsupported */
    init(&f);block(&f,1,legacy,sizeof(legacy));
    const uint8_t changed[]={132,0,128};
    block(&f,1,changed,sizeof(changed));end(&f);
    assert(!open_voc(&d,&f)); /* no implicit resampling */
    init(&f);const uint8_t odd16[]={
        0x44,0xac,0,0,16,2,4,0,0,0,0,0,0,0x80,0
    };
    block(&f,9,odd16,sizeof(odd16));end(&f);
    assert(!open_voc(&d,&f));
    init(&f);const uint8_t silent[]={1,0,131};
    block(&f,3,silent,sizeof(silent));end(&f);
    assert(open_voc(&d,&f));
    float v[2]={1,1};assert(sauti_decoder_read(&d,v,2)==2);
    assert(v[0]==0.f && v[1]==0.f);sauti_decoder_close(&d);
}
int main(void) {
    test_mono_and_seek();
    test_stereo_s16();
    test_stereo_extended();
    test_rejections();
    puts("PASS Creative VOC PCM: blocks 1/2/3/8/9, seek, bounds, fail-closed");
    return 0;
}
