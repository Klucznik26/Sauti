; Experimental SBR-74 AVX2 window: fully unroll the ten-tap inner loop.
; System V x86-64: output64=rdi, coeff640=rsi, history_ptrs[10]=rdx.
; Exact arithmetic order matches both the existing SSE2 and AVX2 kernels.
; No FMA, no hidden state and no changes to caller validation/rollback.
default rel
global sauti_aac_sbr_qmf_synth_window_slot_avx2_unrolled
section .text

sauti_aac_sbr_qmf_synth_window_slot_avx2_unrolled:
    xor ecx, ecx                            ; two 32-float chunks
.chunk:
    vxorps ymm0, ymm0, ymm0
    vxorps ymm1, ymm1, ymm1
    vxorps ymm2, ymm2, ymm2
    vxorps ymm3, ymm3, ymm3

%assign tap 0
%rep 10
    mov rax, [rdx + tap*8]
    vmovups ymm4, [rax + rcx]
    vmulps ymm4, ymm4, [rsi + rcx + tap*256]
    vaddps ymm0, ymm0, ymm4

    vmovups ymm4, [rax + rcx + 32]
    vmulps ymm4, ymm4, [rsi + rcx + tap*256 + 32]
    vaddps ymm1, ymm1, ymm4

    vmovups ymm4, [rax + rcx + 64]
    vmulps ymm4, ymm4, [rsi + rcx + tap*256 + 64]
    vaddps ymm2, ymm2, ymm4

    vmovups ymm4, [rax + rcx + 96]
    vmulps ymm4, ymm4, [rsi + rcx + tap*256 + 96]
    vaddps ymm3, ymm3, ymm4
%assign tap tap + 1
%endrep

    vmovups [rdi + rcx], ymm0
    vmovups [rdi + rcx + 32], ymm1
    vmovups [rdi + rcx + 64], ymm2
    vmovups [rdi + rcx + 96], ymm3
    add rcx, 128
    cmp rcx, 256
    jb .chunk
    vzeroupper
    ret
