#include "sauti_aac_ps_matrix.h"

#include <math.h>

/* ISO/IEC 14496-3 PS IID dequantizer and the two ICC matrix variants.  The
 * short scalar calculation runs at most 170 times per frame; SIMD/assembly
 * belongs in the later complex hybrid-slot mixing loop, not here. */
static const float iid_default[15] = {
    0.05623413251903f,0.12589254117942f,0.19952623149689f,
    0.31622776601684f,0.44668359215096f,0.63095734448019f,
    0.79432823472428f,1.0f,1.25892541179417f,1.58489319246111f,
    2.23872113856834f,3.16227766016838f,5.01187233627272f,
    7.94328234724282f,17.7827941003892f
};
static const float iid_fine[31] = {
    0.00316227766017f,0.00562341325190f,0.01000000000000f,
    0.01778279410039f,0.03162277660168f,0.05623413251903f,
    0.07943282347243f,0.11220184543020f,0.15848931924611f,
    0.22387211385683f,0.31622776601684f,0.39810717055350f,
    0.50118723362727f,0.63095734448019f,0.79432823472428f,
    1.00000000000000f,1.25892541179417f,1.58489319246111f,
    1.99526231496888f,2.51188643150958f,3.16227766016838f,
    4.46683592150963f,6.30957344480193f,8.91250938133745f,
    12.5892541179417f,17.7827941003892f,31.6227766016838f,
    56.2341325190349f,100.0000000000000f,177.827941003892f,
    316.227766016837f
};
static const float icc_inverse[8] = {
    1.0f,0.937f,0.84118f,0.60092f,0.36764f,0.0f,-0.589f,-1.0f
};
static const float icc_angle[8] = {
    0.0f,0.35685527f,0.57133466f,0.92614472f,1.1943263f,
    1.5707963267948966f,2.2006171f,3.1415926535897932f
};

static int make_one(int iid,int icc,unsigned fine,unsigned variant_b,
                    SautiAacPsMatrixCoefficients *out){
    const int limit=fine?15:7;
    if(!out||iid<-limit||iid>limit||icc<0||icc>7)return 0;
    const float c=fine?iid_fine[iid+15]:iid_default[iid+7];
    const float c1=1.4142135623730951f/sqrtf(1.0f+c*c);
    const float c2=c*c1;
    SautiAacPsMatrixCoefficients next;
    if(!variant_b){
        const float alpha=0.5f*icc_angle[icc];
        const float beta=alpha*(c1-c2)*0.7071067811865475f;
        next.h11=c2*cosf(beta+alpha);
        next.h12=c1*cosf(beta-alpha);
        next.h21=c2*sinf(beta+alpha);
        next.h22=c1*sinf(beta-alpha);
    }else{
        const float rho=fmaxf(icc_inverse[icc],0.05f);
        float alpha=0.5f*atan2f(2.0f*c*rho,c*c-1.0f);
        const float mu0=c+1.0f/c;
        const float mu=sqrtf(1.0f+(4.0f*rho*rho-4.0f)/(mu0*mu0));
        const float gamma=atanf(sqrtf((1.0f-mu)/(1.0f+mu)));
        if(alpha<0.0f)alpha+=1.5707963267948966f;
        const float ac=cosf(alpha),as=sinf(alpha),gc=cosf(gamma),gs=sinf(gamma);
        next.h11=1.4142135623730951f*ac*gc;
        next.h12=1.4142135623730951f*as*gc;
        next.h21=-1.4142135623730951f*as*gs;
        next.h22=1.4142135623730951f*ac*gs;
    }
    if(!isfinite(next.h11)||!isfinite(next.h12)||
       !isfinite(next.h21)||!isfinite(next.h22))return 0;
    *out=next;
    return 1;
}

int sauti_aac_ps_matrix_build(const SautiAacPsFrame *frame,
                              const SautiAacPsMappedFrame *mapped,
                              SautiAacPsMatrix *out){
    if(!frame||!mapped||!out||!mapped->env_count||
       mapped->env_count>SAUTI_AAC_PS_MAX_ENVELOPES||
       mapped->env_count!=frame->resolved_num_env||
       (mapped->bands!=20u&&mapped->bands!=34u)||frame->header.iid_mode>5u||
       frame->header.icc_mode>5u)return 0;
    const unsigned fine=frame->header.enable_iid&&frame->header.iid_mode>2u;
    SautiAacPsMatrix next={0};
    next.bands=mapped->bands;
    next.env_count=mapped->env_count;
    next.variant_b=frame->header.enable_icc&&frame->header.icc_mode>=3u;
    for(unsigned e=0u;e<next.env_count;e++){
        for(unsigned b=0u;b<next.bands;b++){
            if(!make_one(mapped->iid[e][b],mapped->icc[e][b],fine,next.variant_b,
                         &next.coefficient[e][b]))return 0;
        }
    }
    *out=next;
    return 1;
}
