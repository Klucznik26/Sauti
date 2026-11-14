; ISO AAC spectral codebook index -> integer tuple, NASM x86-64 SysV.
; Codeword lookup happens in sauti_aac_huffman_decode_asm.
; Codes: 1/2 signed 4 x [-1..1], 3/4 unsigned 4 x [0..2],
; 5/6 signed 2 x [-4..4], 7/8 unsigned 2 x [0..7],
; 9/10 unsigned 2 x [0..12], 11 unsigned 2 x [0..16].
; The decoder extracts radix digits in the specification's band order.
;
; int f(uint32_t codebook (edi), uint32_t index (esi), int16_t out[4] (rdx))
default rel
global sauti_aac_spectral_index_asm
section .text
sauti_aac_spectral_index_asm:
    xor eax,eax
    test rdx,rdx
    jz .fail
    cmp edi,1
    jb .fail
    cmp edi,2
    jbe .quad_signed
    cmp edi,4
    jbe .quad_unsigned
    cmp edi,6
    jbe .pair_signed
    cmp edi,8
    jbe .pair_8
    cmp edi,10
    jbe .pair_13
    cmp edi,11
    je .pair_17
    jmp .fail
.quad_signed:
    mov r8d,3               ; radix 2*LAV+1
    mov r10d,1              ; subtracted signed offset
    mov ecx,4
    mov edi,81
    jmp .common
.quad_unsigned:
    mov r8d,3
    xor r10d,r10d
    mov ecx,4
    mov edi,81
    jmp .common
.pair_signed:
    mov r8d,9
    mov r10d,4
    mov ecx,2
    mov edi,81
    jmp .common
.pair_8:
    mov r8d,8
    xor r10d,r10d
    mov ecx,2
    mov edi,64
    jmp .common
.pair_13:
    mov r8d,13
    xor r10d,r10d
    mov ecx,2
    mov edi,169
    jmp .common
.pair_17:
    mov r8d,17
    xor r10d,r10d
    mov ecx,2
    mov edi,289
.common:
    cmp esi,edi             ; every index must be in the codebook table
    jae .fail
    mov r9,rdx              ; preserve caller output across integer div
    mov r11d,esi
    sub rsp,16
    mov qword [rsp],0       ; zero unused tuple slots
.loop:
    mov eax,r11d
    xor edx,edx
    div r8d                 ; quotient in eax, digit in edx
    mov r11d,eax
    sub edx,r10d
    dec ecx
    mov word [rsp+rcx*2],dx
    test ecx,ecx
    jnz .loop
    mov rax,[rsp]
    mov [r9],rax
    add rsp,16
    mov eax,1
    ret
.fail:
    xor eax,eax
    ret
