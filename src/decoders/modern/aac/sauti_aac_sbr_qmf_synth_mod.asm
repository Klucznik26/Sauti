; HE-AAC SBR-26 / SBR-64: Native x86-64 NASM SSE2 complex QMF synthesis modulation kernel.
; void sauti_aac_sbr_qmf_synth_mod_slot_asm(
;     float *out128,                        ; rdi
;     const SautiAacSbrComplex *bands64);   ; rsi

default rel
global sauti_aac_sbr_qmf_synth_mod_slot_asm
section .text

sauti_aac_sbr_qmf_synth_mod_slot_asm:
    sub rsp, 520                ; 256 bytes for re, 256 bytes for im, 8 bytes padding

    ; 1. Deinterleave 64 complex numbers into re[64] and im[64]
    xor rax, rax
.deinterleave_loop:
    movups xmm0, [rsi + rax*8]          ; r0, i0, r1, i1
    movups xmm1, [rsi + rax*8 + 16]     ; r2, i2, r3, i3

    movaps xmm2, xmm0
    shufps xmm2, xmm1, 0x88             ; r0, r1, r2, r3
    movaps xmm3, xmm0
    shufps xmm3, xmm1, 0xDD             ; i0, i1, i2, i3

    movaps [rsp + rax*4], xmm2          ; store re
    movaps [rsp + 256 + rax*4], xmm3    ; store im

    add rax, 4
    cmp rax, 64
    jl .deinterleave_loop

    lea rdx, [sauti_aac_sbr_qmf_synth_cos_table]
    lea rcx, [sauti_aac_sbr_qmf_synth_sin_table]

    ; 2. Compute 64 rows: n = 0..63
    xor r8, r8                         ; n = 0
.row_loop:
    mov r10, r8
    shl r10, 8                         ; r10 = n * 256 (64 floats * 4 bytes)
    mov r11, r10
    add r10, rdx                       ; cos_row = cos_table + n*64
    add r11, rcx                       ; sin_row = sin_table + n*64

    xorps xmm0, xmm0                    ; acc_r0
    xorps xmm1, xmm1                    ; acc_r1
    xorps xmm2, xmm2                    ; acc_r2
    xorps xmm3, xmm3                    ; acc_r3

    xorps xmm4, xmm4                    ; acc_i0
    xorps xmm5, xmm5                    ; acc_i1
    xorps xmm6, xmm6                    ; acc_i2
    xorps xmm7, xmm7                    ; acc_i3

    xor rax, rax                        ; k = 0
.k_loop:
    ; Real accumulation
    movaps xmm8, [rsp + rax]
    mulps xmm8, [r10 + rax]
    addps xmm0, xmm8

    movaps xmm10, [rsp + rax + 16]
    mulps xmm10, [r10 + rax + 16]
    addps xmm1, xmm10

    movaps xmm12, [rsp + rax + 32]
    mulps xmm12, [r10 + rax + 32]
    addps xmm2, xmm12

    movaps xmm14, [rsp + rax + 48]
    mulps xmm14, [r10 + rax + 48]
    addps xmm3, xmm14

    ; Imag accumulation
    movaps xmm8, [rsp + 256 + rax]
    mulps xmm8, [r11 + rax]
    addps xmm4, xmm8

    movaps xmm10, [rsp + 256 + rax + 16]
    mulps xmm10, [r11 + rax + 16]
    addps xmm5, xmm10

    movaps xmm12, [rsp + 256 + rax + 32]
    mulps xmm12, [r11 + rax + 32]
    addps xmm6, xmm12

    movaps xmm14, [rsp + 256 + rax + 48]
    mulps xmm14, [r11 + rax + 48]
    addps xmm7, xmm14

    add rax, 64
    cmp rax, 256
    jl .k_loop

    ; Reduce R (xmm0..xmm3 -> scalar in xmm0)
    addps xmm0, xmm1
    addps xmm2, xmm3
    addps xmm0, xmm2
    movhlps xmm8, xmm0
    addps xmm0, xmm8
    movaps xmm8, xmm0
    shufps xmm8, xmm8, 1
    addss xmm0, xmm8                   ; xmm0 = R

    ; Reduce I (xmm4..xmm7 -> scalar in xmm4)
    addps xmm4, xmm5
    addps xmm6, xmm7
    addps xmm4, xmm6
    movhlps xmm8, xmm4
    addps xmm4, xmm8
    movaps xmm8, xmm4
    shufps xmm8, xmm8, 1
    addss xmm4, xmm8                   ; xmm4 = I

    ; out[n] = R + I
    movaps xmm1, xmm0
    addss xmm1, xmm4
    movss [rdi + r8*4], xmm1

    ; out[127 - n] = I - R
    movaps xmm2, xmm4
    subss xmm2, xmm0
    mov rax, 127
    sub rax, r8
    movss [rdi + rax*4], xmm2

    inc r8
    cmp r8, 64
    jl .row_loop

    add rsp, 520
    ret

section .rodata
%include "src/decoders/modern/aac/sauti_aac_sbr_qmf_synth_tables.inc"
