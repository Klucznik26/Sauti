; True-stereo SPSC ring for Sauti. New ABI; legacy mono ring untouched.
; System V x86-64, x86 TSO only. Caller zero-initializes the entire ring.
; Layout: u32 write_index, u32 read_index, f32 interleaved[16384][2].
; uint32 sauti_stereo_ring_push_f32(ring*, const float* LR, uint32 frames)
; uint32 sauti_stereo_ring_drain_f32(ring*, float* left, float* right, uint32 window_frames)
; Producer drops newest frames if full; consumer keeps newest samples when backlogged.
bits 64
default rel
%define CAPACITY 16384
%define MASK 16383

section .rodata align=16
minimum: dd -1.0
maximum: dd 1.0

section .text
global sauti_stereo_ring_push_f32
sauti_stereo_ring_push_f32:
    test rdi, rdi
    jz .none
    test rsi, rsi
    jz .none
    test edx, edx
    jz .none
    cmp edx, CAPACITY
    ja .none
    mov r8d, [rdi]
    mov r9d, [rdi+4]
    mov ecx, r8d
    sub ecx, r9d
    cmp ecx, CAPACITY
    jae .none
    mov eax, CAPACITY
    sub eax, ecx
    cmp edx, eax
    cmova edx, eax
    xor ecx, ecx
.loop:
    mov eax, r8d
    add eax, ecx
    and eax, MASK
    movss xmm0, [rsi+rcx*8]
    ucomiss xmm0, xmm0
    jp .bad_l
    maxss xmm0, [rel minimum]
    minss xmm0, [rel maximum]
    jmp .save_l
.bad_l:
    xorps xmm0, xmm0
.save_l:
    movss [rdi+rax*8+8], xmm0
    movss xmm0, [rsi+rcx*8+4]
    ucomiss xmm0, xmm0
    jp .bad_r
    maxss xmm0, [rel minimum]
    minss xmm0, [rel maximum]
    jmp .save_r
.bad_r:
    xorps xmm0, xmm0
.save_r:
    movss [rdi+rax*8+12], xmm0
    inc ecx
    cmp ecx, edx
    jb .loop
    add r8d, edx
    mov [rdi], r8d
    mov eax, edx
    ret
.none:
    xor eax, eax
    ret

global sauti_stereo_ring_drain_f32
sauti_stereo_ring_drain_f32:
    test rdi, rdi
    jz .none
    test rsi, rsi
    jz .none
    test rdx, rdx
    jz .none
    test ecx, ecx
    jz .none
    cmp ecx, 4096
    ja .none
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov rbx, rdi
    mov r12, rsi
    mov r13, rdx
    mov r14d, ecx
    mov r8d, [rbx]
    mov r9d, [rbx+4]
    mov r10d, r8d
    sub r10d, r9d
    cmp r10d, CAPACITY
    ja .empty
    test r10d, r10d
    jz .empty
    cmp r10d, r14d
    jbe .size_ok
    mov r10d, r14d
    mov r9d, r8d
    sub r9d, r10d
.size_ok:
    mov r15d, r14d
    sub r15d, r10d
    mov rdi, r12
    lea rsi, [r12+r10*4]
    mov ecx, r15d
    rep movsd
    mov rdi, r13
    lea rsi, [r13+r10*4]
    mov ecx, r15d
    rep movsd
    xor ecx, ecx
.copy:
    mov eax, r9d
    add eax, ecx
    and eax, MASK
    movss xmm0, [rbx+rax*8+8]
    movss [r12+r15*4], xmm0
    movss xmm0, [rbx+rax*8+12]
    movss [r13+r15*4], xmm0
    inc r15d
    inc ecx
    cmp ecx, r10d
    jb .copy
    mov [rbx+4], r8d
    mov eax, r10d
    jmp .exit
.empty:
    xor eax, eax
.exit:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    ret
.none:
    xor eax, eax
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
