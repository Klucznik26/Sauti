/* AAC (ADTS/MP4), ALAC (MP4/M4A) and WebM (Opus/Vorbis).
 * Optional libavformat/libavcodec/libswresample adapter; no ffmpeg subprocess.
 * The Sauti core/ABI is unchanged. Initial bounded in-memory decode trades
 * higher memory usage at load time for sample-exact, instant PCM seeking.
 * TODO: replace the buffered PCM with a streaming ring for long recordings. */
#include "sauti_decoder_ffmpeg.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

enum { MODE_AAC, MODE_ALAC, MODE_WEBM };
static int is_mp4(const uint8_t *p,size_t n){
    return p && n>=12 && memcmp(p+4,"ftyp",4)==0;
}
static int aac_probe(const uint8_t *p,size_t n){
    return is_mp4(p,n) || (p && n>=7 && p[0]==0xff &&
           (p[1]&0xf6u)==0xf0u);
}
static int alac_probe(const uint8_t *p,size_t n){return is_mp4(p,n);}
static int webm_probe(const uint8_t *p,size_t n){
    static const uint8_t ebml[]={0x1a,0x45,0xdf,0xa3};
    return p && n>=4 && memcmp(p,ebml,4)==0;
}
#ifdef SAUTI_HAVE_FFMPEG
#include <errno.h>
#include <limits.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

