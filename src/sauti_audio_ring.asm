; Sayari Sauti: SPSC stereo capture buffer (x86-64 System V ABI).
; One real-time producer and one UI consumer, independent of PipeWire/OS.
; X86 TSO provides acquire/release semantics for aligned uint32 indices.
; No allocations, locks or syscalls. Caller owns initialization and lifetime.
;
; ring struct { u32 w, u32 r, float samples[16384] };
; uint32 sauti_ring_push_stereo_f32(ring*, float interleaved_stereo*, u32 frames)
; uint32 sauti_ring_drain_window_f32(ring*, float window*, u32 n <= 4096)
; On full ring: producer drops newest samples. On an overrun backlog, UI
; skips to the newest 'n' samples. Read window is chronological and shifts
; left by number of newly consumed samples before appending. Caller must
; initialize both ring and window to zero before first use.
bits 64
default rel
%define RING_SIZE 16384
%define RING_MASK 16383

section .rodata align=16
sauti_ring_half: dd 0x3f000000
sauti_ring_min:  dd 0xbf800000
sauti_ring_max:  dd 0x3f800000

section .text
global sauti_ring_push_stereo_f32
sauti_ring_push_stereo_f32:
    test rdi, rdi
    jz .none
    test rsi, rsi
    jz .none
    test edx, edx
    jz .none
    cmp edx, RING_SIZE
    ja .none
    mov r8d, [rdi]           ; producer's private write index
    mov r9d, [rdi + 4]       ; acquire consumer's read index
    mov ecx, r8d
    sub ecx, r9d
    cmp ecx, RING_SIZE
    jae .none
    mov eax, RING_SIZE
    sub eax, ecx            ; room = capacity - occupancy
    cmp edx, eax
    cmova edx, eax
    xor ecx, ecx
.loop:
    movss xmm0, [rsi + rcx * 8]
    addss xmm0, [rsi + rcx * 8 + 4]
    mulss xmm0, [rel sauti_ring_half]
    ucomiss xmm0, xmm0
    jp .invalid
    maxss xmm0, [rel sauti_ring_min]
    minss xmm0, [rel sauti_ring_max]
    jmp .save
.invalid:
    xorps xmm0, xmm0
.save:
    mov eax, r8d
    add eax, ecx
    and eax, RING_MASK
    movss [rdi + rax * 4 + 8], xmm0
    inc ecx
    cmp ecx, edx
    jb .loop
    add r8d, edx
    mov [rdi], r8d           ; publish after writing samples (release)
    mov eax, edx
    ret
.none:
    xor eax, eax
    ret

global sauti_ring_drain_window_f32
sauti_ring_drain_window_f32:
    test rdi, rdi
    jz .none
    test rsi, rsi
    jz .none
    test edx, edx
    jz .none
    cmp edx, 4096
    ja .none
    push rbx
    push r12
    mov rbx, rdi            ; ring
    mov r12, rsi            ; consumer's persistent window
    mov r8d, [rbx]          ; acquire producer publication
    mov r9d, [rbx + 4]      ; private read index
    mov r10d, r8d
    sub r10d, r9d           ; backlog in samples
    cmp r10d, RING_SIZE
    ja .empty              ; malformed ring: no writes
    test r10d, r10d
    jz .empty
    cmp r10d, edx
    jbe .size_ok
    mov r10d, edx
    mov r9d, r8d
    sub r9d, r10d           ; skip old backlog, retain newest n
.size_ok:
    mov r11d, edx
    sub r11d, r10d          ; number of samples retained in window
    mov rdi, r12
    lea rsi, [r12 + r10 * 4]
    mov ecx, r11d
    rep movsd               ; forward memmove, destination < source

    xor ecx, ecx
.copy:
    mov eax, r9d
    add eax, ecx
    and eax, RING_MASK
    movss xmm0, [rbx + rax * 4 + 8]
    movss [r12 + r11 * 4], xmm0
    inc ecx
    inc r11d
    cmp ecx, r10d
    jb .copy

    mov [rbx + 4], r8d      ; publish consumer progress (release)
    mov eax, r10d
    pop r12
    pop rbx
    ret
.empty:
    pop r12
    pop rbx
.none:
    xor eax, eax
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
