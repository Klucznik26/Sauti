/* AAC-LC with any PCE layout and coupling channel elements (sauti_aac_lcw): the PCE parser (standard positions, other layouts, limits), element-to-slot assignment by (type, tag), more than 8
 * channels, the syntax of coupling channel elements (all three coupling points, gain lists), fail-closed refusals and atomicity.  Frames are assembled bit by bit from elements with max_sfb = 0
 * (silence), so the test is self-contained; the arithmetic of the coupling and the PCM against FFmpeg are checked on the ISO/IEC 14496-26 conformance streams by
 * scripts/test_iso_conformance_vs_ffmpeg.py (al07 / am05: coupling; al08: 48 channels). */
#include "sauti_aac_lcw.h"
#include "sauti_aac_tables.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t b[4096]; size_t bits; } Bits;
static void put(Bits *b,unsigned width,unsigned value){
    while(width--){ b->b[b->bits/8]|=(uint8_t)(((value>>width)&1u)<<(7-b->bits%8)); b->bits++; }
}
static void ics_body(Bits *b){ put(b,8,100); put(b,1,0); put(b,2,0); put(b,1,0); put(b,6,0); put(b,1,0); put(b,1,0); put(b,1,0); put(b,1,0); }   /* global_gain, ics_info (max_sfb 0), pulse, tns, gain control */
static void sce(Bits *b,unsigned id,unsigned tag){ put(b,3,id); put(b,4,tag); ics_body(b); }
static void cpe(Bits *b,unsigned tag){ put(b,3,1); put(b,4,tag); put(b,1,0); ics_body(b); ics_body(b); }
static void end(Bits *b){ put(b,3,7); while(b->bits%8)put(b,1,0); }
/* PCE: counts and CPE masks of the front / side / back lists, tags = list index; lfe elements with tags 0.., cc coupling entries */
static void pce_bits(Bits *b,int with_id,unsigned nf,unsigned cf,unsigned ns,unsigned cs,unsigned nb,unsigned cb,unsigned nl,unsigned ncc){
    if(with_id)put(b,3,5);
    put(b,4,0); put(b,2,1); put(b,4,4); put(b,4,nf); put(b,4,ns); put(b,4,nb); put(b,2,nl); put(b,3,0); put(b,4,ncc);
    put(b,1,0); put(b,1,0); put(b,1,0);
    { unsigned tc[2]={0,0};                                                         /* tags: running counters per element type, unique across the lists */
      for(unsigned i=0;i<nf;i++){const unsigned c=(cf>>i)&1u;put(b,1,c);put(b,4,tc[c]++&15u);}
      for(unsigned i=0;i<ns;i++){const unsigned c=(cs>>i)&1u;put(b,1,c);put(b,4,tc[c]++&15u);}
      for(unsigned i=0;i<nb;i++){const unsigned c=(cb>>i)&1u;put(b,1,c);put(b,4,tc[c]++&15u);} }
    for(unsigned i=0;i<nl;i++)put(b,4,i);
    for(unsigned i=0;i<ncc;i++){put(b,1,0);put(b,4,i);}
    while(b->bits%8)put(b,1,0);
    put(b,8,2); put(b,16,0x4142u);                                              /* comment */
}

/* ---- non-silent frames: one scalefactor band of four lines, codebook 11 ---------------------------------------------------------------------------------------------------------------------- */
static void huff(Bits *b,unsigned book,unsigned symbol){ uint32_t code; uint8_t bits; assert(sauti_aac_standard_codeword(book,symbol,&code,&bits)); put(b,bits,code); }
static void ics_spec(Bits *b,unsigned gain,const int q[4]){                      /* global_gain, ics_info (max_sfb 1), section (cb 11, 1 band), first scalefactor, no tools, 4 lines */
    put(b,8,gain); put(b,1,0); put(b,2,0); put(b,1,0); put(b,6,1); put(b,1,0);
    put(b,4,11); put(b,5,1);
    huff(b,0,60);
    put(b,1,0); put(b,1,0); put(b,1,0);
    for(unsigned i=0;i<4;i+=2){
        const unsigned a=(unsigned)abs(q[i]),c=(unsigned)abs(q[i+1]);
        huff(b,11,a*17u+c);
        if(a)put(b,1,q[i]<0);
        if(c)put(b,1,q[i+1]<0);
    }
}
static void sce_spec(Bits *b,unsigned tag,unsigned gain,const int q[4]){ put(b,3,0); put(b,4,tag); ics_spec(b,gain,q); }
static int parse(const Bits *b,SautiAacLcwLayout *l,size_t *end){
    size_t pos=0; const int r=sauti_aac_lcw_pce_parse(b->b,(b->bits+7)/8,&pos,l); if(end)*end=pos; return r;
}
static SautiAacLcw *make(const SautiAacLcwLayout *l,unsigned main,unsigned f960){
    SautiAacLcwConfig c; memset(&c,0,sizeof c); c.rate_index=4; c.layout=*l; c.main=main; c.frame960=f960;
    return sauti_aac_lcw_new(&c);
}
static int run(SautiAacLcw *s,const Bits *src,float *pcm,SautiAacReason *rs){ Bits b=*src; end(&b); return sauti_aac_lcw_decode(s,b.b,b.bits/8,pcm,rs); }

