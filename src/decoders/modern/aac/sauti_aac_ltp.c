/* AAC LTP, see sauti_aac_ltp.h.  Written from ISO/IEC 14496-3 4.6.6. */
#include "sauti_aac_ltp.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_imdct.h"
#include "sauti_aac_layout.h"
#include "sauti_aac_window.h"
#include <math.h>
#include <string.h>

/* ltp_coef gains (Table 4.xx of the standard) */
static const float ltp_gain[8]={0.570829f,0.696616f,0.813004f,0.911304f,0.984900f,1.067894f,1.194601f,1.369533f};

void sauti_aac_ltp_reset(SautiAacLtpState *state){memset(state,0,sizeof(*state));}

/* Forward MDCT as the folding of the 2048 samples into 1024 followed by a DCT-IV; the DCT-IV is taken from the IMDCT, whose output holds it in two quarters (y = 2/N sum v cos...). */
int sauti_aac_ltp_mdct(const float in[2048],float out[1024]){
    float u[1024],y[2048];
    for(unsigned m=0;m<512u;m++)u[m]=-in[1535u-m]-in[1536u+m];
    for(unsigned m=512u;m<1024u;m++)u[m]=in[m-512u]-in[1535u-m];
    if(!sauti_aac_imdct(u,1024u,y))return 0;
    for(unsigned m=0;m<512u;m++)out[m]=-512.0f*y[1535u-m];
    for(unsigned m=512u;m<1024u;m++)out[m]=512.0f*y[m-512u];
    return 1;
}
int sauti_aac_ltp_apply(const SautiAacLtpState *state,const SautiAacIcsInfo *ics,unsigned idx,unsigned previous_shape,const SautiAacTnsData *tns,float spectral[1024]){
    const uint16_t *offsets=NULL;unsigned bands=0;
    if(!state||!ics||!spectral||ics->window_sequence>3u||previous_shape>1u||ics->window_shape>1u||
       !sauti_aac_band_offsets(idx,0u,&offsets,&bands))return 0;
    if(!ics->ltp_present||ics->window_sequence==2u)return 1;
    if(ics->ltp_lag>2047u||ics->ltp_coef>7u||SAUTI_AAC_IDX_LAYOUT(idx)!=SAUTI_AAC_LAYOUT_1024)return 0;
    float weight[2048],pred[2048],freq[1024];
    if(!sauti_aac_window_long_weights(1024u,ics->window_sequence,previous_shape,ics->window_shape,weight))return 0;
    const unsigned lag=ics->ltp_lag,count=lag<1024u?lag+1024u:2048u;
    const float gain=ltp_gain[ics->ltp_coef];
    for(unsigned i=0;i<2048u;i++)pred[i]=(i<count?state->buf[i+2048u-lag]*gain:0.0f)*weight[i];
    if(!sauti_aac_ltp_mdct(pred,freq))return 0;
    if(tns && !sauti_aac_tns_analysis(tns,ics,idx,freq))return 0;
    const unsigned n=ics->max_sfb<40u?ics->max_sfb:40u;
    float next[1024];
    memcpy(next,spectral,sizeof next);
    for(unsigned sfb=0;sfb<n&&sfb<bands;sfb++)
        if(ics->ltp_used[sfb])
            for(unsigned k=offsets[sfb];k<offsets[sfb+1u];k++)next[k]+=freq[k];
    for(unsigned k=0;k<1024u;k++)if(!isfinite(next[k]))return 0;
    memcpy(spectral,next,sizeof next);
    return 1;
}
void sauti_aac_ltp_update(SautiAacLtpState *state,const float pcm[1024],const float overlap[1024]){
    memmove(state->buf,state->buf+1024,1024*sizeof(float));
    memcpy(state->buf+1024,pcm,1024*sizeof(float));
    memcpy(state->buf+2048,overlap,1024*sizeof(float));
}
