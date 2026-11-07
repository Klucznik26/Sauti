#include "sauti_aac_ps_map.h"
#include <string.h>

/* Integer forms of ISO PS parameter remapping (Table 8.46 family). The
 * arithmetic is intentionally scalar: this configuration work runs once per
 * envelope, not in the hybrid-QMF inner loop. */
static void map10to20(int8_t d[34],const int8_t s[34],unsigned full){
    unsigned top=full?10u:5u;
    memset(d,0,34u);
    for(unsigned i=0u;i<top;i++)d[2u*i]=d[2u*i+1u]=s[i];
}
static void map34to20(int8_t d[34],const int8_t s[34],unsigned full){
    d[0]=(int8_t)((2*s[0]+s[1])/3); d[1]=(int8_t)((s[1]+2*s[2])/3);
    d[2]=(int8_t)((2*s[3]+s[4])/3); d[3]=(int8_t)((s[4]+2*s[5])/3);
    d[4]=(int8_t)((s[6]+s[7])/2); d[5]=(int8_t)((s[8]+s[9])/2);
    d[6]=s[10];d[7]=s[11];d[8]=(int8_t)((s[12]+s[13])/2);
    d[9]=(int8_t)((s[14]+s[15])/2);d[10]=s[16];
    if(full){d[11]=s[17];d[12]=s[18];d[13]=s[19];d[14]=(int8_t)((s[20]+s[21])/2);
        d[15]=(int8_t)((s[22]+s[23])/2);d[16]=(int8_t)((s[24]+s[25])/2);
        d[17]=(int8_t)((s[26]+s[27])/2);d[18]=(int8_t)((s[28]+s[29]+s[30]+s[31])/4);
        d[19]=(int8_t)((s[32]+s[33])/2);}
}
static void map10to34(int8_t d[34],const int8_t s[34],unsigned full){
    static const uint8_t map[17]={0,0,0,1,1,1,2,2,2,2,3,3,4,4,4,4,5};
    memset(d,0,34u);for(unsigned i=0;i<17u;i++)d[i]=s[map[i]];
    if(full){static const uint8_t high[17]={5,6,6,7,7,7,7,8,8,8,8,9,9,9,9,9,9};
        for(unsigned i=0;i<17u;i++)d[17u+i]=s[high[i]];}
}
static void map20to34(int8_t d[34],const int8_t s[34],unsigned full){
    memset(d,0,34u);d[0]=s[0];d[1]=(int8_t)((s[0]+s[1])/2);d[2]=s[1];
    d[3]=s[2];d[4]=(int8_t)((s[2]+s[3])/2);d[5]=s[3];d[6]=s[4];d[7]=s[4];
    d[8]=s[5];d[9]=s[5];d[10]=s[6];d[11]=s[7];d[12]=s[8];d[13]=s[8];
    d[14]=s[9];d[15]=s[9];d[16]=s[10];
    if(full){static const uint8_t high[17]={11,12,13,14,14,15,15,16,16,17,17,18,18,18,18,19,19};
        for(unsigned i=0;i<17u;i++)d[17u+i]=s[high[i]];}
}
static int map_one(int8_t d[34],const int8_t s[34],unsigned count,unsigned target,unsigned full){
    if(!d||!s||(target!=20u&&target!=34u))return 0;
    memset(d,0,34u);
    if(!count)return 1;
    if(target==20u){
        if(count==34u||count==17u)map34to20(d,s,full);
        else if(count==10u||count==5u)map10to20(d,s,full);
        else if(count==20u||count==11u)memcpy(d,s,count);
        else return 0;
    }else{
        if(count==34u||count==17u)memcpy(d,s,count);
        else if(count==20u||count==11u)map20to34(d,s,full);
        else if(count==10u||count==5u)map10to34(d,s,full);
        else return 0;
    }
    return 1;
}
int sauti_aac_ps_map_frame(const SautiAacPsFrame *frame,SautiAacPsMappedFrame *out){
    if(!frame||!out||!frame->resolved_num_env||
       frame->resolved_num_env>SAUTI_AAC_PS_MAX_ENVELOPES)return 0;
    const unsigned bands=(frame->iid_count==34u||frame->icc_count==34u)?34u:20u;
    SautiAacPsMappedFrame next={0};next.bands=bands;next.env_count=frame->resolved_num_env;
    for(unsigned e=0;e<next.env_count;e++){
        if(!map_one(next.iid[e],frame->iid[e],frame->iid_count,bands,1u)||
           !map_one(next.icc[e],frame->icc[e],frame->icc_count,bands,1u)||
           !map_one(next.ipd[e],frame->ipd[e],frame->enable_ipdopd?frame->ipdopd_count:0u,bands,0u)||
           !map_one(next.opd[e],frame->opd[e],frame->enable_ipdopd?frame->ipdopd_count:0u,bands,0u))return 0;
    }
    *out=next;return 1;
}
