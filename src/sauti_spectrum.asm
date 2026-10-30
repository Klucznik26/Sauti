; Sayari Sauti - OS-independent ARGB8888 spectrum renderer.
; x86-64 System V ABI:
; void sauti_render_spectrum_argb32(uint32_t *dst, uint32_t width,
;                                    uint32_t height, const float *mags,
;                                    uint32_t bin_count);
; Linear frequency axis, single-sided normalized linear amplitudes.
; Caller supplies at least width*height pixels and bin_count magnitudes.
; Invalid dimensions/pointers do not touch destination.
bits 64
default rel

section .rodata align=16
sauti_spectrum_zero: dd 0x00000000
sauti_spectrum_one:  dd 0x3f800000

section .text
global sauti_render_spectrum_argb32
sauti_render_spectrum_argb32:
    test rdi, rdi
    jz .done
    test esi, esi
    jz .done
    cmp esi, 8192
    ja .done
    cmp edx, 32
    jb .done
    cmp edx, 8192
    ja .done
    test rcx, rcx
    jz .done
    test r8d, r8d
    jz .done
    cmp r8d, 4096
    ja .done

    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12, rdi              ; pixels
    mov r13d, esi             ; width
    mov r14d, edx             ; height
    mov r15, rcx              ; magnitudes
    mov ebx, r8d              ; number of bins

    mov eax, 0xff101722
    mov ecx, r13d
    imul rcx, r14
    rep stosd                 ; graphite background

    ; Grid guides every 64 pixels.
    xor r9d, r9d
.grid_y:
    cmp r9d, r14d
    jae .floor_line
    mov eax, r9d
    imul rax, r13
    lea rdi, [r12 + rax * 4]
    mov ecx, r13d
    mov eax, 0xff1d2d3c
    rep stosd
    add r9d, 64
    jmp .grid_y
.floor_line:
    mov eax, r14d
    sub eax, 8
    imul rax, r13
    lea rdi, [r12 + rax * 4]
    mov ecx, r13d
    mov eax, 0xff315365
    rep stosd

    xor r9d, r9d             ; x column
.column:
    cmp r9d, r13d
    jae .finish
    mov eax, r9d
    and eax, 3
    cmp eax, 3
    je .next

    mov eax, r9d
    imul rax, rbx
    xor edx, edx
    div r13                   ; bin = (x * bins) / width
    movss xmm0, [r15 + rax * 4]
    maxss xmm0, [rel sauti_spectrum_zero]
    minss xmm0, [rel sauti_spectrum_one]
    mov eax, r14d
    sub eax, 12              ; 8px floor + 4px headroom
    cvtsi2ss xmm1, eax
    mulss xmm0, xmm1
    cvttss2si r10d, xmm0     ; magnitude => filled pixels
    test r10d, r10d
    jle .next
    mov edx, r14d
    sub edx, 9               ; last filled y
    mov eax, edx
    sub eax, r10d
    inc eax                  ; first filled y
    xor r11d, r11d
    cmp eax, 0
    cmovl eax, r11d
    mov ecx, eax             ; y
.draw:
    cmp ecx, edx
    jg .next
    mov eax, ecx
    imul rax, r13
    add rax, r9
    mov r11d, 0xff31d7b8    ; teal bottom
    mov esi, r14d
    shr esi, 1
    cmp ecx, esi
    jae .paint
    mov r11d, 0xffd66b75    ; ruby upper half
.paint:
    mov dword [r12 + rax * 4], r11d
    inc ecx
    jmp .draw
.next:
    inc r9d
    jmp .column
.finish:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
.done:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
