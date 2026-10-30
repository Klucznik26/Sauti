/* Atari Falcon Digital Tracker: reject truncated and mismatched modules.
 * Authentic replay/seek tests are a separate acceptance gate. */
#include "sauti_decoder.h"
#include "sauti_decoder_dtm.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void){
    const SautiDecoderOps *ops[]={&sauti_decoder_dtm_ops};
    SautiDecoder d={0};
    uint8_t buffer[128]={0};
    assert(sauti_decoder_dtm_ops.probe((const uint8_t *)"D.T.",4));
    assert(sauti_decoder_dtm_ops.probe((const uint8_t *)"SONG",4));
    assert(!sauti_decoder_dtm_ops.probe((const uint8_t *)"DTM!",4));
    assert(!sauti_decoder_dtm_ops.probe((const uint8_t *)"D.T.",3));
    assert(!sauti_decoder_dtm_ops.probe(NULL,0));
    assert(!sauti_decoder_open(&d,ops,1,buffer,sizeof(buffer)));
    memcpy(buffer,"D.T.",4);
    assert(!sauti_decoder_open(&d,ops,1,buffer,sizeof(buffer)));
    memcpy(buffer,"SONG",4);
    assert(!sauti_decoder_open(&d,ops,1,buffer,sizeof(buffer)));
    /* A valid signature alone must not make a malformed module playable. */
    puts("PASS: Atari DTM Digital Tracker magic and malformed module rejection");
    return 0;
}
