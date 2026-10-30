; Sayari Sauti: fully OS-independent two-pane animated frame in NASM.
; x86-64 System V ABI, no libc, window or audio functions.
; int sauti_render_demo_frame(uint32_t *argb, uint32_t w, uint32_t h,
;                             SautiFrameScratch *scratch)
; The caller allocates >= w*h pixels and initializes scratch to zero once.
; Top: waveform; bottom: logarithmic frequency and -90..0 dBFS spectrum.

bits 64
default rel

%define SAMPLES          4096
%define BINS             2049
%define BANDS             256
%define TONE_OFFSET     16384
%define REAL_OFFSET     32768
%define IMAG_OFFSET     49152
%define MAG_OFFSET      65536
%define PHASE_A_OFFSET  81920
%define PHASE_B_OFFSET  81924
%define DB_OFFSET       81928
%define LOG_OFFSET      98312
%define SMOOTH_OFFSET   99336

section .rodata align=16
sauti_step_a:   dd 0x3d30c514  ; ~0.04316 rad/sample
sauti_step_b:   dd 0x3dc7db64  ; ~0.09759 rad/sample
sauti_gain_a:   dd 0x3f266666  ; 0.65
sauti_gain_b:   dd 0x3e8f5c29  ; 0.28
sauti_min:      dd 0xbf800000
sauti_max:      dd 0x3f800000
sauti_attack:   dd 0x3f400000  ; 0.75 per analysis frame
sauti_release:  dd 0x3dcccccd  ; 0.10 per analysis frame
sauti_auto_target: dd 0.85       ; waveform peak occupies 85% half-height
sauti_auto_floor:  dd 0.01328125 ; target / 64: protect very quiet input
sauti_auto_one:    dd 1.0
sauti_auto_limit:  dd 64.0

section .text
global sauti_render_demo_frame
global sauti_render_audio_frame
extern sauti_sine_f32
extern sauti_fft_real_f32
extern sauti_fft_magnitude_f32
extern sauti_db_normalize_f32
extern sauti_log_bands_f32
extern sauti_smooth_bands_f32
extern sauti_render_wave_scaled_argb32
extern sauti_metrics_f32
extern sauti_draw_levels_argb32
extern sauti_render_spectrum_argb32

sauti_render_demo_frame:
    xor r8d, r8d           ; generator mode
    jmp sauti_frame_common

sauti_render_audio_frame:
    mov r8d, 1             ; caller supplied audio
    jmp sauti_frame_common

sauti_frame_common:
    test rdi, rdi
    jz .bad
    test rcx, rcx
    jz .bad
    cmp esi, 128
    jb .bad
    cmp edx, 128
    jb .bad
    cmp esi, 4096
    ja .bad
    cmp edx, 4096
    ja .bad

    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12, rcx            ; frame scratch
    mov r13, rdi            ; pixels
    mov r14d, esi           ; width
    mov r15d, edx           ; height
    mov ebx, r8d          ; preserve mode before volatile calls
    test ebx, ebx
    jnz .analyze

    lea rdi, [r12]
    mov esi, SAMPLES
    lea rdx, [r12 + PHASE_A_OFFSET]
    movss xmm0, [rel sauti_step_a]
    call sauti_sine_f32

    lea rdi, [r12 + TONE_OFFSET]
    mov esi, SAMPLES
    lea rdx, [r12 + PHASE_B_OFFSET]
    movss xmm0, [rel sauti_step_b]
    call sauti_sine_f32

    xor ebx, ebx
.mix:
    movss xmm0, [r12 + rbx * 4]
    mulss xmm0, [rel sauti_gain_a]
    movss xmm1, [r12 + TONE_OFFSET + rbx * 4]
    mulss xmm1, [rel sauti_gain_b]
    addss xmm0, xmm1
    maxss xmm0, [rel sauti_min]
    minss xmm0, [rel sauti_max]
    movss [r12 + rbx * 4], xmm0
    inc ebx
    cmp ebx, SAMPLES
    jb .mix

.analyze:
    lea rdi, [r12]
    mov esi, SAMPLES
    lea rdx, [r12 + REAL_OFFSET]
    lea rcx, [r12 + IMAG_OFFSET]
    call sauti_fft_real_f32
    test eax, eax
    jnz .failed

    lea rdi, [r12 + REAL_OFFSET]
    lea rsi, [r12 + IMAG_OFFSET]
    mov edx, SAMPLES
    lea rcx, [r12 + MAG_OFFSET]
    call sauti_fft_magnitude_f32
    test eax, eax
    jnz .failed

    ; Convert linear FFT amplitudes to -90..0 dBFS display coordinates.
    lea rdi, [r12 + MAG_OFFSET]
    mov esi, BINS
    lea rdx, [r12 + DB_OFFSET]
    call sauti_db_normalize_f32
    test eax, eax
    jnz .failed

    ; Aggregate spectral bins into 256 log2-spaced frequency bands.
    lea rdi, [r12 + DB_OFFSET]
    mov esi, BINS
    lea rdx, [r12 + LOG_OFFSET]
    mov ecx, BANDS
    call sauti_log_bands_f32
    test eax, eax
    jnz .failed

    ; Fast attack, gentle release; state survives across frames.
    lea rdi, [r12 + LOG_OFFSET]
    lea rsi, [r12 + SMOOTH_OFFSET]
    mov edx, BANDS
    movss xmm0, [rel sauti_attack]
    movss xmm1, [rel sauti_release]
    call sauti_smooth_bands_f32
    test eax, eax
    jnz .failed

    ; Peak/RMS in the ORIGINAL signal units, never after visual gain.
    ; 16-byte aligned on SysV ABI (after 5 register pushes).
    sub rsp, 16                ; 0..7 peak/rms; 8..15 normalized display
    mov rdi, r12
    mov esi, SAMPLES
    mov rdx, rsp
    call sauti_metrics_f32

    ; Stable display-only magnification target 85% screen amplitude.
    ; Clamp to [1x, 64x]; no change to FFT or the caller's sample array.
    movss xmm1, [rsp]          ; peak
    maxss xmm1, [rel sauti_auto_floor]
    movss xmm0, [rel sauti_auto_target]
    divss xmm0, xmm1
    maxss xmm0, [rel sauti_auto_one]
    minss xmm0, [rel sauti_auto_limit]

    mov rdi, r13
    mov esi, r14d
    mov edx, r15d
    shr edx, 1
    mov rcx, r12
    mov r8d, SAMPLES
    call sauti_render_wave_scaled_argb32

    ; Two on-screen dBFS level meters for unamplified Peak and RMS.
    mov rdi, rsp
    mov esi, 2
    lea rdx, [rsp + 8]
    call sauti_db_normalize_f32
    mov rdi, r13
    mov esi, r14d
    mov edx, r15d
    shr edx, 1
    lea rcx, [rsp + 8]
    call sauti_draw_levels_argb32
    add rsp, 16

    mov eax, r15d
    shr eax, 1
    imul rax, r14
    lea rdi, [r13 + rax * 4]
    mov esi, r14d
    mov edx, r15d
    mov eax, edx
    shr eax, 1
    sub edx, eax
    lea rcx, [r12 + SMOOTH_OFFSET]
    mov r8d, BANDS
    call sauti_render_spectrum_argb32

    xor eax, eax
    jmp .exit
.failed:
    mov eax, -1
.exit:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
.bad:
    mov eax, -1
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
