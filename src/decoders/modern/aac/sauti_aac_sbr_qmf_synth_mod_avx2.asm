; SBR-74: AVX2 64-band complex QMF modulation, System V x86-64.
; void sauti_aac_sbr_qmf_synth_mod_slot_avx2(
;   float *out128 (rdi), const SautiAacSbrComplex *bands64 (rsi));
; Reads 64 interleaved (real,imag) floats, computes 64 mirrored pairs.
; The SSE2 table symbols are shared: no duplicate 32-KiB constants.
; Deliberately no FMA: independent multiply/add rounding.
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
    vxorps ymm0, ymm0, ymm0
    vxorps ymm1, ymm1, ymm1
    vxorps ymm2, ymm2, ymm2
    vxorps ymm3, ymm3, ymm3
    vxorps ymm4, ymm4, ymm4
    vxorps ymm5, ymm5, ymm5
    vxorps ymm6, ymm6, ymm6
    vxorps ymm7, ymm7, ymm7
    xor r9d, r9d
.band:
    vmovaps ymm8, [rsp + r9]
    vmulps ymm8, ymm8, [r10 + r9]
    vaddps ymm0, ymm0, ymm8
    vmovaps ymm8, [rsp + r9 + 32]
    vmulps ymm8, ymm8, [r10 + r9 + 32]
    vaddps ymm1, ymm1, ymm8
    vmovaps ymm8, [rsp + r9 + 64]
    vmulps ymm8, ymm8, [r10 + r9 + 64]
    vaddps ymm2, ymm2, ymm8
    vmovaps ymm8, [rsp + r9 + 96]
    vmulps ymm8, ymm8, [r10 + r9 + 96]
    vaddps ymm3, ymm3, ymm8

    vmovaps ymm8, [rsp + 256 + r9]
    vmulps ymm8, ymm8, [r11 + r9]
    vaddps ymm4, ymm4, ymm8
    vmovaps ymm8, [rsp + 256 + r9 + 32]
    vmulps ymm8, ymm8, [r11 + r9 + 32]
    vaddps ymm5, ymm5, ymm8
    vmovaps ymm8, [rsp + 256 + r9 + 64]
    vmulps ymm8, ymm8, [r11 + r9 + 64]
    vaddps ymm6, ymm6, ymm8
    vmovaps ymm8, [rsp + 256 + r9 + 96]
    vmulps ymm8, ymm8, [r11 + r9 + 96]
    vaddps ymm7, ymm7, ymm8
    add r9, 128
    cmp r9, 256
    jb .band

    ; Horizontal reduction of 64 real and 64 imaginary products.
    vaddps ymm0, ymm0, ymm1
    vaddps ymm2, ymm2, ymm3
    vaddps ymm0, ymm0, ymm2
    vextractf128 xmm8, ymm0, 1
    vaddps xmm0, xmm0, xmm8
    vhaddps xmm0, xmm0, xmm0
    vhaddps xmm0, xmm0, xmm0

    vaddps ymm4, ymm4, ymm5
    vaddps ymm6, ymm6, ymm7
    vaddps ymm4, ymm4, ymm6
    vextractf128 xmm8, ymm4, 1
    vaddps xmm4, xmm4, xmm8
    vhaddps xmm4, xmm4, xmm4
    vhaddps xmm4, xmm4, xmm4

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
