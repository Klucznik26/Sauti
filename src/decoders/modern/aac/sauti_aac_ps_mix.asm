; Hot real PS matrix kernel, x86-64 SysV ABI / SSE.
; void f(const complex *direct, const complex *decorrelated,
;        complex *left, complex *right, const coeff *h,
;        size_t samples, size_t stride_bytes);
; The seventh argument is at [rsp+8].  Each complex is { float re, im }.
default rel
global sauti_aac_ps_mix_strided_asm
section .text
sauti_aac_ps_mix_strided_asm:
    test r9,r9
    jz .done
    movss xmm0,[r8]       ; h11
    movss xmm1,[r8+4]     ; h12
    movss xmm2,[r8+8]     ; h21
    movss xmm3,[r8+12]    ; h22
    shufps xmm0,xmm0,0
    shufps xmm1,xmm1,0
    shufps xmm2,xmm2,0
    shufps xmm3,xmm3,0
.loop:
    movq xmm4,[rdi]
    movq xmm5,[rsi]
    movaps xmm6,xmm4
    mulps xmm6,xmm0
    movaps xmm7,xmm5
    mulps xmm7,xmm2
    addps xmm6,xmm7
    movq [rdx],xmm6
    movaps xmm6,xmm4
    mulps xmm6,xmm1
    movaps xmm7,xmm5
    mulps xmm7,xmm3
    addps xmm6,xmm7
    movq [rcx],xmm6
    add rdi,[rsp+8]
    add rsi,[rsp+8]
    add rdx,[rsp+8]
    add rcx,[rsp+8]
    dec r9
    jnz .loop
.done:
    ret

; Identical argument layout, but h points to four { re, im } coefficients.
; The macro uses only caller-saved XMM registers and works on one strided
; hybrid subband over a run of QMF slots.
%macro CMUL 5
    movq %1,[%4]
    movaps %2,%1
    shufps %2,%2,0xB1
    movq %3,[r8+%5]
    movaps xmm12,%3
    shufps xmm12,xmm12,0
    shufps %3,%3,0x55
    mulps %1,xmm12
    mulps %2,%3
    movaps %3,%1
    subss %1,%2
    shufps %3,%3,0x55
    shufps %2,%2,0x55
    addss %3,%2
    unpcklps %1,%3
%endmacro

global sauti_aac_ps_mix_complex_strided_asm
sauti_aac_ps_mix_complex_strided_asm:
    test r9,r9
    jz .complex_done
.complex_loop:
    ; left = h11 * direct + h21 * decorrelated
    CMUL xmm4,xmm5,xmm6,rdi,0
    CMUL xmm7,xmm8,xmm9,rsi,16
    addps xmm4,xmm7
    movq [rdx],xmm4
    ; right = h12 * direct + h22 * decorrelated
    CMUL xmm4,xmm5,xmm6,rdi,8
    CMUL xmm7,xmm8,xmm9,rsi,24
    addps xmm4,xmm7
    movq [rcx],xmm4
    add rdi,[rsp+8]
    add rsi,[rsp+8]
    add rdx,[rsp+8]
    add rcx,[rsp+8]
    dec r9
    jnz .complex_loop
.complex_done:
    ret
