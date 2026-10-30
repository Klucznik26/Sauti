; Sayari Sauti - portable analysis/render engine, x86-64 System V ABI.
; No OS calls, no dynamic allocations, no dependency on Sayari or libc.
; Windows x64 requires an ABI bridge; AyoOS adapter must verify its ABI.
;
;   void sauti_sine_f32(float *dst, uint32_t count, float *phase, float step);
;   void sauti_metrics_f32(const float *src, uint32_t count, SautiMetrics *out);
;   void sauti_render_wave_argb32(uint32_t *dst, uint32_t width,
;                                  uint32_t height, const float *src,
;                                  uint32_t sample_count);
;
; Surface format: ARGB8888 in 32-bit native little-endian words.

bits 64
default rel

section .rodata align=16
align 16
sauti_abs_mask: dd 0x7fffffff, 0, 0, 0
sauti_one:      dd 0x3f800000
sauti_neg_one:  dd 0xbf800000
sauti_tau:      dd 0x40c90fdb
sauti_half:     dd 0x3f000000
sauti_gain_limit: dd 0x42800000 ; 64x maximum screen magnification

section .text

global sauti_sine_f32
sauti_sine_f32:
    ; rdi=dst, esi=count, rdx=*phase, xmm0=step (System V)
    test rdi, rdi
    jz .done
    test rdx, rdx
    jz .done
    test esi, esi
    jz .done
    sub rsp, 16
    movss xmm1, [rdx]
    movss xmm2, [rel sauti_tau]
    xor ecx, ecx
.loop:
    ; x87 FSIN is intentionally the small, simple reference generator.
    ; In later milestones SIMD polynomial/LUT generation may replace it.
    movss [rsp], xmm1
    fld dword [rsp]
    fsin
    fstp dword [rdi + rcx * 4]
    addss xmm1, xmm0
    ucomiss xmm1, xmm2
    jb .next
    subss xmm1, xmm2
.next:
    inc ecx
    cmp ecx, esi
    jb .loop
    movss [rdx], xmm1
    add rsp, 16
.done:
    ret

global sauti_metrics_f32
sauti_metrics_f32:
    ; rdi=samples, esi=count, rdx=out {float peak, float rms}
    test rdx, rdx
    jz .done
    xorps xmm0, xmm0          ; maximum abs(sample)
    xorps xmm1, xmm1          ; sum(sample^2)
    test rdi, rdi
    jz .zero
    test esi, esi
    jz .zero
    xor ecx, ecx
.loop:
    movss xmm2, [rdi + rcx * 4]
    movaps xmm3, xmm2
    andps xmm3, [rel sauti_abs_mask]
    maxss xmm0, xmm3
    mulss xmm2, xmm2
    addss xmm1, xmm2
    inc ecx
    cmp ecx, esi
    jb .loop
    cvtsi2ss xmm2, esi
    divss xmm1, xmm2
    sqrtss xmm1, xmm1
    movss [rdx], xmm0
    movss [rdx + 4], xmm1
    ret
.zero:
    mov dword [rdx], 0
    mov dword [rdx + 4], 0
.done:
    ret

global sauti_render_wave_argb32
global sauti_render_wave_scaled_argb32
sauti_render_wave_argb32:
    movss xmm0, [rel sauti_one] ; legacy 1x display
    jmp sauti_render_wave_scaled_argb32
sauti_render_wave_scaled_argb32:
    ; rdi=pixels, esi=width, edx=height, rcx=samples, r8d=count
    ; xmm0=gain (>=1, clamped at 64). Samples are never modified.
    ; Defensive: excessive dimensions and zero size do not touch memory.
    test rdi, rdi
    jz .done
    test esi, esi
    jz .done
    test edx, edx
    jz .done
    cmp esi, 8192
    ja .done
    cmp edx, 8192
    ja .done
    ucomiss xmm0, [rel sauti_one]
    jb .done                ; also rejects NaN and negative/zero gain
    minss xmm0, [rel sauti_gain_limit]
    movaps xmm5, xmm0       ; display-only gain, preserved throughout rendering
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12, rdi              ; pixel buffer
    mov r13d, esi             ; width
    mov r14d, edx             ; height
    mov r15, rcx              ; sample buffer
    mov ebx, r8d              ; sample count

    ; Clear all pixels to a graphite background.
    mov ecx, r13d
    imul rcx, r14
    mov eax, 0xff101722
    rep stosd

    ; Horizontal grid lines every 64 pixels.
    xor r9d, r9d
.hgrid_row:
    cmp r9d, r14d
    jae .vgrid_begin
    mov eax, r9d
    imul rax, r13
    lea rdi, [r12 + rax * 4]
    mov ecx, r13d
    mov eax, 0xff1d2d3c
    rep stosd
    add r9d, 64
    jmp .hgrid_row

    ; Vertical grid lines every 64 pixels.
.vgrid_begin:
    xor r9d, r9d
.vgrid_col:
    cmp r9d, r13d
    jae .centerline
    xor r10d, r10d
.vgrid_y:
    cmp r10d, r14d
    jae .vgrid_next
    mov eax, r10d
    imul rax, r13
    add rax, r9
    mov dword [r12 + rax * 4], 0xff1d2d3c
    inc r10d
    jmp .vgrid_y
.vgrid_next:
    add r9d, 64
    jmp .vgrid_col

    ; Zero axis across the vertical center.
.centerline:
    mov eax, r14d
    shr eax, 1
    imul rax, r13
    lea rdi, [r12 + rax * 4]
    mov ecx, r13d
    mov eax, 0xff315365
    rep stosd
    test r15, r15
    jz .finish
    test ebx, ebx
    jz .finish

    ; Each x selects one source sample. Render a connected vertical trace.
    cvtsi2ss xmm4, r14d
    mulss xmm4, [rel sauti_half]
    subss xmm4, [rel sauti_one]
    xor r10d, r10d            ; current x
    mov r11d, r14d
    shr r11d, 1              ; previous y
.wave_x:
    cmp r10d, r13d
    jae .finish
    mov eax, r10d
    imul rax, rbx
    xor edx, edx
    div r13                   ; index = x * count / width
    movss xmm0, [r15 + rax * 4]
    mulss xmm0, xmm5        ; auto-gain visual only, original audio remains untouched
    ; Clamp sample amplitude to [-1,1] before float-to-int conversion.
    maxss xmm0, [rel sauti_neg_one]
    minss xmm0, [rel sauti_one]
    mulss xmm0, xmm4
    cvtss2si r9d, xmm0
    mov eax, r14d
    shr eax, 1
    sub eax, r9d             ; current y
    test eax, eax
    jns .y_nonneg
    xor eax, eax
.y_nonneg:
    cmp eax, r14d
    jb .y_inrange
    mov eax, r14d
    dec eax
.y_inrange:
    mov r9d, eax              ; current y
    mov ecx, r11d
    cmp ecx, r9d
    jbe .trace_start
    mov ecx, r9d
.trace_start:
    mov edx, r11d
    cmp edx, r9d
    jae .trace_end_ok
    mov edx, r9d
.trace_end_ok:
.trace_column:
    mov eax, ecx
    imul rax, r13
    add rax, r10
    mov dword [r12 + rax * 4], 0xff32e6c2
    cmp ecx, edx
    jae .trace_done
    inc ecx
    jmp .trace_column
.trace_done:
    mov r11d, r9d
    inc r10d
    jmp .wave_x
.finish:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
.done:
    ret

; Explicit non-executable stack marker on ELF platforms.
section .note.GNU-stack noalloc noexec nowrite progbits
