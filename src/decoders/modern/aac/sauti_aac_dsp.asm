; AAC windowed overlap-add, NASM x86-64 SysV ABI / SSE scalar.
; This is a DSP building block, not a complete IMDCT or AAC decoder.
; int f(const float* imdct, const float* win, float* history,
;       float* out, size_t N); N=128 or 1024.
; Inputs and output arrays must not overlap; history updated in place.
default rel
global sauti_aac_window_overlap_asm
section .text
sauti_aac_window_overlap_asm:
    xor eax,eax
    test rdi,rdi
    jz .invalid
    test rsi,rsi
    jz .invalid
    test rdx,rdx
    jz .invalid
    test rcx,rcx
    jz .invalid
    cmp r8,128
    je .valid
    cmp r8,1024
    jne .invalid
.valid:
    xor r9d,r9d
.loop:
    movss xmm0,[rdi+r9*4]          ; imdct[i]
    mulss xmm0,[rsi+r9*4]          ; window[i]
    addss xmm0,[rdx+r9*4]          ; history[i]
    movss [rcx+r9*4],xmm0
    lea r10,[r9+r8]
    movss xmm1,[rdi+r10*4]         ; imdct[N+i]
    mulss xmm1,[rsi+r10*4]         ; window[N+i]
    movss [rdx+r9*4],xmm1          ; next history[i]
    inc r9
    cmp r9,r8
    jb .loop
    mov eax,1
.invalid:
    ret
