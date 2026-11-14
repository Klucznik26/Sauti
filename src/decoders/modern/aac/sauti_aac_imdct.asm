; AAC IMDCT row dot-product in native NASM SSE2 / SysV AMD64.
; double f(const float *spectral=rdi, const float *cosine_basis=rsi,
;          size_t n=rdx); return result in xmm0.
; Conversion to double before sum reduces cancellation error.
default rel
global sauti_aac_imdct_dot_asm
section .text
sauti_aac_imdct_dot_asm:
    xorpd xmm0,xmm0
    test rdi,rdi
    jz .done
    test rsi,rsi
    jz .done
    test rdx,rdx
    jz .done
    xor rcx,rcx
.loop:
    cvtss2sd xmm1,[rdi+rcx*4]
    cvtss2sd xmm2,[rsi+rcx*4]
    mulsd xmm1,xmm2
    addsd xmm0,xmm1
    inc rcx
    cmp rcx,rdx
    jb .loop
.done:
    ret
