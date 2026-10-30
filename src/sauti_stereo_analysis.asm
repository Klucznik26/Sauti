; Sauti true-stereo FFT analysis: independent history per channel.
; int sauti_analyze_stereo_bands_f32(const float *left_window,
;   const float *right_window, SautiFrameScratch *left, SautiFrameScratch *right)
; Each scratch is zero-initialized and retained across analysis frames.
; Reads 4096 samples/channel, outputs 256 smoothed logarithmic bands/channel.
; No allocation, platform calls, or shared global state.
bits 64
default rel
%define N 4096
%define BINS 2049
%define BANDS 256
%define REAL_OFFSET 32768
%define IMAG_OFFSET 49152
%define MAG_OFFSET 65536
%define DB_OFFSET 81928
%define LOG_OFFSET 98312
%define SMOOTH_OFFSET 99336

section .rodata
attack: dd 0.75
release: dd 0.10

section .text
global sauti_analyze_stereo_bands_f32
extern sauti_fft_real_f32
extern sauti_fft_magnitude_f32
extern sauti_db_normalize_f32
extern sauti_log_bands_f32
extern sauti_smooth_bands_f32

sauti_analyze_stereo_bands_f32:
    test rdi, rdi
    jz .bad
    test rsi, rsi
    jz .bad
    test rdx, rdx
    jz .bad
    test rcx, rcx
    jz .bad
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12, rdi
    mov r13, rsi
    mov r14, rdx
    mov r15, rcx
    mov rdi, r12
    mov rsi, r14
    call .analyze_one
    test eax, eax
    jnz .end
    mov rdi, r13
    mov rsi, r15
    call .analyze_one
.end:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
.bad:
    mov eax, -1
    ret

; RDI: 4096-sample window, RSI: persistent SautiFrameScratch.
; Parent has already aligned stack to 16 bytes.
.analyze_one:
    push rbx
    sub rsp, 16
    mov rbx, rsi
    mov [rsp], rdi
    mov esi, N
    lea rdx, [rbx+REAL_OFFSET]
    lea rcx, [rbx+IMAG_OFFSET]
    call sauti_fft_real_f32
    test eax, eax
    jnz .failed
    lea rdi, [rbx+REAL_OFFSET]
    lea rsi, [rbx+IMAG_OFFSET]
    mov edx, N
    lea rcx, [rbx+MAG_OFFSET]
    call sauti_fft_magnitude_f32
    test eax, eax
    jnz .failed
    lea rdi, [rbx+MAG_OFFSET]
    mov esi, BINS
    lea rdx, [rbx+DB_OFFSET]
    call sauti_db_normalize_f32
    test eax, eax
    jnz .failed
    lea rdi, [rbx+DB_OFFSET]
    mov esi, BINS
    lea rdx, [rbx+LOG_OFFSET]
    mov ecx, BANDS
    call sauti_log_bands_f32
    test eax, eax
    jnz .failed
    lea rdi, [rbx+LOG_OFFSET]
    lea rsi, [rbx+SMOOTH_OFFSET]
    mov edx, BANDS
    movss xmm0, [rel attack]
    movss xmm1, [rel release]
    call sauti_smooth_bands_f32
.failed:
    add rsp, 16
    pop rbx
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
