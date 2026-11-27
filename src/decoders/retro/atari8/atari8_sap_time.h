#ifndef ATARI8_SAP_TIME_H
#define ATARI8_SAP_TIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum {
    ATARI8_SAP_TIME_ABSENT = 0,
    ATARI8_SAP_TIME_VALID,
    ATARI8_SAP_TIME_INVALID
} Atari8SapTimeStatus;

typedef struct {
    uint32_t duration_ms;
    Atari8SapTimeStatus status;
    bool loop;
} Atari8SapTime;

/* Match the archive diagnostic convention: m:ss or mm:ss,
 * optional 1..3 fractional digits and optional literal " LOOP".
 * Zero is a valid declaration, distinct from absent/invalid metadata.
 * Do not let malformed optional metadata change playback acceptance. */
static inline Atari8SapTime atari8_sap_time_parse(const uint8_t *p, size_t n)
{
    Atari8SapTime t = {0, ATARI8_SAP_TIME_INVALID, false};
    if (n >= 5 && !memcmp(p+n-5," LOOP",5)) {
        t.loop = true;
        n -= 5;
    }
    size_t i=0;
    unsigned min=0, sec=0, fraction=0, digits=0;
    while (i<n && i<2 && p[i]>='0' && p[i]<='9') {
        min=min*10u+(unsigned)(p[i]-'0');
        ++i;
    }
    if (!i || n-i<3 || p[i++]!=':') goto invalid;
    if (p[i]<'0' || p[i]>'9' || p[i+1]<'0' || p[i+1]>'9')
        goto invalid;
    sec=(unsigned)(p[i]-'0')*10u+(unsigned)(p[i+1]-'0');
    i+=2;
    if (sec>=60) goto invalid;
    if (i<n) {
        if (p[i++]!='.') goto invalid;
        while (i<n && digits<3 && p[i]>='0' && p[i]<='9') {
            fraction=fraction*10u+(unsigned)(p[i++]-'0');
            ++digits;
        }
        if (!digits || i!=n) goto invalid;
        while (digits++<3) fraction*=10u;
    }
    t.duration_ms=(min*60u+sec)*1000u+fraction;
    t.status=ATARI8_SAP_TIME_VALID;
    return t;
invalid:
    t.loop=false;
    return t;
}

#endif
