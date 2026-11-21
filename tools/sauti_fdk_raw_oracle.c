/* Test oracle (tool, not a product): the reference decoder Fraunhofer FDK AAC (libfdk-aac) on raw access units with an AudioSpecificConfig, 32-BIT PCM -> float32 interleaved.
 * The stock library outputs 16-bit PCM (~90 dB of dynamic range); for comparisons at float precision build it from source with SAMPLE_BITS 32:
 *   in libSYS/include/machine_type.h replace the INT_PCM block by `typedef LONG INT_PCM; #define MAXVAL_PCM MAXVAL_DBL; #define MINVAL_PCM MINVAL_DBL; #define WAV_BITS 32; #define SAMPLE_BITS 32`,
 *   cmake -S . -B build -DBUILD_SHARED_LIBS=ON -DBUILD_PROGRAMS=OFF && cmake --build build
 * build the tool against THAT tree: cc -O2 -I$FDK/libAACdec/include -I$FDK/libSYS/include -o build/sauti_fdk_raw_oracle tools/sauti_fdk_raw_oracle.c -L$FDK/build -lfdk-aac -Wl,-rpath,$FDK/build
 * usage: sauti_fdk_raw_oracle ASC.bin AUS.bin OUT.f32   (inputs from tools/mp4_dump_aus.py)
 *        sauti_fdk_raw_oracle --loas IN.loas OUT.f32     (a LOAS/LATM stream decoded by the library's own transport layer)\n *        sauti_fdk_raw_oracle --adts IN.aac OUT.f32      (an ADTS stream; prints the number of frames and of CRC errors the library found). */
