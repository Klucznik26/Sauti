/* Wielobieznosc natywnych adapterow AAC: ten sam zestaw plikow dekodowany jednoczesnie przez wiele watkow (kazdy watek: wlasne otwarcia, jedno wspolne,
 * tylko do odczytu, ziarno pliku) musi dac PCM bit w bit taki jak dekodowanie w jednym watku; w tym przewijanie w trakcie.  Uruchamiany pod ThreadSanitizer i
 * AddressSanitizer przez scripts/test_aac_threads.sh (pliki z fdkaac/FFmpeg z sygnalu syntetycznego).
 * usage: test_aac_threads THREADS ROUNDS FILE... */
#include "sauti_decoder.h"
#include "sauti_decoder_aac_native.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *bytes; size_t n; uint64_t hash,after_seek_hash; uint64_t total; unsigned channels; } File;
static const SautiDecoderOps *const registry[]={&sauti_decoder_aac_native_ops,&sauti_decoder_aac_mono_native_ops,&sauti_decoder_aac_multi_native_ops,
    &sauti_decoder_aac_mp4_native_ops,&sauti_decoder_aac_latm_native_ops,&sauti_decoder_aac_adif_native_ops,&sauti_decoder_aac_he_native_ops,
    &sauti_decoder_aac_he_cpe_native_ops};
static uint64_t fnv(uint64_t h,const void *p,size_t n){const uint8_t *b=p;for(size_t i=0;i<n;i++){h^=b[i];h*=1099511628211ull;}return h;}
/* decode the whole file; second hash: seek to the middle after 3 blocks and decode 4 blocks */
static int decode(const File *f,uint64_t *hash,uint64_t *seek_hash,uint64_t *total,unsigned *channels){
    SautiDecoder d;
    if(!sauti_legacy_decoder_open(&d,registry,sizeof registry/sizeof registry[0],f->bytes,f->n))return 0;
    static __thread float buf[4096*8];
    uint64_t h=1469598103934665603ull,t=0;size_t got;
    while((got=sauti_legacy_decoder_read(&d,buf,2048u))>0u){h=fnv(h,buf,got*d.info.channels*sizeof(float));t+=got;}
    uint64_t sh=1469598103934665603ull;
    const uint64_t mid=d.info.total_frames/2u;
    if(!sauti_legacy_decoder_seek(&d,mid)){sauti_legacy_decoder_close(&d);return 0;}
    for(int i=0;i<4&&(got=sauti_legacy_decoder_read(&d,buf,2048u))>0u;i++)sh=fnv(sh,buf,got*d.info.channels*sizeof(float));
    *hash=h;*seek_hash=sh;*total=t;*channels=d.info.channels;
    sauti_legacy_decoder_close(&d);
    return 1;
}
typedef struct { const File *files; int nfiles,rounds,id; int failures; } Job;
static void *worker(void *arg){
    Job *j=arg;
    for(int r=0;r<j->rounds;r++)
        for(int k=0;k<j->nfiles;k++){
            const File *f=&j->files[(k+j->id+r)%j->nfiles];
            uint64_t h,sh,t;unsigned ch;
            if(!decode(f,&h,&sh,&t,&ch)||h!=f->hash||sh!=f->after_seek_hash||t!=f->total){
                fprintf(stderr,"thread %d round %d: MISMATCH on a file (hash %llx vs %llx)\n",j->id,r,(unsigned long long)h,(unsigned long long)f->hash);
                j->failures++;
            }
        }
    return NULL;
}
int main(int argc,char **argv){
    if(argc<4)return 2;
    const int threads=atoi(argv[1]),rounds=atoi(argv[2]),nfiles=argc-3;
    File *files=calloc((size_t)nfiles,sizeof(*files));
    for(int i=0;i<nfiles;i++){
        FILE *fp=fopen(argv[3+i],"rb");if(!fp){perror(argv[3+i]);return 2;}
        fseek(fp,0,SEEK_END);files[i].n=(size_t)ftell(fp);fseek(fp,0,SEEK_SET);
        files[i].bytes=malloc(files[i].n);if(!files[i].bytes||fread(files[i].bytes,1,files[i].n,fp)!=files[i].n)return 2;fclose(fp);
        if(!decode(&files[i],&files[i].hash,&files[i].after_seek_hash,&files[i].total,&files[i].channels)){fprintf(stderr,"cannot decode %s\n",argv[3+i]);return 2;}
    }
    pthread_t *tid=calloc((size_t)threads,sizeof(*tid));Job *jobs=calloc((size_t)threads,sizeof(*jobs));
    for(int t=0;t<threads;t++){jobs[t]=(Job){files,nfiles,rounds,t,0};pthread_create(&tid[t],NULL,worker,&jobs[t]);}
    int failures=0;
    for(int t=0;t<threads;t++){pthread_join(tid[t],NULL);failures+=jobs[t].failures;}
    printf("threads: %d threads x %d rounds x %d files, %d mismatches\n",threads,rounds,nfiles,failures);
    for(int i=0;i<nfiles;i++)free(files[i].bytes);
    free(files);free(tid);free(jobs);
    return failures?1:0;
}
