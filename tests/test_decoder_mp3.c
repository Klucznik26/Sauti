#include "sauti_decoder.h"
#include "sauti_decoder_mp3.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Regresja: minimp3 (mp3dec_detect_cb) uzywal niezainicjowanego free_format_bytes; wartosc ze stosu, ujemna i duza, dawala odczyt przed poczatkiem bufora
 * (SEGV w hdr_valid, wykryty przez TSan po tym, jak nowy rdzen zaczal sondowac WSZYSTKIE kodeki na kazdym pliku).  Test zasmieca stos i sonduje bufor o dokladnym
 * rozmiarze zawierajacy naglowek "free format" (bitrate 0). */
#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void poison_stack(unsigned char value){
    volatile unsigned char scratch[32768];
    memset((void *)scratch,value,sizeof(scratch));
}

static void probe_with_poisoned_stack(const SautiDecoderOps *ops){
    static const unsigned char values[]={0x80,0xFF,0xC0,0x7F,0x00};
    for(size_t v=0;v<sizeof(values);v++){
        const size_t sizes[]={64,1000,2000,5000};
        for(size_t k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++){
            uint8_t *buf=calloc(1,sizes[k]);
            assert(buf);
            buf[0]=0xFF;buf[1]=0xFB;buf[2]=0x00;buf[3]=0x00;  /* MPEG-1 layer III, free format */
            poison_stack(values[v]);
            (void)ops->probe(buf,sizes[k]);                  /* wynik nieistotny: ma nie czytac poza buforem */
            free(buf);
        }
    }
}

int main(int argc,char **argv){
    const SautiDecoderOps *reg[]={&sauti_decoder_mp3_ops};
    const uint8_t bad[]={'R','I','F','F',0,0,0,0};
    SautiDecoder dec={0};
    float samples[4096]={0};
    assert(sauti_legacy_decoder_ops_valid(reg[0]));
    assert(!reg[0]->probe(bad,sizeof(bad)));
    probe_with_poisoned_stack(reg[0]);
    assert(!sauti_legacy_decoder_open(&dec,reg,1,bad,sizeof(bad)));
    assert(sauti_legacy_decoder_read(&dec,samples,4)==0);
    sauti_legacy_decoder_close(&dec);

    /* Optional real-file integration test; file I/O is test-only. */
    if(argc<2)return 0;
    FILE *f=fopen(argv[1],"rb");
    if(!f){perror("MP3 test fopen");return 1;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return 1;}
    long size=ftell(f);
    if(size<=0 || fseek(f,0,SEEK_SET)!=0){fclose(f);return 1;}
    uint8_t *bytes=malloc((size_t)size);
    if(!bytes){fclose(f);return 1;}
    size_t n=fread(bytes,1,(size_t)size,f);
    fclose(f);
    if(n!=(size_t)size){free(bytes);return 1;}
    int success=sauti_legacy_decoder_open(&dec,reg,1,bytes,n);
    if(success){
        printf("MP3: %u Hz, %u channels, %llu frames\n",
               dec.info.sample_rate,dec.info.channels,
               (unsigned long long)dec.info.total_frames);
        size_t frames=sauti_legacy_decoder_read(&dec,samples,4096/dec.info.channels);
        printf("MP3: decoded %zu frames\n",frames);
        success=frames>0 && sauti_legacy_decoder_seek(&dec,0) &&
                sauti_legacy_decoder_read(&dec,samples,4096/dec.info.channels)>0;
        sauti_legacy_decoder_close(&dec);
    }
    free(bytes);
    return success?0:1;
}
