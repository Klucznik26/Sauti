#include "sauti_ym_archive.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
 uint8_t *out=(uint8_t *)(uintptr_t)1;
 size_t count=123;
 uint8_t raw[]={ 'Y','M','2','!',0,0 };
 assert(sauti_ym_unpack_lha(raw,sizeof(raw),&out,&count)==0);
 assert(out==NULL&&count==0);
 uint8_t bad[]={0x1d,0xd0,'-','l','h','5','-',0,0,0};
 assert(sauti_ym_unpack_lha(bad,sizeof(bad),&out,&count)==-1);
 assert(out==NULL&&count==0);
 assert(sauti_ym_unpack_lha(NULL,0,&out,&count)==0);
 assert(sauti_ym_unpack_lha(raw,sizeof(raw),NULL,&count)==-1);
 puts("PASS: LHA YM raw bypass, invalid-header rejection and ownership");
 return 0;
}
