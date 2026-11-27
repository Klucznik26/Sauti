#ifndef ATARI8_SAP_TEXT_H
#define ATARI8_SAP_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ATARI8_SAP_TEXT_CAPACITY 256u

typedef struct {
    char value[ATARI8_SAP_TEXT_CAPACITY];
    bool present;
    bool truncated;
} Atari8SapText;

typedef struct {
    Atari8SapText name, author, date;
} Atari8SapMetadata;

/* The existing header parser validates printable ASCII before calling
 * here. Keep metadata owned by the parsed info (no dangling pointers
 * after a caller releases the source file). Last duplicate wins, matching
 * existing acceptance of repeated optional tags. Overlong values are
 * explicitly marked as truncated rather than rejecting playable files. */
static inline bool atari8_sap_text_parse(Atari8SapText *out,
                                         const uint8_t *p, size_t n)
{
    if (n<2 || p[0]!='"' || p[n-1]!='"')
        return false;
    size_t length=n-2;
    memset(out,0,sizeof(*out));
    out->present=true;
    out->truncated=length>=sizeof(out->value);
    if (out->truncated) length=sizeof(out->value)-1;
    memcpy(out->value,p+1,length);
    out->value[length]='\0';
    return true;
}

#endif
