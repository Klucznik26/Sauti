; HE-AAC SBR-25 / SBR-64: Native x86-64 NASM SSE2 polyphase synthesis window kernel.
; void sauti_aac_sbr_qmf_synth_window_slot_asm(
;     float *output64,                          ; rdi
;     const float *qmf_coeff640,                ; rsi
;     const float * const history_ptrs[10]);    ; rdx
;
; Synthesizes 64 outputs in four 16-sample blocks:
; For each block c in 0..3:
;   acc0..acc3 = sum_{j=0..9} coeff[j*64 + c*16 + offset] * history[j][c*16 + offset]
; Store acc0..acc3 to output64[c*16].

default rel
global sauti_aac_sbr_qmf_synth_window_slot_asm
section .text

sauti_aac_sbr_qmf_synth_window_slot_asm:
    xor rcx, rcx                                ; chunk offset in bytes (0, 64, 128, 192)

.chunk_loop:
    xorps xmm0, xmm0                            ; acc0 (floats 0..3)
    xorps xmm1, xmm1                            ; acc1 (floats 4..7)
    xorps xmm2, xmm2                            ; acc2 (floats 8..11)
    xorps xmm3, xmm3                            ; acc3 (floats 12..15)

    xor r8, r8                                  ; j = 0..9
.j_loop:
    mov rax, [rdx + r8*8]                       ; history_ptrs[j]
    mov r9, r8
    shl r9, 8                                   ; j * 256 bytes (64 floats)
    add r9, rsi                                 ; qmf_coeff + j*64

    movups xmm4, [rax + rcx]
    mulps xmm4, [r9 + rcx]
    addps xmm0, xmm4

    movups xmm4, [rax + rcx + 16]
    mulps xmm4, [r9 + rcx + 16]
    addps xmm1, xmm4

    movups xmm4, [rax + rcx + 32]
    mulps xmm4, [r9 + rcx + 32]
    addps xmm2, xmm4

    movups xmm4, [rax + rcx + 48]
    mulps xmm4, [r9 + rcx + 48]
    addps xmm3, xmm4

    inc r8
    cmp r8, 10
    jl .j_loop

    movups [rdi + rcx], xmm0
    movups [rdi + rcx + 16], xmm1
    movups [rdi + rcx + 32], xmm2
    movups [rdi + rcx + 48], xmm3

    add rcx, 64
    cmp rcx, 256
    jl .chunk_loop

    ret
