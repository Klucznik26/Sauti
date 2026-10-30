/* Portable baseline MIDI synthesizer: sine voice approximation, NOT General MIDI.
 * A SoundFont synth is needed for instrument-accurate playback.
 */
#include "sauti_decoder_midi.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define TML_IMPLEMENTATION
#define TML_NO_STDIO
#include "../../../third_party/tml/tml.h"

#define MIDI_RATE 44100u
#define MIDI_VOICES 64
#define MIDI_TAU 6.2831853071795864769
typedef struct {
    unsigned char channel,key;
    double phase,step;
    float amplitude;
    int active;
} Voice;
typedef struct {
    tml_message *messages,*next;
    uint64_t frame,total;
    Voice voices[MIDI_VOICES];
} MidiState;
static int midi_probe(const uint8_t *p,size_t n){
    return p && n>=14 && memcmp(p,"MThd",4)==0;
}
static void midi_reset(MidiState *s){
    memset(s->voices,0,sizeof(s->voices));
    s->next=s->messages;
    s->frame=0;
}
static void midi_event(MidiState *s,const tml_message *m){
    unsigned ch=m->channel,key=(unsigned char)m->key;
    if(ch>=16 || key>=128)return;
    if(m->type==TML_NOTE_OFF || (m->type==TML_NOTE_ON && m->velocity==0)){
        for(int i=0;i<MIDI_VOICES;++i)
            if(s->voices[i].active && s->voices[i].channel==ch && s->voices[i].key==key)
                s->voices[i].active=0;
    }else if(m->type==TML_NOTE_ON){
        int slot=-1;
        for(int i=0;i<MIDI_VOICES;++i)
            if(!s->voices[i].active){slot=i;break;}
        if(slot<0)slot=0;
        Voice *v=&s->voices[slot];
        v->channel=(unsigned char)ch;v->key=(unsigned char)key;
        v->step=MIDI_TAU*440.0*pow(2.0,((int)key-69)/12.0)/MIDI_RATE;
        v->phase=0.0;
        v->amplitude=(float)(unsigned char)m->velocity/127.0f;
        v->active=1;
    }else if(m->type==TML_CONTROL_CHANGE && (unsigned char)m->control==123){
        for(int i=0;i<MIDI_VOICES;++i)
            if(s->voices[i].channel==ch)s->voices[i].active=0;
    }
}
static void midi_step(MidiState *s,float *out){
    while(s->next && (uint64_t)s->next->time*MIDI_RATE<=s->frame*1000u){
        midi_event(s,s->next);
        s->next=s->next->next;
    }
    float value=0.0f;
    for(int v=0;v<MIDI_VOICES;++v){
        Voice *voice=&s->voices[v];
        if(!voice->active)continue;
        value+=(float)sin(voice->phase)*voice->amplitude*(0.15f);
        voice->phase+=voice->step;
        if(voice->phase>=MIDI_TAU)voice->phase-=MIDI_TAU;
    }
    if(value>1.0f)value=1.0f;
    if(value< -1.0f)value= -1.0f;
    if(out){out[0]=value;out[1]=value;}
    ++s->frame;
}
static void *midi_open(const uint8_t *data,size_t len,SautiPcmInfo *info){
    if(!midi_probe(data,len) || !info || len>INT32_MAX)return NULL;
    tml_message *messages=tml_load_memory(data,(int)len);
    if(!messages)return NULL;
    unsigned int ms=0;
    tml_get_info(messages,NULL,NULL,NULL,NULL,&ms);
    MidiState *s=calloc(1,sizeof(*s));
    if(!s){tml_free(messages);return NULL;}
    s->messages=messages;
    s->total=((uint64_t)ms+1000u)*MIDI_RATE/1000u;
    midi_reset(s);
    info->sample_rate=MIDI_RATE;
    info->channels=2;
    info->total_frames=s->total;
    return s;
}
static size_t midi_read(void *ctx,float *samples,size_t n){
    MidiState *s=ctx;
    if(n>s->total-s->frame)n=(size_t)(s->total-s->frame);
    for(size_t i=0;i<n;++i)midi_step(s,samples+2*i);
    return n;
}
static int midi_seek(void *ctx,uint64_t frame){
    MidiState *s=ctx;
    if(frame>s->total)return 0;
    if(frame<s->frame)midi_reset(s);
    /* Accurate but linear seek: optimize with MIDI event snapshots later. */
    while(s->frame<frame)midi_step(s,NULL);
    return 1;
}
static void midi_close(void *ctx){
    MidiState *s=ctx;
    tml_free(s->messages);
    free(s);
}
const SautiDecoderOps sauti_decoder_midi_ops={
    "midi-basic-synth",midi_probe,midi_open,midi_read,midi_seek,midi_close
};