#include "aacdecoder_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char *slurp(const char *p,long *n){FILE *f=fopen(p,"rb");unsigned char *b;if(!f)return NULL;fseek(f,0,SEEK_END);*n=ftell(f);fseek(f,0,SEEK_SET);b=malloc((size_t)*n+1);if(fread(b,1,(size_t)*n,f)!=(size_t)*n)return NULL;fclose(f);return b;}
int main(int argc,char **argv){
    long asc_n,aus_n,pos=0,frames=0,errs=0;unsigned char *asc,*aus;HANDLE_AACDECODER h;static INT_PCM pcm[8*2048];FILE *o;
    if(argc==4&&!strcmp(argv[1],"--adts")){                                 /* an ADTS stream through the library's transport layer; counts the CRC errors it reports (the library checks the ADTS error check) */
        long n=0,pos=0,frames=0,crc_errors=0,other=0;unsigned char *b=slurp(argv[2],&n);HANDLE_AACDECODER h=aacDecoder_Open(TT_MP4_ADTS,1);FILE *o=fopen(argv[3],"wb");static INT_PCM pcm[8*2048];static float f[8*2048];
        if(!b||!o)return 2;
        (void)aacDecoder_SetParam(h,AAC_PCM_LIMITER_ENABLE,0);
        while(pos<n){
            UINT valid,chunk=(UINT)(n-pos<4096?n-pos:4096);UCHAR *buf[1];UINT sz[1];AAC_DECODER_ERROR e;
            buf[0]=b+pos;sz[0]=chunk;valid=chunk;e=aacDecoder_Fill(h,buf,sz,&valid);pos+=chunk-valid;
            for(;;){
                e=aacDecoder_DecodeFrame(h,pcm,(INT)(sizeof pcm/sizeof pcm[0]),0);
                if(e==AAC_DEC_OK){CStreamInfo *si=aacDecoder_GetStreamInfo(h);size_t k,m=(size_t)si->frameSize*(size_t)si->numChannels;for(k=0;k<m;k++)f[k]=(float)((double)pcm[k]/2147483648.0);fwrite(f,sizeof(float),m,o);frames++;}
                else if(e==AAC_DEC_NOT_ENOUGH_BITS)break;
                else if(e==AAC_DEC_CRC_ERROR)crc_errors++;
                else if(e!=AAC_DEC_TRANSPORT_SYNC_ERROR){other++;break;}
            }
            if(valid==chunk&&chunk==0)break;
        }
        fclose(o);fprintf(stderr,"frames %ld crc_errors %ld other_errors %ld\n",frames,crc_errors,other);return 0;
    }
    if(argc==4&&!strcmp(argv[1],"--loas")){
        long n=0,pos=0,frames=0;unsigned char *b=slurp(argv[2],&n);HANDLE_AACDECODER h=aacDecoder_Open(TT_MP4_LOAS,1);FILE *o=fopen(argv[3],"wb");(void)aacDecoder_SetParam(h,AAC_PCM_LIMITER_ENABLE,0);static INT_PCM pcm[8*2048];static float f[8*2048];
        if(!b||!o)return 2;
        while(pos<n){
            UINT valid,chunk=(UINT)(n-pos<(1<<20)?n-pos:(1<<20))  /* the whole file at once: with 4096-byte pieces FDK loses a LOAS element that straddles a piece boundary (usac_c1.loas: one frame of 24) */;UCHAR *buf[1];UINT sz[1];AAC_DECODER_ERROR e;
            buf[0]=b+pos;sz[0]=chunk;valid=chunk;e=aacDecoder_Fill(h,buf,sz,&valid);pos+=chunk-valid;
            for(;;){
                e=aacDecoder_DecodeFrame(h,pcm,(INT)(sizeof pcm/sizeof pcm[0]),0);
                if(e==AAC_DEC_OK){CStreamInfo *si=aacDecoder_GetStreamInfo(h);size_t k,m=(size_t)si->frameSize*(size_t)si->numChannels;for(k=0;k<m;k++)f[k]=(float)((double)pcm[k]/2147483648.0);fwrite(f,sizeof(float),m,o);frames++;}
                else if(e==AAC_DEC_NOT_ENOUGH_BITS)break;
                else if(e!=AAC_DEC_TRANSPORT_SYNC_ERROR&&e!=AAC_DEC_CRC_ERROR)break;
            }
            if(valid==chunk&&chunk==0)break;
        }
        fclose(o);fprintf(stderr,"frames %ld\n",frames);return 0;
    }
    if(argc!=4)return 2;
    if(sizeof(INT_PCM)!=4){fprintf(stderr,"this tool needs a 32-bit PCM build of libfdk-aac (INT_PCM is %zu bytes)\n",sizeof(INT_PCM));return 2;}
    asc=slurp(argv[1],&asc_n);aus=slurp(argv[2],&aus_n);if(!asc||!aus)return 2;
    h=aacDecoder_Open(TT_MP4_RAW,1);(void)aacDecoder_SetParam(h,AAC_PCM_LIMITER_ENABLE,0);                /* no 15 ms limiter delay, no gain reduction: a faithful decode for comparison */
    { UCHAR *conf[1]={asc};UINT len[1]={(UINT)asc_n};if(aacDecoder_ConfigRaw(h,conf,len)!=AAC_DEC_OK){fprintf(stderr,"ConfigRaw failed\n");return 3;} }
    o=fopen(argv[3],"wb");
    while(pos+4<=aus_n){
        UINT len=((UINT)aus[pos]<<24)|((UINT)aus[pos+1]<<16)|((UINT)aus[pos+2]<<8)|aus[pos+3],valid;UCHAR *buf[1];UINT sz[1];AAC_DECODER_ERROR e;
        pos+=4;if(pos+(long)len>aus_n)break;
        buf[0]=aus+pos;sz[0]=len;valid=len;
        e=aacDecoder_Fill(h,buf,sz,&valid);
        if(e==AAC_DEC_OK)e=aacDecoder_DecodeFrame(h,pcm,(INT)(sizeof pcm/sizeof pcm[0]),0);
        if(e==AAC_DEC_OK){
            CStreamInfo *si=aacDecoder_GetStreamInfo(h);size_t n=(size_t)si->frameSize*(size_t)si->numChannels,i;static float f[8*2048];
            for(i=0;i<n;i++)f[i]=(float)((double)pcm[i]/2147483648.0);
            fwrite(f,sizeof(float),n,o);
            if(!frames)fprintf(stderr,"rate %d channels %d frame %d aot %d extAot %d profile %d\n",si->sampleRate,si->numChannels,si->frameSize,(int)si->aot,(int)si->extAot,si->profile);
            frames++;
        }else{errs++;if(errs<4)fprintf(stderr,"frame %ld: error 0x%x\n",frames+errs,(unsigned)e);}
        pos+=(long)len;
    }
    fclose(o);fprintf(stderr,"frames %ld errors %ld\n",frames,errs);aacDecoder_Close(h);return errs?1:0;}
