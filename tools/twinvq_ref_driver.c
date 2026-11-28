/* Test-fixture tool (not a product): a driver for the TwinVQ base layer of the ISO/IEC 14496-5 reference software (the "natural audio" Rewrite VM, outside this repository).  It does what the
 * reference decoder's tvqInitDecoder / tvqCoreDec do for raw access units, without the ISO MP4 library: ntt_base_init, the header and bit unpacking, ntt_vq_decoder, the long term predictor,
 * TNS and the filter bank.  It is the oracle of tests/test_aac_twinvq.c and docs/AAC_COMPLETENESS_PLAN.md.  Build it with tools/twinvq_ref_build.sh.
 * usage: twinvq_ref IN.aus RATE CHANNELS BLOCK OUT.spec OUT.pcm   (IN.aus: 4-byte big-endian length + bytes per access unit, as tools/mp4_dump_aus.py and tools/twinvq_gen write;
 * OUT.spec: the spectra after the long term predictor, doubles; OUT.pcm: the output, doubles in 16-bit units, one channel after the other for every frame).  Environment TVQV=1: one line per frame. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "allHandles.h"
#include "block.h"
#include "all.h"
#include "monopredStruct.h"
#include "tf_mainStruct.h"
#include "tns.h"
#include "bitstream.h"
#include "common_m4a.h"
#include "ntt_conf.h"
#include "ntt_scale_conf.h"
#include "nok_lt_prediction.h"
#include "aac.h"
#include "allVariables.h"
#include "obj_descr.h"
#include "tf_main.h"

extern void tvqInitDecoderX(void);
static int st_w[4],st_ltp,st_tns,st_shape,st_bad;
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"), *o = fopen(argv[5], "wb"), *op = fopen(argv[6], "wb"); int sfi_idx = 0;
    int rate = atoi(argv[2]), nch = atoi(argv[3]), bsz = atoi(argv[4]);
    static unsigned char au[1 << 16]; uint32_t len;
    FRAME_DATA fd; TF_DATA tf; ntt_INDEX index; HANDLE_BSBITSTREAM st; HANDLE_BSBITBUFFER bb;
    float brate; int i, frame = 0; TNS_frame_info tns_info[MAX_TIME_CHANNELS];
    static HANDLE_DECODER_GENERAL hFault;
    static double *spec[MAX_TIME_CHANNELS]; static double sbuf[2][1024];
    BsInit(0, 0, 0);
    memset(&fd, 0, sizeof fd); memset(&tf, 0, sizeof tf);
    fd.od = (OBJECT_DESCRIPTOR *)calloc(1, sizeof *fd.od);
    fd.od->ESDescriptor[0] = (ES_DESCRIPTOR *)calloc(1, sizeof *fd.od->ESDescriptor[0]);
    fd.od->streamCount.value = 1;
    hFault = (HANDLE_DECODER_GENERAL)calloc(4, 4096); hFault[0].hVm = CreateBuffer(VIRTUELL);
    for (;;) {
        unsigned char b4[4];
        if (fread(b4, 1, 4, f) != 4) break;
        len = ((uint32_t)b4[0] << 24) | (b4[1] << 16) | (b4[2] << 8) | b4[3];
        if (len > sizeof au || fread(au, 1, len, f) != len) break;
        if (frame == 0) {
            ntt_INDEX idx0, sidx; int ibps_scl[8] = {0};
            brate = (float)len * 8.0f * (float)rate / (float)bsz;
            memset(&index, 0, sizeof index);
            ntt_base_init((float)rate, brate, 0.0f, nch, bsz, &index);
            ntt_scale_init(0, ibps_scl, (float)rate, &index, &sidx);
            index.nttDataScl = sidx.nttDataScl;
            { int k; for (k = 0; k < (1 << LEN_SAMP_IDX); k++) if (samp_rate_info[k].samp_rate == rate) break;
              infoinit(&samp_rate_info[k], bsz);
              winmap[0] = win_seq_info[ONLY_LONG_SEQUENCE]; winmap[1] = win_seq_info[ONLY_LONG_SEQUENCE];
              winmap[2] = win_seq_info[EIGHT_SHORT_SEQUENCE]; winmap[3] = win_seq_info[ONLY_LONG_SEQUENCE]; tf.sfbInfo = winmap; }
            { int k; for (k = 0; k < 13; k++) if (samp_rate_info[k].samp_rate == rate) sfi_idx = k; }
            tf.pred_type = NOK_LTP; tf.block_size_samples = bsz;
            fprintf(stderr, "bitrate %.1f nbits_fr %d\n", brate, index.nttDataBase->ntt_NBITS_FR);
            for (i = 0; i < nch; i++) { tf.spectral_line_vector[0][i] = sbuf[i]; tf.nok_lt_status[i] = (NOK_LT_PRED_STATUS *)calloc(1, sizeof(NOK_LT_PRED_STATUS)); nok_init_lt_pred(tf.nok_lt_status[i]); tf.nok_lt_status[i]->overlap_buffer = (double *)calloc(2048, sizeof(double)); }
        }
        bb = BsAllocBuffer(len * 8 + 64);
        st = BsOpenBufferWrite(bb);
        for (i = 0; i < (int)len; i++) BsPutBit(st, au[i], 8);
        BsClose(st);
        st = BsOpenBufferRead(bb);
        BsManipulateSetNumByte(len, st);
        { unsigned long u; int dec = 0, cbt, avail, tomo;
          BsGetBit(st, &u, 2); cbt = (int)u; tf.windowSequence[0] = cbt == 0 ? ONLY_LONG_SEQUENCE : cbt == 1 ? LONG_START_SEQUENCE : cbt == 2 ? EIGHT_SHORT_SEQUENCE : LONG_STOP_SEQUENCE;
          BsGetBit(st, &u, 1); tf.windowShape[0] = (WINDOW_SHAPE)u; dec = 3; index.w_type = tf.windowSequence[0]; if (nch == 2) { tf.windowSequence[1] = tf.windowSequence[0]; tf.windowShape[1] = tf.windowShape[0]; }
          index.ms_mask = 0; index.group_code = 0x7F; index.last_max_sfb[0] = 0; index.er_tvq = 0;
          ntt_headerdec(-1, st, &index, tf.sfbInfo, &dec, tns_info, tf.nok_lt_status, tf.pred_type, hFault);
          tomo = dec;
          dec += ntt_BitUnPack(st, (int)len * 8 - tomo, tomo, &fd, tf.windowSequence[0], &index);
          ntt_vq_decoder(&index, tf.spectral_line_vector[0], tf.sfbInfo);
          { int tn=0,k; for(k=0;k<tns_info[0].n_subblocks;k++) if(tns_info[0].info[k].n_filt) tn++; if(getenv("TVQV"))fprintf(stderr,"F %d ws %d ltp %d tns %d\n",frame,cbt,tf.nok_lt_status[0]->sbk_prediction_used[0]!=0,tn>0); st_w[cbt]++; st_ltp+=tf.nok_lt_status[0]->sbk_prediction_used[0]!=0; st_tns+=tn>0; st_shape+=tf.windowShape[0]; st_bad+= (dec!=(int)len*8); }
        }
        { /* LTP + TNS + filterbank as tvqCoreDec / tvqAUDecode do */
          Info *info = tf.sfbInfo[tf.windowSequence[0]]; int i_ch, itmp, j, k; static double cur[1024], tmpb[1024], tsig[1024]; static double pcm[1024];
          for (i_ch = 0; i_ch < nch; i_ch++) {
            for (itmp = 0; itmp < info->bins_per_bk; itmp++) cur[itmp] = tf.spectral_line_vector[0][i_ch][itmp];
            nok_lt_predict(info, tf.windowSequence[0], tf.windowShape[i_ch], tf.prev_windowShape[i_ch],
                           tf.nok_lt_status[i_ch]->sbk_prediction_used, tf.nok_lt_status[i_ch]->sfb_prediction_used,
                           tf.nok_lt_status[i_ch], tf.nok_lt_status[i_ch]->weight, tf.nok_lt_status[i_ch]->delay,
                           cur, info->bins_per_bk, info->bins_per_bk / 8, sfi_idx, &(tns_info[i_ch]), NTT_VQ);
            for (i = 0; i < info->bins_per_bk; i++) tf.spectral_line_vector[0][i_ch][i] = cur[i];
            for (i = j = 0; i < tns_info[i_ch].n_subblocks; i++) {
              tns_decode_subblock(cur + j, index.max_sfb[0], info->sbk_sfb_top[i], info->islong, &(tns_info[i_ch].info[i]), NTT_VQ, sfi_idx);
              j += info->bins_per_sbk[i];
            }
            for (i = 0; i < info->bins_per_bk; i++) tmpb[i] = cur[i];
            freq2buffer(tmpb, tsig, tf.nok_lt_status[i_ch]->overlap_buffer, tf.windowSequence[0], info->bins_per_bk, info->bins_per_bk / 8,
                        tf.windowShape[0], tf.prev_windowShape[i_ch], OVERLAPPED_MODE, 8);
            nok_lt_update(tf.nok_lt_status[i_ch], tsig, tf.nok_lt_status[i_ch]->overlap_buffer, info->bins_per_bk);
            fwrite(tsig, sizeof(double), bsz, op);
            tf.prev_windowShape[i_ch] = tf.windowShape[i_ch];
          }
        }
        for (i = 0; i < nch; i++) fwrite(sbuf[i], sizeof(double), bsz, o);
        BsFreeBuffer(bb);
        frame++;
    }
    fprintf(stderr,"frames %d wseq[%d %d %d %d] ltp %d tns %d shape1 %d bitmismatch %d\n",frame,st_w[0],st_w[1],st_w[2],st_w[3],st_ltp,st_tns,st_shape,st_bad);
    fclose(o);
    return 0;
}