/* At most 512 MiB of decoded float PCM per file (for now). */
#define MAX_PCM_FLOATS ((size_t)128u*1024u*1024u)
typedef struct {const uint8_t *p;size_t n,pos;} ByteView;
typedef struct {
    float *pcm;
    size_t capacity,frames,position;
    uint32_t rate,channels;
} FfmpegState;
static int io_read(void *ctx,uint8_t *out,int requested){
    ByteView *b=ctx;
    if (requested<=0) return AVERROR(EINVAL);
    if (b->pos>=b->n) return AVERROR_EOF;
    size_t size=(size_t)requested;
    if (size>b->n-b->pos) size=b->n-b->pos;
    memcpy(out,b->p+b->pos,size);
    b->pos+=size;
    return (int)size;
}
static int64_t io_seek(void *ctx,int64_t off,int whence){
    ByteView *b=ctx;
    if (b->n > INT64_MAX) return AVERROR(EINVAL);
    if (whence==AVSEEK_SIZE) return (int64_t)b->n;
    whence &= ~AVSEEK_FORCE;
    int64_t base=0;
    if (whence==SEEK_SET) base=0;
    else if (whence==SEEK_CUR) base=(int64_t)b->pos;
    else if (whence==SEEK_END) base=(int64_t)b->n;
    else return AVERROR(EINVAL);
    if ((off>0 && base>INT64_MAX-off) ||
        (off<0 && base<INT64_MIN-off)) return AVERROR(EINVAL);
    int64_t dest=base+off;
    if(dest<0 || dest>(int64_t)b->n) return AVERROR(EINVAL);
    b->pos=(size_t)dest;
    return dest;
}
static int append_samples(FfmpegState *s,const float *pcm,size_t frames){
    if (!frames) return 1;
    size_t limit=MAX_PCM_FLOATS/s->channels;
    if (frames>limit-s->frames) return 0;
    size_t required=(s->frames+frames)*s->channels;
    if (required>s->capacity) {
        size_t target=s->capacity ? s->capacity : 16384u;
        while (target<required) {
            if (target>MAX_PCM_FLOATS/2u) {target=MAX_PCM_FLOATS;break;}
            target*=2u;
        }
        if (target<required) return 0;
        float *p=realloc(s->pcm,target*sizeof(float));
        if(!p)return 0;
        s->pcm=p;s->capacity=target;
    }
    memcpy(s->pcm+s->frames*s->channels,pcm,frames*s->channels*sizeof(float));
    s->frames+=frames;
    return 1;
}
static int convert_frame(FfmpegState *s,SwrContext *swr,AVFrame *frame){
    int cap=swr_get_out_samples(swr,frame->nb_samples);
    if (cap<0 || (size_t)cap>MAX_PCM_FLOATS/s->channels) return 0;
    if (!cap) return 1;
    size_t needed=(size_t)cap*s->channels;
    float *scratch=malloc(needed*sizeof(float));
    if (!scratch) return 0;
    uint8_t *out=(uint8_t *)scratch;
    int count=swr_convert(swr,&out,cap,
                          (const uint8_t **)frame->extended_data,
                          frame->nb_samples);
    int ok=count>=0 && append_samples(s,scratch,(size_t)(count<0?0:count));
    free(scratch);
    return ok;
}
static int decode_available(FfmpegState *s,AVCodecContext *codec,
                            SwrContext *swr,AVFrame *frame){
    for(;;){
        int got=avcodec_receive_frame(codec,frame);
        if(got==AVERROR(EAGAIN) || got==AVERROR_EOF)return 1;
        if(got<0)return 0;
        int ok=convert_frame(s,swr,frame);
        av_frame_unref(frame);
        if(!ok)return 0;
    }
}
static int is_expected(enum AVCodecID codec,int mode){
    if (mode==MODE_AAC) return codec==AV_CODEC_ID_AAC;
    if (mode==MODE_ALAC) return codec==AV_CODEC_ID_ALAC;
    return codec==AV_CODEC_ID_OPUS || codec==AV_CODEC_ID_VORBIS;
}
static void *open_codec(const uint8_t *p,size_t n,SautiPcmInfo *info,int mode){
    if (!info || !p || !n || n>INT64_MAX) return NULL;
    if ((mode==MODE_AAC && !aac_probe(p,n)) ||
        (mode==MODE_ALAC && !alac_probe(p,n)) ||
        (mode==MODE_WEBM && !webm_probe(p,n)))return NULL;
    ByteView view={p,n,0};
    AVIOContext *io=NULL;
    AVFormatContext *fmt=NULL;
    AVCodecContext *codec=NULL;
    AVFrame *frame=NULL;
    AVPacket *packet=NULL;
    SwrContext *swr=NULL;
    FfmpegState *s=NULL;
    int ok=0;
    uint8_t *io_buffer=av_malloc(32768u);
    if (!io_buffer)goto cleanup;
    io=avio_alloc_context(io_buffer,32768,0,&view,io_read,NULL,io_seek);
    if (!io){av_free(io_buffer);goto cleanup;}
    fmt=avformat_alloc_context();
    if(!fmt)goto cleanup;
    fmt->pb=io;fmt->flags|=AVFMT_FLAG_CUSTOM_IO;
    if(avformat_open_input(&fmt,NULL,NULL,NULL)<0 ||
       avformat_find_stream_info(fmt,NULL)<0)goto cleanup;
    int idx=av_find_best_stream(fmt,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    if(idx<0)goto cleanup;
    AVCodecParameters *params=fmt->streams[idx]->codecpar;
    if(!is_expected(params->codec_id,mode))goto cleanup;
    const AVCodec *impl=avcodec_find_decoder(params->codec_id);
    if(!impl)goto cleanup;
    codec=avcodec_alloc_context3(impl);
    if(!codec || avcodec_parameters_to_context(codec,params)<0 ||
       avcodec_open2(codec,impl,NULL)<0)goto cleanup;
    if(codec->sample_rate<1000 || codec->sample_rate>384000 ||
       codec->ch_layout.nb_channels<1 || codec->ch_layout.nb_channels>32)
        goto cleanup;
    uint32_t channels=codec->ch_layout.nb_channels==1 ? 1u : 2u;
    AVChannelLayout dst={0},src={0};
    av_channel_layout_default(&dst,(int)channels);
    if(codec->ch_layout.order==AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&src,codec->ch_layout.nb_channels);
    else if(av_channel_layout_copy(&src,&codec->ch_layout)<0){
        av_channel_layout_uninit(&dst);goto cleanup;
    }
    int configured=swr_alloc_set_opts2(&swr,&dst,AV_SAMPLE_FMT_FLT,
        codec->sample_rate,&src,codec->sample_fmt,codec->sample_rate,0,NULL);
    av_channel_layout_uninit(&dst);av_channel_layout_uninit(&src);
    if(configured<0 || !swr || swr_init(swr)<0)goto cleanup;
    s=calloc(1,sizeof(*s));packet=av_packet_alloc();frame=av_frame_alloc();
    if(!s||!packet||!frame)goto cleanup;
    s->rate=(uint32_t)codec->sample_rate;s->channels=channels;
    int read_result;
    while((read_result=av_read_frame(fmt,packet))>=0){
        if(packet->stream_index!=(int)idx){av_packet_unref(packet);continue;}
        int sent=avcodec_send_packet(codec,packet);
        av_packet_unref(packet);
        if(sent<0 || !decode_available(s,codec,swr,frame))goto cleanup;
    }
    if(read_result!=AVERROR_EOF || avcodec_send_packet(codec,NULL)<0 ||
       !decode_available(s,codec,swr,frame))goto cleanup;
    /* Complete delayed swr frames. */
    for(;;){
        int cap=swr_get_out_samples(swr,0);
        if(cap<=0)break;
        if((size_t)cap>MAX_PCM_FLOATS/channels)goto cleanup;
        float *scratch=malloc((size_t)cap*channels*sizeof(float));
        if(!scratch)goto cleanup;
        uint8_t *out=(uint8_t *)scratch;
        int got=swr_convert(swr,&out,cap,NULL,0);
        int good=got>=0 && append_samples(s,scratch,(size_t)(got<0?0:got));
        free(scratch);
        if(!good)goto cleanup;
        if(!got)break;
    }
    if(!s->frames)goto cleanup;
    info->channels=s->channels;info->sample_rate=s->rate;
    info->total_frames=s->frames;ok=1;
cleanup:
    av_packet_free(&packet);av_frame_free(&frame);
    swr_free(&swr);avcodec_free_context(&codec);
    avformat_close_input(&fmt);avio_context_free(&io);
    if(!ok){if(s){free(s->pcm);free(s);}return NULL;}
    return s;
}
static size_t ff_read(void *ctx,float *out,size_t frames){
    FfmpegState *s=ctx;
    if(!s||!out||!frames||s->position>=s->frames)return 0;
    if(frames>s->frames-s->position)frames=s->frames-s->position;
    memcpy(out,s->pcm+s->position*s->channels,
           frames*s->channels*sizeof(float));
    s->position+=frames;return frames;
}
static int ff_seek(void *ctx,uint64_t frame){
    FfmpegState *s=ctx;
    if(!s||frame>s->frames)return 0;
    s->position=(size_t)frame;return 1;
}
static void ff_close(void *ctx){
    FfmpegState *s=ctx;
    if(s){free(s->pcm);free(s);}
}
#else
static void *open_codec(const uint8_t *p,size_t n,SautiPcmInfo *info,int mode){
    (void)p;(void)n;(void)info;(void)mode;return NULL;
}
static size_t ff_read(void *s,float *f,size_t n){
    (void)s;(void)f;(void)n;return 0;
}
static int ff_seek(void *s,uint64_t n){(void)s;(void)n;return 0;}
static void ff_close(void *s){(void)s;}
#endif
static void *aac_open(const uint8_t *p,size_t n,SautiPcmInfo *info){
    return open_codec(p,n,info,MODE_AAC);
}
static void *alac_open(const uint8_t *p,size_t n,SautiPcmInfo *info){
    return open_codec(p,n,info,MODE_ALAC);
}
static void *webm_open(const uint8_t *p,size_t n,SautiPcmInfo *info){
    return open_codec(p,n,info,MODE_WEBM);
}
const SautiDecoderOps sauti_decoder_aac_ops={
    "aac-libavcodec",aac_probe,aac_open,ff_read,ff_seek,ff_close
};
const SautiDecoderOps sauti_decoder_alac_ops={
    "alac-libavcodec",alac_probe,alac_open,ff_read,ff_seek,ff_close
};
const SautiDecoderOps sauti_decoder_webm_ops={
    "webm-libavcodec",webm_probe,webm_open,ff_read,ff_seek,ff_close
};
