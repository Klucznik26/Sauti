; Sayari Sauti: simple Peak/RMS level meters in independent NASM.
; void sauti_draw_levels_argb32(uint32_t *argb, u32 width, u32 height,
;                               const float normalized_db_levels[2]);
; Samples are unmodified. x=44..220 top pane, pink Peak and emerald RMS.
; 7-pixel P/R glyphs render the meanings; meter coordinates represent
; values normalized from -90..0 dBFS. Caller owns width*height ARGB pixels.
bits 64
default rel
section .rodata align=16
level_zero: dd 0x00000000
level_one:  dd 0x3f800000
label_P:    db 0x1e,0x11,0x11,0x1e,0x10,0x10,0x10
label_R:    db 0x1e,0x11,0x11,0x1e,0x14,0x12,0x11
section .text
global sauti_draw_levels_argb32
sauti_draw_levels_argb32:
    test rdi, rdi
    jz .done
    test rcx, rcx
    jz .done
    cmp esi, 128
    jb .done
    cmp edx, 64
    jb .done
    cmp esi, 4096
    ja .done
    cmp edx, 4096
    ja .done
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12, rdi
    mov r13d, esi
    mov r14, rcx
    mov r15d, esi
    sub r15d, 60
    cmp r15d, 176
    jbe .width_ok
    mov r15d, 176
.width_ok:
    xor ebx, ebx             ; 0 Peak, 1 RMS
.meter:
    mov r8d, 10              ; Peak y origin
    test ebx, ebx
    jz .read_level
    mov r8d, 27              ; RMS y origin
.read_level:
    movss xmm0, [r14 + rbx * 4]
    ucomiss xmm0, xmm0
    jp .zero
    maxss xmm0, [rel level_zero]
    minss xmm0, [rel level_one]
    jmp .scale
.zero:
    xorps xmm0, xmm0
.scale:
    cvtsi2ss xmm1, r15d
    mulss xmm0, xmm1
    cvttss2si r9d, xmm0      ; filled width in pixels

    xor r10d, r10d           ; y 0..6
.row:
    xor r11d, r11d           ; x 0..meter width-1
.col:
    mov eax, r11d
    cmp eax, r9d
    jae .background
    mov edx, 0xffd66b75     ; Peak / ruby
    test ebx, ebx
    jz .paint
    mov edx, 0xff31d7b8     ; RMS / emerald
    jmp .paint
.background:
    mov edx, 0xff253544
.paint:
    mov eax, r8d
    add eax, r10d
    imul rax, r13
    add rax, r11
    add rax, 44
    mov [r12 + rax * 4], edx
    inc r11d
    cmp r11d, r15d
    jb .col

    ; 5x7 legend P or R using 5-bit bitmap rows.
    lea rsi, [rel label_P]
    test ebx, ebx
    jz .glyph
    lea rsi, [rel label_R]
.glyph:
    movzx eax, byte [rsi + r10]
    xor r11d, r11d
.glyph_col:
    mov edx, 4
    sub edx, r11d
    bt eax, edx
    jnc .glyph_next
    mov edx, r8d
    add edx, r10d
    imul rdx, r13
    add rdx, r11
    add rdx, 22
    mov dword [r12 + rdx * 4], 0xffe2ebf2
.glyph_next:
    inc r11d
    cmp r11d, 5
    jb .glyph_col

    inc r10d
    cmp r10d, 7
    jb .row

    inc ebx
    cmp ebx, 2
    jb .meter
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
.done:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
