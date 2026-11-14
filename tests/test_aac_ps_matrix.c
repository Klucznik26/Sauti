#include "sauti_aac_ps_matrix.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int near(float a,float b){ return fabsf(a-b)<0.00001f; }

int main(void){
    SautiAacPsFrame frame={0};
    SautiAacPsMappedFrame mapped={0};
    SautiAacPsMatrix out={0},saved;
    frame.resolved_num_env=1u;
    frame.header.enable_iid=1u;
    frame.header.iid_mode=0u;
    frame.header.enable_icc=1u;
    frame.header.icc_mode=0u;
    mapped.bands=20u;
    mapped.env_count=1u;
    assert(sauti_aac_ps_matrix_build(&frame,&mapped,&out));
    assert(!out.variant_b&&near(out.coefficient[0][0].h11,1.0f)&&
           near(out.coefficient[0][0].h12,1.0f)&&
           near(out.coefficient[0][0].h21,0.0f)&&near(out.coefficient[0][0].h22,0.0f));

    mapped.iid[0][0]=7;
    assert(sauti_aac_ps_matrix_build(&frame,&mapped,&out));
    assert(near(out.coefficient[0][0].h11,1.4119828f)&&
           near(out.coefficient[0][0].h12,0.0794016f));

    mapped.iid[0][0]=0;
    mapped.icc[0][0]=7;
    assert(sauti_aac_ps_matrix_build(&frame,&mapped,&out));
    assert(near(out.coefficient[0][0].h11,0.0f)&&near(out.coefficient[0][0].h12,0.0f)&&
           near(out.coefficient[0][0].h21,1.0f)&&near(out.coefficient[0][0].h22,-1.0f));

    frame.header.icc_mode=3u;
    mapped.icc[0][0]=3;
    assert(sauti_aac_ps_matrix_build(&frame,&mapped,&out));
    assert(out.variant_b&&near(out.coefficient[0][0].h11,out.coefficient[0][0].h12)&&
           near(out.coefficient[0][0].h21,-out.coefficient[0][0].h22));

    saved=out;
    frame.header.icc_mode=0u;
    mapped.iid[0][0]=8;
    assert(!sauti_aac_ps_matrix_build(&frame,&mapped,&out));
    assert(memcmp(&out,&saved,sizeof(out))==0);
    puts("PASS AAC PS matrix: IID/ICC dequantization, variants A/B and atomicity");
    return 0;
}
