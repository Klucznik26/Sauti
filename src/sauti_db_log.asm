; Sayari Sauti: logarithmic spectrum processing and attack/release envelopes.
; OS-independent x86-64 System V ABI, NASM. All arrays caller-owned.
; No malloc, syscalls, shared static mutable state, or C DSP dependencies.
; Integer/log mapping deliberately skips FFT DC bin 0.

bits 64
default rel

section .rodata align=16
sauti_zero:        dd 0x00000000
sauti_one:         dd 0x3f800000
sauti_min_amp:     dd 0x38049ba6  ; approx 3.16228e-5 (-90 dBFS)
sauti_db_per_log2: dd 0x3d8901a5  ; 20/(90*log2(10)) = 0.0668956

section .text

global sauti_db_normalize_f32
; int sauti_db_normalize_f32(const float *amplitudes, uint32_t n, float *out)
; Converts linear amplitude to [0..1], with 0 at -90dBFS and 1 at 0dBFS.
; Negative, zero, NaN and below-floor => 0; +Inf or >=1 => 1.
sauti_db_normalize_f32:
    test rdi, rdi
    jz .bad
    test rdx, rdx
    jz .bad
    test esi, esi
    jz .bad
    cmp esi, 4096
    ja .bad
    sub rsp, 16
    xor ecx, ecx
.loop:
    movss xmm0, [rdi + rcx * 4]
    ucomiss xmm0, xmm0
    jp .zero
    ucomiss xmm0, [rel sauti_min_amp]
    jbe .zero
    ucomiss xmm0, [rel sauti_one]
    jae .one
    movss [rsp], xmm0
    fld1
    fld dword [rsp]
    fyl2x
    fstp dword [rsp]
    movss xmm0, [rsp]
    mulss xmm0, [rel sauti_db_per_log2]
    addss xmm0, [rel sauti_one]
    maxss xmm0, [rel sauti_zero]
    jmp .store
.zero:
    xorps xmm0, xmm0
    jmp .store
.one:
    movss xmm0, [rel sauti_one]
.store:
    movss [rdx + rcx * 4], xmm0
    inc ecx
    cmp ecx, esi
    jb .loop
    add rsp, 16
    xor eax, eax
    ret
.bad:
    mov eax, -1
    ret

global sauti_log_bands_f32
; int sauti_log_bands_f32(const float *db_bins, uint32_t bin_count,
;                         float *bands, uint32_t band_count)
; Maps bins 1..N-1 (DC excluded) to log2-spaced bands by max amplitude.
; Both arrays have caller-supplied capacities (N, and band_count).
sauti_log_bands_f32:
    test rdi, rdi
    jz .bad
    test rdx, rdx
    jz .bad
    cmp esi, 3
    jb .bad
    cmp esi, 4096
    ja .bad
    cmp ecx, 2
    jb .bad
    cmp ecx, 1024
    ja .bad

    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 16
    mov r12, rdi
    mov r13d, esi
    mov r14, rdx
    mov r15d, ecx
    mov rdi, r14
    mov ecx, r15d
    xor eax, eax
    rep stosd

    ; scale = (band_count - 1) / log2(bin_count - 1)
    mov eax, r13d
    dec eax
    mov [rsp], eax
    fld1
    fild dword [rsp]
    fyl2x
    fstp dword [rsp + 4]
    mov eax, r15d
    dec eax
    cvtsi2ss xmm0, eax
    divss xmm0, [rsp + 4]
    movss [rsp + 8], xmm0

    mov ebx, 1
.map:
    mov [rsp], ebx
    fld1
    fild dword [rsp]
    fyl2x
    fstp dword [rsp + 12]
    movss xmm0, [rsp + 12]
    mulss xmm0, [rsp + 8]
    cvttss2si eax, xmm0
    cmp eax, r15d
    jb .band_in_range
    mov eax, r15d
    dec eax
.band_in_range:
    movss xmm1, [r12 + rbx * 4]
    ucomiss xmm1, xmm1
    jp .next
    maxss xmm1, [rel sauti_zero]
    minss xmm1, [rel sauti_one]
    maxss xmm1, [r14 + rax * 4]
    movss [r14 + rax * 4], xmm1
.next:
    inc ebx
    cmp ebx, r13d
    jb .map
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

global sauti_smooth_bands_f32
; int sauti_smooth_bands_f32(const float *target, float *history,
;                            uint32_t n, float attack, float release)
; history updated in-place; attack and release must be finite [0..1].
sauti_smooth_bands_f32:
    test rdi, rdi
    jz .bad
    test rsi, rsi
    jz .bad
    test edx, edx
    jz .bad
    cmp edx, 1024
    ja .bad
    ucomiss xmm0, xmm0
    jp .bad
    ucomiss xmm1, xmm1
    jp .bad
    ucomiss xmm0, [rel sauti_zero]
    jb .bad
    ucomiss xmm1, [rel sauti_zero]
    jb .bad
    ucomiss xmm0, [rel sauti_one]
    ja .bad
    ucomiss xmm1, [rel sauti_one]
    ja .bad
    movaps xmm4, xmm0
    movaps xmm5, xmm1
    xor ecx, ecx
.loop:
    movss xmm2, [rdi + rcx * 4]
    ucomiss xmm2, xmm2
    jp .target_zero
    maxss xmm2, [rel sauti_zero]
    minss xmm2, [rel sauti_one]
    jmp .target_valid
.target_zero:
    xorps xmm2, xmm2
.target_valid:
    movss xmm3, [rsi + rcx * 4]
    ucomiss xmm3, xmm3
    jp .history_zero
    maxss xmm3, [rel sauti_zero]
    minss xmm3, [rel sauti_one]
    jmp .history_valid
.history_zero:
    xorps xmm3, xmm3
.history_valid:
    movaps xmm6, xmm2
    subss xmm6, xmm3
    ucomiss xmm2, xmm3
    ja .attack
    mulss xmm6, xmm5
    jmp .write
.attack:
    mulss xmm6, xmm4
.write:
    addss xmm3, xmm6
    movss [rsi + rcx * 4], xmm3
    inc ecx
    cmp ecx, edx
    jb .loop
    xor eax, eax
    ret
.bad:
    mov eax, -1
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
