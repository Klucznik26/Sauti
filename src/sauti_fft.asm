; Sayari Sauti: Windowed radix-2 real-input FFT, x86-64 System V ABI.
; Caller-owned scratch/output arrays; no OS calls, allocation or libc.
; n must be power-of-two within [64,4096].
;
; int sauti_fft_real_f32(const float *input, uint32_t n,
;                        float *real_out, float *imag_out);
; Hann window (periodic), unnormalized forward DFT, negative exponent.
;
; int sauti_fft_magnitude_f32(const float *real, const float *imag,
;                             uint32_t n, float *magnitude);
; Output: n/2+1 bins, Hann coherent-gain corrected single-sided amplitude.
; DC and Nyquist: 2/n; other bins: 4/n. No dB/log compression.
;
; Preconditions: input and output arrays each have n float32 elements,
; magnitude array has n/2+1 floats, arrays do not overlap.

bits 64
default rel

section .rodata align=16
sauti_fft_tau:      dd 0x40c90fdb       ; 2*pi
sauti_fft_neg_tau:  dd 0xc0c90fdb       ; -2*pi
sauti_fft_half:     dd 0x3f000000
sauti_fft_two:      dd 0x40000000
sauti_fft_four:     dd 0x40800000

section .text

global sauti_fft_real_f32
sauti_fft_real_f32:
    test rdi, rdi
    jz .bad
    test rdx, rdx
    jz .bad
    test rcx, rcx
    jz .bad
    cmp esi, 64
    jb .bad
    cmp esi, 4096
    ja .bad
    mov eax, esi
    dec eax
    test eax, esi
    jnz .bad

    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 16
    mov rbx, rdi              ; input
    mov r15d, esi             ; n
    mov r13, rdx              ; real array
    mov r14, rcx              ; imaginary array

    ; Bit-reversal copy with periodic Hann window.
    xor r8d, r8d             ; i
.copy:
    mov eax, r8d
    mov edx, r15d
    shr edx, 1
    xor r9d, r9d             ; reversed index
.reverse:
    shl r9d, 1
    mov r10d, eax
    and r10d, 1
    or r9d, r10d
    shr eax, 1
    shr edx, 1
    jnz .reverse

    ; cos(2*pi*i/n) via x87, calculate Hann = 0.5*(1-cos).
    cvtsi2ss xmm0, r8d
    mulss xmm0, [rel sauti_fft_tau]
    cvtsi2ss xmm1, r15d
    divss xmm0, xmm1
    movss [rsp], xmm0
    fld dword [rsp]
    fcos
    fstp dword [rsp + 4]
    movss xmm0, [rsp + 4]
    movss xmm1, [rel sauti_fft_half]
    mulss xmm0, xmm1
    subss xmm1, xmm0
    mulss xmm1, [rbx + r8 * 4]
    movss [r13 + r9 * 4], xmm1
    mov dword [r14 + r9 * 4], 0
    inc r8d
    cmp r8d, r15d
    jb .copy

    mov r12d, 2              ; butterfly span m
.stage:
    mov r10d, r12d
    shr r10d, 1              ; half
    xor r8d, r8d             ; twiddle j
.j_loop:
    ; wr = cos(-2*pi*j/m), wi = sin(-2*pi*j/m)
    cvtsi2ss xmm0, r8d
    mulss xmm0, [rel sauti_fft_neg_tau]
    cvtsi2ss xmm1, r12d
    divss xmm0, xmm1
    movss [rsp], xmm0
    fld dword [rsp]
    fsincos
    fstp dword [rsp + 4]    ; wr
    fstp dword [rsp + 8]    ; wi

    mov r9d, r8d             ; k=j, j+m, j+2m, ...
.k_loop:
    mov r11d, r9d
    add r11d, r10d           ; b = k + half
    movss xmm0, [rsp + 4]   ; wr
    movss xmm1, [rsp + 8]   ; wi
    movss xmm2, [r13 + r11 * 4]  ; re[b]
    movss xmm3, [r14 + r11 * 4]  ; im[b]

    ; t_real = wr*re[b] - wi*im[b]
    movaps xmm4, xmm0
    mulss xmm4, xmm2
    movaps xmm6, xmm1
    mulss xmm6, xmm3
    subss xmm4, xmm6

    ; t_imag = wr*im[b] + wi*re[b]
    movaps xmm5, xmm0
    mulss xmm5, xmm3
    mulss xmm1, xmm2
    addss xmm5, xmm1

    movss xmm2, [r13 + r9 * 4]
    movss xmm3, [r14 + r9 * 4]
    movaps xmm6, xmm2
    subss xmm6, xmm4
    movss [r13 + r11 * 4], xmm6
    addss xmm2, xmm4
    movss [r13 + r9 * 4], xmm2
    movaps xmm6, xmm3
    subss xmm6, xmm5
    movss [r14 + r11 * 4], xmm6
    addss xmm3, xmm5
    movss [r14 + r9 * 4], xmm3

    add r9d, r12d
    cmp r9d, r15d
    jb .k_loop
    inc r8d
    cmp r8d, r10d
    jb .j_loop
    shl r12d, 1
    cmp r12d, r15d
    jbe .stage

    add rsp, 16
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    xor eax, eax
    ret
.bad:
    mov eax, -1
    ret

global sauti_fft_magnitude_f32
sauti_fft_magnitude_f32:
    ; rdi=real, rsi=imag, edx=n, rcx=dst n/2+1 floats.
    test rdi, rdi
    jz .bad
    test rsi, rsi
    jz .bad
    test rcx, rcx
    jz .bad
    cmp edx, 64
    jb .bad
    cmp edx, 4096
    ja .bad
    mov eax, edx
    dec eax
    test eax, edx
    jnz .bad
    mov r8d, edx
    shr r8d, 1                  ; inclusive last bin (Nyquist)
    cvtsi2ss xmm4, edx
    movss xmm5, [rel sauti_fft_four]
    divss xmm5, xmm4            ; 4/n for periodic Hann AC bins
    movss xmm6, [rel sauti_fft_two]
    divss xmm6, xmm4            ; 2/n DC, Nyquist
    xor r9d, r9d
.loop:
    movss xmm0, [rdi + r9 * 4]
    movss xmm1, [rsi + r9 * 4]
    mulss xmm0, xmm0
    mulss xmm1, xmm1
    addss xmm0, xmm1
    sqrtss xmm0, xmm0
    cmp r9d, 0
    je .edge
    cmp r9d, r8d
    je .edge
    mulss xmm0, xmm5
    jmp .store
.edge:
    mulss xmm0, xmm6
.store:
    movss [rcx + r9 * 4], xmm0
    inc r9d
    cmp r9d, r8d
    jbe .loop
    xor eax, eax
    ret
.bad:
    mov eax, -1
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
