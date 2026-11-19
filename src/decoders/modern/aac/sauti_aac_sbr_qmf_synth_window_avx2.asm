; SBR-74: AVX2 polyphase synthesis, System V x86-64 only.
; void sauti_aac_sbr_qmf_synth_window_slot_avx2(
;   float *out64 (rdi), const float *coeff640 (rsi),
;   const float * const hist[10] (rdx));
; The caller checks all lengths, source samples and rollback invariants.
; No FMA: preserve separate multiply and add rounding of the SSE2 path.
default rel
global sauti_aac_sbr_qmf_synth_window_slot_avx2
section .text

sauti_aac_sbr_qmf_synth_window_slot_avx2:
    xor ecx, ecx                       ; 32 floats per chunk, 128 bytes
.chunk:
    vxorps ymm0, ymm0, ymm0
    vxorps ymm1, ymm1, ymm1
    vxorps ymm2, ymm2, ymm2
    vxorps ymm3, ymm3, ymm3
    xor r8d, r8d
.tap:
    mov rax, [rdx + r8*8]              ; history[j]
    mov r9, r8
    shl r9, 8                          ; 64 floats per coefficient row
    add r9, rsi

    vmovups ymm4, [rax + rcx]
    vmulps ymm4, ymm4, [r9 + rcx]
    vaddps ymm0, ymm0, ymm4

    vmovups ymm4, [rax + rcx + 32]
    vmulps ymm4, ymm4, [r9 + rcx + 32]
    vaddps ymm1, ymm1, ymm4

    vmovups ymm4, [rax + rcx + 64]
    vmulps ymm4, ymm4, [r9 + rcx + 64]
    vaddps ymm2, ymm2, ymm4

    vmovups ymm4, [rax + rcx + 96]
    vmulps ymm4, ymm4, [r9 + rcx + 96]
    vaddps ymm3, ymm3, ymm4

    inc r8
    cmp r8, 10
    jb .tap

    vmovups [rdi + rcx], ymm0
    vmovups [rdi + rcx + 32], ymm1
    vmovups [rdi + rcx + 64], ymm2
    vmovups [rdi + rcx + 96], ymm3
    add rcx, 128
    cmp rcx, 256
    jb .chunk

    vzeroupper                         ; avoid AVX -> legacy SSE penalty
    ret
