#include "sauti_aac_ps_map.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void){
    SautiAacPsFrame f={0};SautiAacPsMappedFrame out={0},saved;
    f.resolved_num_env=1u;f.iid_count=10u;f.icc_count=10u;
    for(unsigned i=0u;i<10u;i++){f.iid[0][i]=(int8_t)i;f.icc[0][i]=(int8_t)(7-i);}
    assert(sauti_aac_ps_map_frame(&f,&out));
    assert(out.bands==20u&&out.env_count==1u&&out.iid[0][0]==0&&
           out.iid[0][1]==0&&out.iid[0][18]==9&&out.iid[0][19]==9&&
           out.icc[0][0]==7&&out.icc[0][19]==-2);

    f=(SautiAacPsFrame){0};f.resolved_num_env=1u;f.iid_count=20u;f.icc_count=34u;
    for(unsigned i=0u;i<20u;i++)f.iid[0][i]=(int8_t)i;
    for(unsigned i=0u;i<34u;i++)f.icc[0][i]=(int8_t)(i-7);
    assert(sauti_aac_ps_map_frame(&f,&out));
    assert(out.bands==34u&&out.iid[0][0]==0&&out.iid[0][1]==0&&
           out.iid[0][2]==1&&out.iid[0][4]==2&&out.iid[0][16]==10&&
           out.iid[0][17]==11&&out.iid[0][33]==19&&out.icc[0][33]==26);

    f=(SautiAacPsFrame){0};f.resolved_num_env=1u;f.iid_count=10u;
    f.enable_ipdopd=1u;f.ipdopd_count=5u;
    for(unsigned i=0u;i<5u;i++){f.ipd[0][i]=(int8_t)(i+1);f.opd[0][i]=(int8_t)(7-i);}
    assert(sauti_aac_ps_map_frame(&f,&out));
    assert(out.ipd[0][0]==1&&out.ipd[0][1]==1&&out.ipd[0][8]==5&&
           out.ipd[0][9]==5&&out.ipd[0][10]==0&&out.opd[0][0]==7);

    saved=out;f.iid_count=12u;
    assert(!sauti_aac_ps_map_frame(&f,&out));
    assert(memcmp(&out,&saved,sizeof(out))==0);
    puts("PASS AAC PS mapping: 5/10/11/17/20/34 normalization and atomicity");
    return 0;
}
