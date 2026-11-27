#include "atari8_sapb_stream.h"
#include <stdlib.h>
#include <string.h>

/* A scheduler interval is bounded by the player watchdog plus grid
 * alignment. At the supported max rate (192kHz), one million frames
 * exceed even two maximum-length FASTPLAY intervals. Keep a hard
 * memory ceiling independently of guest behaviour. */
#define MAX_BUFFER_FRAMES 1000000u

typedef struct {
    Atari8Sapb decoder;
    int16_t (*buffer)[2];
    size_t capacity, begin, end;
    uint64_t position;
    Atari8SapbResult terminal;
    bool buffer_failed;
} StreamState;

struct Atari8SapbStream {
    uint8_t *source;
    size_t size;
    int song;
    uint32_t rate;
    bool declared_eof, frame_restart;
    StreamState *state;
};

static void capture(void *context, const int16_t frame[2])
{
    StreamState *s=context;
    if (s->buffer_failed) return;
    if (s->end==s->capacity) {
        size_t cap=s->capacity ? s->capacity*2u : 1024u;
        if (cap>MAX_BUFFER_FRAMES) cap=MAX_BUFFER_FRAMES;
        if (cap<=s->capacity) {
            s->buffer_failed=true;
            return;
        }
        void *p=realloc(s->buffer,cap*sizeof(*s->buffer));
        if (!p) {
            s->buffer_failed=true;
            return;
        }
        s->buffer=p;
        s->capacity=cap;
    }
    s->buffer[s->end][0]=frame[0];
    s->buffer[s->end][1]=frame[1];
    ++s->end;
}

static void free_state(StreamState *s)
{
    if (s) {
        free(s->buffer);
        free(s);
    }
}

static Atari8SapbResult new_state(const Atari8SapbStream *owner, StreamState **out)
{
    *out=NULL;
    StreamState *s=calloc(1,sizeof(*s));
    if (!s) return ATARI8_SAPB_RUNTIME;
    Atari8SapbResult r=atari8_sapb_open_mode(&s->decoder,owner->source,
        owner->size,owner->song,owner->rate,capture,s,owner->frame_restart);
    if (r!=ATARI8_SAPB_OK) {
        free_state(s);
        return r;
    }
    if (owner->declared_eof)
        (void)atari8_sapb_enable_declared_eof(&s->decoder);
    *out=s;
    return ATARI8_SAPB_OK;
}

Atari8SapbResult atari8_sapb_stream_open(Atari8SapbStream **out,
    const uint8_t *data, size_t size, int song, uint32_t rate,
    bool declared_eof, bool frame_restart)
{
    if (!out) return ATARI8_SAPB_BAD_FORMAT;
    *out=NULL;
    if (!data || !size || size>8u*1024u*1024u)
        return ATARI8_SAPB_BAD_FORMAT;
    Atari8SapbStream *s=calloc(1,sizeof(*s));
    if (!s) return ATARI8_SAPB_RUNTIME;
    s->source=malloc(size);
    if (!s->source) {
        free(s);
        return ATARI8_SAPB_RUNTIME;
    }
    memcpy(s->source,data,size);
    s->size=size;
    s->song=song;
    s->rate=rate;
    s->declared_eof=declared_eof;
    s->frame_restart=frame_restart;
    Atari8SapbResult r=new_state(s,&s->state);
    if (r!=ATARI8_SAPB_OK) {
        atari8_sapb_stream_close(s);
        return r;
    }
    *out=s;
    return ATARI8_SAPB_OK;
}

void atari8_sapb_stream_close(Atari8SapbStream *s)
{
    if (s) {
        free_state(s->state);
        free(s->source);
        free(s);
    }
}

/* NULL output here means discard, used only by private seek replay. */
static Atari8SapbResult consume(StreamState *s, int16_t (*out)[2],
    size_t capacity, size_t *produced)
{
    *produced=0;
    unsigned empty=0;
    while (*produced<capacity) {
        if (s->begin==s->end) {
            if (s->terminal!=ATARI8_SAPB_OK) return s->terminal;
            s->begin=s->end=0;
            s->terminal=atari8_sapb_interval(&s->decoder);
            if (s->buffer_failed) s->terminal=ATARI8_SAPB_RUNTIME;
            if (!s->end && s->terminal==ATARI8_SAPB_OK) {
                /* FASTPLAY can produce less than one PCM frame per
                 * interval, but must eventually make progress. */
                if (++empty>=1024u) {
                    s->terminal=ATARI8_SAPB_RUNTIME;
                    return s->terminal;
                }
                continue;
            }
            empty=0;
            if (!s->end) return s->terminal;
        }
        size_t n=s->end-s->begin;
        if (n>capacity-*produced) n=capacity-*produced;
        if ((uint64_t)n>UINT64_MAX-s->position) {
            s->terminal=ATARI8_SAPB_RUNTIME;
            return s->terminal;
        }
        if (out) memcpy(out+*produced,s->buffer+s->begin,n*sizeof(*out));
        s->begin+=n;
        s->position+=n;
        *produced+=n;
    }
    return ATARI8_SAPB_OK;
}

Atari8SapbResult atari8_sapb_stream_read(Atari8SapbStream *s,
    int16_t (*out)[2], size_t capacity, size_t *produced)
{
    if (!produced) return ATARI8_SAPB_BAD_FORMAT;
    *produced=0;
    if (!s || (!out && capacity) || capacity>SIZE_MAX/sizeof(*out))
        return ATARI8_SAPB_BAD_FORMAT;
    return consume(s->state,out,capacity,produced);
}

uint64_t atari8_sapb_stream_tell(const Atari8SapbStream *s)
{
    return s ? s->state->position : 0;
}

bool atari8_sapb_stream_length(const Atari8SapbStream *s, uint64_t *frames)
{
    if (!s || !frames || !s->state->decoder.machine.limit_output) return false;
    *frames=s->state->decoder.machine.output_limit;
    return true;
}

const Atari8SapbInfo *atari8_sapb_stream_info(const Atari8SapbStream *s)
{
    return s ? &s->state->decoder.info : NULL;
}

Atari8SapbResult atari8_sapb_stream_seek(Atari8SapbStream *s, uint64_t position)
{
    if (!s) return ATARI8_SAPB_BAD_FORMAT;
    uint64_t length;
    if (atari8_sapb_stream_length(s,&length) && position>length)
        return ATARI8_SAPB_BAD_FORMAT;
    StreamState *next;
    Atari8SapbResult r=new_state(s,&next);
    if (r!=ATARI8_SAPB_OK) return r;
    while (next->position<position) {
        uint64_t remaining=position-next->position;
        size_t n=remaining>65536u ? 65536u : (size_t)remaining;
        size_t got;
        r=consume(next,NULL,n,&got);
        if (r!=ATARI8_SAPB_OK) {
            free_state(next);
            return r;
        }
    }
    /* A fault reached during the last partially consumed interval
     * must not be hidden by a full discard. */
    if (next->terminal!=ATARI8_SAPB_OK && next->terminal!=ATARI8_SAPB_EOF) {
        r=next->terminal;
        free_state(next);
        return r;
    }
    free_state(s->state);
    s->state=next;
    return ATARI8_SAPB_OK;
}
