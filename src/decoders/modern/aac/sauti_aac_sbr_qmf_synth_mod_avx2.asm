; SBR-74: AVX2 64-band complex QMF modulation, System V x86-64.
; void sauti_aac_sbr_qmf_synth_mod_slot_avx2(
;   float *out128 (rdi), const SautiAacSbrComplex *bands64 (rsi));
; Reads 64 interleaved (real,imag) floats, computes 64 mirrored pairs.
; The SSE2 table symbols are shared: no duplicate 32-KiB constants.
; Deliberately no FMA: independent multiply/add rounding.  Bit-identical to the SSE2 kernel (same summation tree).
default rel
global sauti_aac_sbr_qmf_synth_mod_slot_avx2
extern sauti_aac_sbr_qmf_synth_cos_table
extern sauti_aac_sbr_qmf_synth_sin_table
section .text

sauti_aac_sbr_qmf_synth_mod_slot_avx2:
    push rbp
    mov rbp, rsp
    and rsp, -32
    sub rsp, 512                    ; real[64] + imag[64], 32-byte aligned

    xor eax, eax
.deinterleave:
    vmovups xmm0, [rsi + rax*8]
    vmovups xmm1, [rsi + rax*8 + 16]
    vmovups xmm2, [rsi + rax*8 + 32]
    vmovups xmm3, [rsi + rax*8 + 48]
    vshufps xmm4, xmm0, xmm1, 0x88
    vshufps xmm5, xmm2, xmm3, 0x88
    vinsertf128 ymm4, ymm4, xmm5, 1
    vmovaps [rsp + rax*4], ymm4
    vshufps xmm4, xmm0, xmm1, 0xdd
    vshufps xmm5, xmm2, xmm3, 0xdd
    vinsertf128 ymm4, ymm4, xmm5, 1
    vmovaps [rsp + 256 + rax*4], ymm4
    add rax, 8
    cmp rax, 64
    jb .deinterleave

    lea rdx, [rel sauti_aac_sbr_qmf_synth_cos_table]
    lea rcx, [rel sauti_aac_sbr_qmf_synth_sin_table]
    xor r8d, r8d
.row:
    mov r10, r8
    shl r10, 8                     ; 64 floats per row
    mov r11, r10
    add r10, rdx
    add r11, rcx
    ; Bit-identical to the SSE2 kernel: SSE2 keeps four 4-lane accumulators acc0..acc3 (lane j of acc a sums
    ; k = 16*m + 4*a + j, m = 0..3 in order).  Here ymm0 = [acc0|acc1] and ymm1 = [acc2|acc3] for the real part,
    ; ymm2/ymm3 for the imaginary part: same per-lane summation order, so the products add in exactly the same
    ; sequence, and the reduction below repeats the SSE2 tree.  (An 8-lane layout would reorder the sums and
    ; change the low bits, which made the decoded PCM depend on which core type ran the slot.)
    vxorps ymm0, ymm0, ymm0
    vxorps ymm1, ymm1, ymm1
    vxorps ymm2, ymm2, ymm2
    vxorps ymm3, ymm3, ymm3
    xor r9d, r9d
.band:
    vmovaps ymm8, [rsp + r9]               ; k = 16m .. 16m+7        -> [acc0|acc1]
    vmulps ymm8, ymm8, [r10 + r9]
    vaddps ymm0, ymm0, ymm8
    vmovaps ymm9, [rsp + r9 + 32]          ; k = 16m+8 .. 16m+15     -> [acc2|acc3]
    vmulps ymm9, ymm9, [r10 + r9 + 32]
    vaddps ymm1, ymm1, ymm9

    vmovaps ymm10, [rsp + 256 + r9]
    vmulps ymm10, ymm10, [r11 + r9]
    vaddps ymm2, ymm2, ymm10
    vmovaps ymm11, [rsp + 256 + r9 + 32]
    vmulps ymm11, ymm11, [r11 + r9 + 32]
    vaddps ymm3, ymm3, ymm11
    add r9, 64
    cmp r9, 256
    jb .band

    ; Reduction identical to SSE2: (acc0+acc1)+(acc2+acc3), then lanes 2,3 into 0,1, then lane 1 into lane 0.
    vextractf128 xmm8, ymm0, 1
    vaddps xmm0, xmm0, xmm8                ; acc0 + acc1
    vextractf128 xmm9, ymm1, 1
    vaddps xmm1, xmm1, xmm9                ; acc2 + acc3
    vaddps xmm0, xmm0, xmm1
    vmovhlps xmm8, xmm8, xmm0
    vaddps xmm0, xmm0, xmm8
    vshufps xmm8, xmm0, xmm0, 1
    vaddss xmm0, xmm0, xmm8                ; R

    vextractf128 xmm8, ymm2, 1
    vaddps xmm4, xmm2, xmm8
    vextractf128 xmm9, ymm3, 1
    vaddps xmm5, xmm3, xmm9
    vaddps xmm4, xmm4, xmm5
    vmovhlps xmm8, xmm8, xmm4
    vaddps xmm4, xmm4, xmm8
    vshufps xmm8, xmm4, xmm4, 1
    vaddss xmm4, xmm4, xmm8                ; I

    vaddss xmm1, xmm0, xmm4       ; V[n] = R + I
    vmovss [rdi + r8*4], xmm1
    vsubss xmm2, xmm4, xmm0       ; V[127-n] = I - R
    mov eax, 127
    sub eax, r8d
    vmovss [rdi + rax*4], xmm2
    inc r8
    cmp r8, 64
    jb .row

    vzeroupper
    mov rsp, rbp
    pop rbp
    ret