int main(void){
    SautiAacLcwLayout l; size_t e; Bits p;
    /* PCE parser: standard 5.1 (C, L R, BL BR, LFE) gets the FFmpeg positions FL FR FC LFE BL BR */
    memset(&p,0,sizeof p); pce_bits(&p,0,2,0x2,0,0,1,0x1,1,0);
    assert(parse(&p,&l,&e)==1 && e==p.bits && l.channels==6 && l.n_elements==4 && l.n_cce==0);
    assert(l.elem_type[0]==0 && l.elem_type[1]==1 && l.elem_type[2]==1 && l.elem_type[3]==3 && l.elem_slot[0]==0 && l.elem_slot[1]==1 && l.elem_slot[2]==3 && l.elem_slot[3]==5);
    assert(l.map[0]==1 && l.map[1]==2 && l.map[2]==0 && l.map[3]==5 && l.map[4]==3 && l.map[5]==4);
    /* cc entries are counted, the comment is skipped */
    memset(&p,0,sizeof p); pce_bits(&p,0,1,0x0,0,0,0,0,0,2);
    assert(parse(&p,&l,&e)==1 && e==p.bits && l.channels==1 && l.n_cce==2);
    /* three front pairs: no standard position for the third -> the order of the PCE list */
    memset(&p,0,sizeof p); pce_bits(&p,0,3,0x7,0,0,0,0,0,0);
    assert(parse(&p,&l,&e)==1 && l.channels==6); for(unsigned i=0;i<6;i++)assert(l.map[i]==i);
    /* 48 channels: 15 front pairs, 9 side pairs; more than 64 channels, or a truncated PCE, is refused / malformed */
    memset(&p,0,sizeof p); pce_bits(&p,0,12,0xe00,9,0x1ff,11,0xf,0,0);
    assert(parse(&p,&l,&e)==1 && l.channels==48 && l.n_elements==32);
    memset(&p,0,sizeof p); pce_bits(&p,0,15,0x7fff,15,0x7fff,15,0x7fff,0,0);          /* 90 channels */
    assert(parse(&p,&l,&e)==-1);
    memset(&p,0,sizeof p); pce_bits(&p,0,2,0x2,0,0,1,0x1,1,0);
    { size_t pos=0; assert(sauti_aac_lcw_pce_parse(p.b,5,&pos,&l)==0); }
    assert(sauti_aac_lcw_pce_parse(NULL,0,&e,&l)==0);
    /* two SCEs (front and back) get tags 0 and 1; a repeated (type, tag) would be told apart by the stream order only (n_elements 0, below) */
    memset(&p,0,sizeof p); pce_bits(&p,0,1,0x0,0,0,1,0x0,0,0); 
    assert(parse(&p,&l,&e)==1 && l.channels==2 && l.n_elements==2);

    /* configuration limits */
    { SautiAacLcwConfig c; memset(&c,0,sizeof c); assert(!sauti_aac_lcw_new(NULL) && !sauti_aac_lcw_new(&c)); c.layout.channels=65; assert(!sauti_aac_lcw_new(&c)); c.layout.channels=2; c.rate_index=13; assert(!sauti_aac_lcw_new(&c));
      c.rate_index=12; c.main=1; assert(!sauti_aac_lcw_new(&c)); }

    /* silence in 12 mono elements (tags 0 .. 11), the elements in reverse order in the stream: the slot comes from the tag */
    { Bits q; float *pcm=(float *)malloc(sizeof(float)*1024*12); SautiAacReason rs; SautiAacLcw *s;
      memset(&p,0,sizeof p); pce_bits(&p,0,12,0x0,0,0,0,0,0,0); assert(parse(&p,&l,&e)==1 && l.channels==12 && l.n_elements==12);
      s=make(&l,0,0); assert(s && sauti_aac_lcw_channels(s)==12 && sauti_aac_lcw_frame_samples(s)==1024);
      memset(&q,0,sizeof q); for(int t=11;t>=0;t--)sce(&q,0,(unsigned)t);
      for(unsigned i=0;i<1024*12;i++)pcm[i]=7.0f;
      assert(run(s,&q,pcm,&rs)==1); for(unsigned i=0;i<1024*12;i++)assert(pcm[i]==0.0f);
      /* a missing element, an unknown tag, a repeated element, a trailing element: refused, the state stays usable */
      memset(&q,0,sizeof q); for(unsigned t=0;t<11;t++)sce(&q,0,t); for(unsigned i=0;i<1024*12;i++)pcm[i]=7.0f;
      assert(run(s,&q,pcm,&rs)==0 && pcm[0]==7.0f);
      memset(&q,0,sizeof q); for(unsigned t=0;t<11;t++)sce(&q,0,t); sce(&q,0,14); assert(run(s,&q,pcm,&rs)==0);
      memset(&q,0,sizeof q); for(unsigned t=0;t<11;t++)sce(&q,0,t); sce(&q,0,3); assert(run(s,&q,pcm,&rs)==0);
      memset(&q,0,sizeof q); for(unsigned t=0;t<12;t++)sce(&q,0,t); sce(&q,0,0); assert(run(s,&q,pcm,&rs)==0);
      memset(&q,0,sizeof q); for(unsigned t=0;t<12;t++)sce(&q,0,t); assert(run(s,&q,pcm,&rs)==1);
      /* LFE where an SCE is listed, LTP-style unsupported element ids (SSR / ER are not configurable here) */
      memset(&q,0,sizeof q); for(unsigned t=0;t<11;t++)sce(&q,0,t); sce(&q,3,11); assert(run(s,&q,pcm,&rs)==0);
      sauti_aac_lcw_free(s); free(pcm); }

    /* 48 channels with pairs: decode, and a stream that ends early is malformed */
    { Bits q; float *pcm=(float *)malloc(sizeof(float)*1024*48); SautiAacReason rs; SautiAacLcw *s; unsigned t;
      memset(&p,0,sizeof p); pce_bits(&p,0,12,0xe00,9,0x1ff,11,0xf,0,0); assert(parse(&p,&l,&e)==1 && l.channels==48);
      s=make(&l,0,0); assert(s);
      memset(&q,0,sizeof q); for(t=0;t<9;t++)sce(&q,0,t); for(t=0;t<3;t++)cpe(&q,t); for(t=3;t<12;t++)cpe(&q,t); for(t=12;t<16;t++)cpe(&q,t); for(t=9;t<16;t++)sce(&q,0,t);
      for(unsigned i=0;i<1024u*48u;i++)pcm[i]=7.0f;
      assert(run(s,&q,pcm,&rs)==1);
      for(unsigned i=0;i<1024u*48u;i++)assert(pcm[i]==0.0f);
      memset(&q,0,sizeof q); for(t=0;t<9;t++)sce(&q,0,t); for(t=0;t<3;t++)cpe(&q,t); for(t=3;t<12;t++)cpe(&q,t); for(t=12;t<15;t++)cpe(&q,t); for(t=9;t<16;t++)sce(&q,0,t); assert(run(s,&q,pcm,&rs)==0);
      sauti_aac_lcw_free(s); free(pcm); }

    /* 960 lines per frame: 960 samples per channel; Main is configurable */
    { Bits q; float pcm[2*1024]; SautiAacReason rs; SautiAacLcw *s;
      memset(&p,0,sizeof p); pce_bits(&p,0,1,0x1,0,0,0,0,0,0); assert(parse(&p,&l,&e)==1 && l.channels==2);
      s=make(&l,0,1); assert(s && sauti_aac_lcw_frame_samples(s)==960); memset(&q,0,sizeof q); cpe(&q,0); assert(run(s,&q,pcm,&rs)==1); sauti_aac_lcw_free(s);
      s=make(&l,1,0); assert(s && sauti_aac_lcw_frame_samples(s)==1024); sauti_aac_lcw_free(s); }

    /* coupling channel elements: the syntax of the three coupling points and of the gain lists (silent spectra); a mis-parsed field would shift every later bit and break the trailing check */
    { Bits q; float pcm[2*1024]; SautiAacReason rs; SautiAacLcw *s;
      memset(&p,0,sizeof p); pce_bits(&p,0,1,0x1,0,0,0,0,0,1); assert(parse(&p,&l,&e)==1 && l.channels==2 && l.n_cce==1);
      s=make(&l,0,0); assert(s);
      for(unsigned point=0;point<3;point++){
          memset(&q,0,sizeof q);
          cpe(&q,0);
          put(&q,3,2); put(&q,4,0);                                                 /* CCE, tag 0 */
          put(&q,1,point==2?1u:0u);                                                 /* ind_sw_cce_flag: independent coupling */
          put(&q,3,1);                                                              /* two coupled elements -> gain lists */
          put(&q,1,1); put(&q,4,0); put(&q,2,3);                                    /* target CPE 0, both channels: two lists */
          put(&q,1,0); put(&q,4,1);                                                 /* target SCE 1: one list */
          put(&q,1,point==1?1u:0u);                                                 /* cc_domain */
          put(&q,1,1); put(&q,2,2);                                                 /* gain_element_sign, gain_element_scale */
          ics_body(&q);                                                             /* the coupling channel itself */
          for(unsigned list=1;list<3;list++){                                       /* list 0 has no syntax; the others: common_gain_element_present (not independent) + hcod_sf delta 0 = '0' */
              if(point!=2)put(&q,1,1);
              put(&q,1,0);
          }
          for(unsigned i=0;i<2*1024;i++)pcm[i]=7.0f;
          { Bits r=q; end(&r); const int st=sauti_aac_lcw_decode(s,r.b,r.bits/8,pcm,&rs); assert(st==1); }
      }
      /* coupling with a target that is not in the frame is skipped, one with a damaged gain list is malformed */
      memset(&q,0,sizeof q); cpe(&q,0); put(&q,3,2); put(&q,4,0); put(&q,1,1); put(&q,3,0); put(&q,1,0); put(&q,4,9); put(&q,1,0); put(&q,1,0); put(&q,2,0); ics_body(&q);
      for(unsigned i=0;i<2*1024;i++)pcm[i]=7.0f;
      assert(run(s,&q,pcm,&rs)==1); for(unsigned i=0;i<2*1024;i++)assert(pcm[i]==0.0f);
      memset(&q,0,sizeof q); cpe(&q,0); put(&q,3,2); put(&q,4,0); put(&q,1,1); put(&q,3,0); put(&q,1,0); put(&q,4,9);       /* truncated in the middle of the CCE */
      assert(run(s,&q,pcm,&rs)==0);
      sauti_aac_lcw_free(s); }


    /* the coupling arithmetic: a coupled element equals an element that carries the scaled spectrum itself.  Two SCE targets that are silent, one coupling channel (q = 3 -1 2 0, global_gain 110); the first
     * target takes the coupling channel with gain 1, the second with gain 2 (gain element -1, scale 2.0), which is the same as global_gain + 4.  All three coupling points must give the same PCM. */
    { static const int cq[4]={3,-1,2,0}; float a[2*1024],b[2*1024]; SautiAacReason rs; SautiAacLcw *sa,*sb; Bits q;
      memset(&p,0,sizeof p); pce_bits(&p,0,2,0x0,0,0,0,0,0,1); assert(parse(&p,&l,&e)==1 && l.channels==2);
      for(unsigned point=0;point<3;point++){
          sa=make(&l,0,0); sb=make(&l,0,0); assert(sa&&sb);
          memset(&q,0,sizeof q); sce(&q,0,0); sce(&q,0,1);
          put(&q,3,2); put(&q,4,0); put(&q,1,point==2?1u:0u); put(&q,3,1);
          put(&q,1,0); put(&q,4,0); put(&q,1,0); put(&q,4,1);
          put(&q,1,point==1?1u:0u); put(&q,1,0); put(&q,2,3);                       /* cc_domain, sign 0, scale 2.0 */
          ics_spec(&q,110,cq);
          if(point!=2)put(&q,1,1);                                                  /* common_gain_element_present */
          huff(&q,0,59);                                                            /* gain element -1: 2.0 ^ 1 */
          assert(run(sa,&q,a,&rs)==1);
          memset(&q,0,sizeof q); sce_spec(&q,0,110,cq); sce_spec(&q,1,114,cq);
          assert(run(sb,&q,b,&rs)==1);
          double peak=0; for(unsigned i=0;i<2*1024;i++){ if(fabs(b[i])>peak)peak=fabs(b[i]); }
          assert(peak>0.0);
          for(unsigned i=0;i<2*1024;i++)if(!(fabs((double)a[i]-b[i])<=1e-6*peak)){printf("point %u i %u a %g b %g peak %g\n",point,i,a[i],b[i],peak);assert(0);}
          /* the second frame (overlap-add of the first) agrees as well */
          memset(&q,0,sizeof q); sce(&q,0,0); sce(&q,0,1);
          { Bits r=q;                                                               /* the same coupling channel element with a silent spectrum: an independently coupled channel needs its own overlap */
            put(&r,3,2); put(&r,4,0); put(&r,1,point==2?1u:0u); put(&r,3,1);
            put(&r,1,0); put(&r,4,0); put(&r,1,0); put(&r,4,1);
            put(&r,1,point==1?1u:0u); put(&r,1,0); put(&r,2,3);
            ics_body(&r); if(point!=2)put(&r,1,1); huff(&r,0,59);
            assert(run(sa,&r,a,&rs)==1 && run(sb,&q,b,&rs)==1); }
          for(unsigned i=0;i<2*1024;i++)assert(fabs((double)a[i]-b[i])<=1e-6*peak);
          sauti_aac_lcw_free(sa); sauti_aac_lcw_free(sb);
      }
    }

    /* gain elements per scalefactor band with gain_element_sign: delta -3 gives -(2.0 ^ 2) = -4, which is the negated spectrum at global_gain + 8 */
    { static const int cq[4]={3,-1,2,0},nq[4]={-3,1,-2,0}; float a[2*1024],b[2*1024]; SautiAacReason rs; SautiAacLcw *sa,*sb; Bits q;
      memset(&p,0,sizeof p); pce_bits(&p,0,2,0x0,0,0,0,0,0,1); assert(parse(&p,&l,&e)==1);
      sa=make(&l,0,0); sb=make(&l,0,0); assert(sa&&sb);
      memset(&q,0,sizeof q); sce(&q,0,0); sce(&q,0,1);
      put(&q,3,2); put(&q,4,0); put(&q,1,0); put(&q,3,1);
      put(&q,1,0); put(&q,4,0); put(&q,1,0); put(&q,4,1);
      put(&q,1,0); put(&q,1,1); put(&q,2,3);                                        /* cc_domain 0, gain_element_sign 1, scale 2.0 */
      ics_spec(&q,110,cq);
      put(&q,1,0);                                                                  /* common_gain_element_present = 0: one gain element for the band */
      huff(&q,0,57);
      assert(run(sa,&q,a,&rs)==1);
      memset(&q,0,sizeof q); sce_spec(&q,0,110,cq); sce_spec(&q,1,118,nq);
      assert(run(sb,&q,b,&rs)==1);
      { double peak=0; for(unsigned i=0;i<2*1024;i++){ if(fabs(b[i])>peak)peak=fabs(b[i]); }
        assert(peak>0.0); for(unsigned i=0;i<2*1024;i++)assert(fabs((double)a[i]-b[i])<=1e-6*peak); }
      sauti_aac_lcw_free(sa); sauti_aac_lcw_free(sb);
    }

    /* state copies are independent (checkpoints) */
    { Bits q; float pcm[2*1024]; SautiAacReason rs; SautiAacLcw *s,*c;
      memset(&p,0,sizeof p); pce_bits(&p,0,1,0x1,0,0,0,0,0,0); assert(parse(&p,&l,&e)==1);
      s=make(&l,0,0); assert(s); c=sauti_aac_lcw_dup(s); assert(c && c!=s && sauti_aac_lcw_channels(c)==2 && !sauti_aac_lcw_dup(NULL));
      memset(&q,0,sizeof q); cpe(&q,0); assert(run(s,&q,pcm,&rs)==1 && run(c,&q,pcm,&rs)==1);
      sauti_aac_lcw_free(s); sauti_aac_lcw_free(c); sauti_aac_lcw_free(NULL); }

    puts("PASS AAC lcw: PCE (positions, other layouts, limits), slots by (type, tag), 12 and 48 channels, 960, CCE syntax, refusals, copies");
    return 0;
}
