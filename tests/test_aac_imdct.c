#include "sauti_aac_imdct.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double reference(const float *x,size_t n,size_t t){
    const double pi=3.14159265358979323846264338327950288;
    double result=0;
    for(size_t k=0;k<n;k++)
        result+=(double)x[k]*cos((pi/(double)n)*
                    ((double)t+0.5+(double)n/2.0)*((double)k+0.5));
    return result*2.0/(double)n;
}
static void near(float actual,double expected){
    double delta=fabs((double)actual-expected);
    assert(delta<=0.00002+fabs(expected)*0.00003);
}
static void run(size_t n){
    float x[1024]={0};float y[2048];
    for(size_t i=0;i<2048;i++)y[i]=123.0f;
    assert(sauti_aac_imdct(x,n,y));
    for(size_t t=0;t<2*n;t++)assert(y[t]==0);
    /* Known single-bin IMDCT impulse; independently evaluate cosine. */
    x[0]=1.25f;x[n/4]=-0.75f;
    x[n/2]=2.0f;x[n-1]=-0.25f;
    assert(sauti_aac_imdct(x,n,y));
    for(size_t t=0;t<2*n;t++){
        if(n==128 || t%127u==0 || t==2*n-1)
            near(y[t],reference(x,n,t));
        assert(isfinite(y[t]));
    }
    /* Exercise a dense spectrum with mixed signs, not just sparse bins. */
    for(size_t k=0;k<n;k++)
        x[k]=(float)((int)(k%17u)-8)*0.125f;
    assert(sauti_aac_imdct(x,n,y));
    for(size_t t=0;t<2*n;t++)
        if(n==128 || t%113u==0 || t==2*n-1)
            near(y[t],reference(x,n,t));
    float previous[2048];
    memcpy(previous,y,sizeof(previous));
    x[1]=NAN;
    assert(!sauti_aac_imdct(x,n,y));
    assert(memcmp(previous,y,sizeof(previous))==0);
    x[1]=INFINITY;
    assert(!sauti_aac_imdct(x,n,y));
    assert(memcmp(previous,y,sizeof(previous))==0);
}
int main(void){
    float spectrum[1024]={0};
    float output[2048];
    for(size_t i=0;i<2048;i++)output[i]=77.0f;
    assert(!sauti_aac_imdct(NULL,128,output));
    assert(!sauti_aac_imdct(spectrum,128,NULL));
    assert(!sauti_aac_imdct(spectrum,129,output));
    assert(!sauti_aac_imdct(spectrum,0,output));
    for(size_t i=0;i<2048;i++)assert(output[i]==77.0f);
    run(128);
    run(1024);
    puts("PASS AAC IMDCT NASM: 128/1024, reference cosine, rollback");
    return 0;
}
