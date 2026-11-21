#ifndef SAUTI_USAC_SBR_H
#define SAUTI_USAC_SBR_H
/* USAC (xHE-AAC) enhanced SBR, the part that is the classic SBR with a different syntax (ISO/IEC 23003-3 7.5): usac_sbr_data() of a single channel element or a channel pair, 2:1 only.
 * Not supported (reported as -1 by the caller): the harmonic transposer, predictive vector coding (PVC), inter-subband temporal envelope shaping, bs_sbr_preprocessing = 1.
 * What is parsed here is handed to the same QMF / HF generation / adjustment chain (sauti_aac_sbr_channel.h) as HE-AAC; there is no separate eSBR signal path. */
#include "sauti_usac.h"
#include "sauti_aac_sbr_payload.h"

/* Parses usac_sbr_data(indep) for `channels` (1 = SCE, 2 = CPE) at the current position of `b`.  The payload frame of channel c is out[c]; *coupling tells a coupled pair.  1 = ok,
 * 0 = malformed, -1 = a tool that is not implemented.  `st` is changed only on success. */
int sauti_usac_sbr_parse(const SautiUsacElement *e, unsigned channels, SautiUsacBits *b, int indep, unsigned output_rate, SautiUsacSbrState *st, SautiAacSbrPayloadFrame out[2], unsigned *coupling);
#endif
