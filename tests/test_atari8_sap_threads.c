/* Independent concurrent SAP instances; same instance requires external
 * synchronization. Compare each thread's PCM against a serial baseline.
 * Compile with -pthread -fsanitize=thread. No external samples needed. */
#include "../src/decoders/retro/atari8/atari8_sap_stream.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); abort(); } } while (0)
enum { TYPES=5, THREADS=10, FRAMES=512, ROUNDS=20 };
typedef struct {
    uint8_t data[512];
    size_t size;
    uint32_t rate;
    bool restart;
    int16_t expected[FRAMES][2];
} Fixture;
typedef struct { const Fixture *fixtures; unsigned index; } Worker;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond=PTHREAD_COND_INITIALIZER;
static unsigned ready;
static bool go;

static void fixture(Fixture *f,unsigned type,bool stereo)
{
    static const char *const headers[]={
        "SAP\r\nTYPE B\r\nINIT 2000\r\nPLAYER 2006\r\nTIME 00:00.040\r\n",
        "SAP\r\nTYPE C\r\nMUSIC 2000\r\nPLAYER 2000\r\nTIME 00:00.040\r\n",
        "SAP\r\nTYPE D\r\nINIT 2000\r\nTIME 00:00.040\r\n",
        "SAP\r\nTYPE R\r\nFASTPLAY 312\r\n",
        "SAP\r\nTYPE S\r\nINIT 2000\r\nTIME 00:00.040\r\n"
    };
    const char *h=headers[type];
    size_t n=strlen(h);
    memcpy(f->data,h,n);
    if(stereo) { memcpy(f->data+n,"STEREO\r\n",8); n+=8; }
    static const uint8_t calls[]={255,255,0,32,16,32,
        0x60,0xea,0xea,0x60,0xea,0xea,
        0xa9,0x71,0x8d,0,0xd2,0xa9,0xaf,0x8d,1,0xd2,0x60};
    static const uint8_t loop[]={255,255,0,32,12,32,
        0xa9,0x71,0x8d,0,0xd2,0xa9,0xaf,0x8d,1,0xd2,0x4c,10,32};
    if(type==3) {
        f->data[n++]='\r'; f->data[n++]='\n';
        size_t stride=stereo?18u:9u;
        for(unsigned i=0;i<4;++i) {
            memset(f->data+n,0,stride);
            f->data[n]=31u+i; f->data[n+1]=0xaf;
            if(stereo) { f->data[n+9]=71; f->data[n+10]=0xa7; }
            n+=stride;
        }
    } else {
        const uint8_t *p=type<2?calls:loop;
        size_t size=type<2?sizeof(calls):sizeof(loop);
        memcpy(f->data+n,p,size); n+=size;
    }
    f->size=n;
    f->rate=stereo?48000u:44100u;
    f->restart=type==0 && stereo;
}

static void exercise(const Fixture *f,int16_t out[FRAMES][2])
{
    Atari8SapStream *s=NULL;
    CHECK(atari8_sap_stream_open(&s,f->data,f->size,0,f->rate,true,f->restart)==ATARI8_SAPB_OK);
    size_t pos=0;
    while(pos<FRAMES) {
        size_t n=FRAMES-pos<37u?FRAMES-pos:37u,got=0;
        CHECK(atari8_sap_stream_read(s,out+pos,n,&got)==ATARI8_SAPB_OK && got==n);
        pos+=got;
    }
    CHECK(atari8_sap_stream_tell(s)==FRAMES);
    uint64_t len;
    CHECK(atari8_sap_stream_length(s,&len) && len>FRAMES);
    CHECK(atari8_sap_stream_seek(s,19)==ATARI8_SAPB_OK);
    int16_t tmp[53][2];
    size_t got;
    CHECK(atari8_sap_stream_read(s,tmp,53,&got)==ATARI8_SAPB_OK && got==53);
    CHECK(!memcmp(tmp,out+19,sizeof(tmp)));
    CHECK(atari8_sap_stream_set_repeat(s,true));
    CHECK(atari8_sap_stream_seek(s,len-7)==ATARI8_SAPB_OK);
    CHECK(atari8_sap_stream_read(s,tmp,53,&got)==ATARI8_SAPB_OK && got==53);
    CHECK(!memcmp(tmp+7,out,46*sizeof(*tmp)));
    CHECK(atari8_sap_stream_reset(s)==ATARI8_SAPB_OK);
    CHECK(atari8_sap_stream_read(s,tmp,53,&got)==ATARI8_SAPB_OK && got==53);
    CHECK(!memcmp(tmp,out,sizeof(tmp)));
    atari8_sap_stream_close(s);
}

static void *worker(void *arg)
{
    const Worker *w=arg;
    CHECK(!pthread_mutex_lock(&lock));
    ++ready;
    CHECK(!pthread_cond_broadcast(&cond));
    while(!go) CHECK(!pthread_cond_wait(&cond,&lock));
    CHECK(!pthread_mutex_unlock(&lock));
    int16_t out[FRAMES][2];
    for(unsigned i=0;i<ROUNDS;++i) {
        const Fixture *f=&w->fixtures[(w->index+i)%THREADS];
        exercise(f,out);
        CHECK(!memcmp(out,f->expected,sizeof(out)));
    }
    return NULL;
}

int main(void)
{
    Fixture f[THREADS]={0};
    pthread_t threads[THREADS];
    Worker args[THREADS];
    for(unsigned i=0;i<THREADS;++i) {
        fixture(&f[i],i%TYPES,i>=TYPES);
        exercise(&f[i],f[i].expected);
    }
    for(unsigned i=0;i<THREADS;++i) {
        args[i]=(Worker){f,i};
        CHECK(!pthread_create(&threads[i],NULL,worker,&args[i]));
    }
    CHECK(!pthread_mutex_lock(&lock));
    while(ready<THREADS) CHECK(!pthread_cond_wait(&cond,&lock));
    go=true;
    CHECK(!pthread_cond_broadcast(&cond));
    CHECK(!pthread_mutex_unlock(&lock));
    for(unsigned i=0;i<THREADS;++i) CHECK(!pthread_join(threads[i],NULL));
    CHECK(!pthread_cond_destroy(&cond));
    CHECK(!pthread_mutex_destroy(&lock));
    printf("PASS: %u independent concurrent SAP playback/seek/repeat sessions, %u threads\n",
           THREADS*ROUNDS,THREADS);
    return 0;
}
