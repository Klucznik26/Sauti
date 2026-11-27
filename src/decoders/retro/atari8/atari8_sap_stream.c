#include "atari8_sap_stream.h"
#include <stdlib.h>
#include <string.h>

struct Atari8SapStream {
    Atari8SapStreamInfo info;
    Atari8SapbStream *exec;
    Atari8Sapr reg;
    uint8_t *source; /* R only; executable backend owns its input */
    bool repeat;
};

static Atari8SapbResult map_r(Atari8SaprResult r)
{
    switch(r) {
    case ATARI8_SAPR_OK: return ATARI8_SAPB_OK;
    case ATARI8_SAPR_EOF: return ATARI8_SAPB_EOF;
    case ATARI8_SAPR_BAD_FORMAT: return ATARI8_SAPB_BAD_FORMAT;
    default: return ATARI8_SAPB_UNSUPPORTED;
    }
}

Atari8SapbResult atari8_sap_stream_open(Atari8SapStream **out,
    const uint8_t *data, size_t size, int song, uint32_t rate,
    bool declared_eof, bool frame_restart)
{
    if (!out) return ATARI8_SAPB_BAD_FORMAT;
    *out=NULL;
    if (!data || !size || size>64u*1024u*1024u)
        return ATARI8_SAPB_BAD_FORMAT;
    Atari8SaprInfo ri;
    Atari8SaprResult rr=atari8_sapr_parse(data,size,&ri);
    Atari8SapStream *s=calloc(1,sizeof(*s));
    if (!s) return ATARI8_SAPB_RUNTIME;
    Atari8SapbResult r;
    if (rr==ATARI8_SAPR_OK) {
        if (song < -1 || song > 0) r=ATARI8_SAPB_BAD_FORMAT;
        else if (frame_restart) r=ATARI8_SAPB_UNSUPPORTED;
        else {
            s->source=malloc(size);
            if (!s->source) r=ATARI8_SAPB_RUNTIME;
            else {
                memcpy(s->source,data,size);
                r=map_r(atari8_sapr_open(&s->reg,s->source,size,rate));
                if (r==ATARI8_SAPB_OK) {
                    s->info.type='R';
                    s->info.channels=ri.stereo ? 2u : 1u;
                    s->info.songs=1;
                    s->info.time=ri.time;
                    s->info.metadata=ri.metadata;
                }
            }
        }
    } else {
        r=atari8_sapb_stream_open(&s->exec,data,size,song,rate,declared_eof,frame_restart);
        if (r==ATARI8_SAPB_OK) {
            const Atari8SapbInfo *i=atari8_sapb_stream_info(s->exec);
            s->info.type=i->type;
            s->info.channels=i->stereo ? 2u : 1u;
            s->info.songs=i->songs;
            s->info.default_song=i->default_song;
            s->info.selected_song=song<0 ? i->default_song : (unsigned)song;
            s->info.time=i->time[s->info.selected_song];
            s->info.metadata=i->metadata;
        }
    }
    if (r!=ATARI8_SAPB_OK) {
        atari8_sap_stream_close(s);
        return r;
    }
    s->info.rate=rate;
    *out=s;
    return ATARI8_SAPB_OK;
}

void atari8_sap_stream_close(Atari8SapStream *s)
{
    if (s) {
        atari8_sapb_stream_close(s->exec);
        free(s->source);
        free(s);
    }
}

const Atari8SapStreamInfo *atari8_sap_stream_info(const Atari8SapStream *s)
{
    return s ? &s->info : NULL;
}

bool atari8_sap_stream_length(const Atari8SapStream *s,uint64_t *frames)
{
    if (!s || !frames) return false;
    if (s->exec) return atari8_sapb_stream_length(s->exec,frames);
    *frames=atari8_sapr_length(&s->reg);
    return true;
}

uint64_t atari8_sap_stream_tell(const Atari8SapStream *s)
{
    if (!s) return 0;
    return s->exec ? atari8_sapb_stream_tell(s->exec) : atari8_sapr_tell(&s->reg);
}

Atari8SapbResult atari8_sap_stream_seek(Atari8SapStream *s,uint64_t position)
{
    if (!s) return ATARI8_SAPB_BAD_FORMAT;
    return s->exec ? atari8_sapb_stream_seek(s->exec,position)
        : map_r(atari8_sapr_seek(&s->reg,position));
}

Atari8SapbResult atari8_sap_stream_reset(Atari8SapStream *s)
{
    return atari8_sap_stream_seek(s,0);
}

bool atari8_sap_stream_set_repeat(Atari8SapStream *s,bool repeat)
{
    if (!s) return false;
    uint64_t length;
    if (repeat && (!atari8_sap_stream_length(s,&length) || !length))
        return false;
    s->repeat=repeat;
    return true;
}

static Atari8SapbResult read_once(Atari8SapStream *s,
    int16_t (*out)[2],size_t capacity,size_t *produced)
{
    if (s->exec) return atari8_sapb_stream_read(s->exec,out,capacity,produced);
    *produced=0;
    while (*produced<capacity) {
        Atari8SaprResult r=atari8_sapr_next(&s->reg,out[*produced]);
        if (r!=ATARI8_SAPR_OK) return map_r(r);
        ++*produced;
    }
    return ATARI8_SAPB_OK;
}

Atari8SapbResult atari8_sap_stream_read(Atari8SapStream *s,
    int16_t (*out)[2],size_t capacity,size_t *produced)
{
    if (!produced) return ATARI8_SAPB_BAD_FORMAT;
    *produced=0;
    if (!s || (!out && capacity) || capacity>SIZE_MAX/sizeof(*out))
        return ATARI8_SAPB_BAD_FORMAT;
    bool just_reset=false;
    while (*produced<capacity) {
        size_t got;
        Atari8SapbResult r=read_once(s,out+*produced,capacity-*produced,&got);
        *produced+=got;
        if (r!=ATARI8_SAPB_EOF || !s->repeat) return r;
        if (!got && just_reset) return ATARI8_SAPB_RUNTIME;
        r=atari8_sap_stream_reset(s);
        if (r!=ATARI8_SAPB_OK) return r;
        just_reset=true;
    }
    return ATARI8_SAPB_OK;
}
