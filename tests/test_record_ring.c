#include "sauti_record_ring.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
int main(void){
    SautiRecordRing *ring=calloc(1,sizeof(*ring));assert(ring);
    float src[6]={1,2,3,4,5,6},out[6]={0};
    sauti_record_push(ring,src,3);
    assert(sauti_record_pop(ring,out,3)==0);
    sauti_record_reset(ring);
    sauti_record_enable(ring,1);
    sauti_record_push(ring,src,3);
    assert(sauti_record_pop(ring,out,2)==2);
    assert(out[0]==1&&out[1]==2&&out[2]==3&&out[3]==4);
    assert(sauti_record_pop(ring,out,3)==1);
    assert(out[0]==5&&out[1]==6);
    sauti_record_push(ring,src,SAUTI_RECORD_CAPACITY+1u);
    assert(sauti_record_overflow(ring));
    sauti_record_reset(ring);
    assert(!sauti_record_overflow(ring));
    free(ring);
    puts("Sauti recording ring tests passed");
    return 0;
}
