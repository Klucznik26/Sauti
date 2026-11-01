#ifndef SAUTI_AAC_TNS_H
#define SAUTI_AAC_TNS_H
#include "sauti_aac_syntax.h"
#include <stddef.h>
#include <stdint.h>
#define SAUTI_AAC_TNS_MAX_WINDOWS 8u
#define SAUTI_AAC_TNS_MAX_FILTERS 4u
#define SAUTI_AAC_TNS_MAX_ORDER 20u

typedef struct {
    uint8_t length;    /* scalefactor-band count */
    uint8_t order;
    uint8_t direction;
    uint8_t resolution; /* 3- or 4-bit unsigned base resolution */
    uint8_t compress;
    int8_t coefficients[SAUTI_AAC_TNS_MAX_ORDER];
} SautiAacTnsFilter;

typedef struct {
    uint8_t filter_count[SAUTI_AAC_TNS_MAX_WINDOWS];
    SautiAacTnsFilter filter[SAUTI_AAC_TNS_MAX_WINDOWS][SAUTI_AAC_TNS_MAX_FILTERS];
} SautiAacTnsData;

/* Consume tns_data() immediately after tns_data_present=1.
 * Output and bit position are atomic on error.
 * This is structural parsing only: it does NOT apply inverse TNS. */
int sauti_aac_tns_parse(const uint8_t *data,size_t size,size_t *bit_offset,
                        const SautiAacIcsInfo *ics,SautiAacTnsData *out);
#endif
