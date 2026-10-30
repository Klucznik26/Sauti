; Independent stereo spectral peak hold, System V AMD64 ABI.
; SautiPeakHold = uint32_t level[24], last_peak_ms[24].
; uint32 levels are in [0,20] and only their renderer-visible bands are used.
; Hold 300 ms; decay one segment every 80 ms thereafter.
bits 64
default rel
section .rodata
zero: dd 0.0
one: dd 1.0
twenty: dd 20.0
section .text
global sauti_peak_hold_update
sauti_peak_hold_update:
    test rdi,rdi
    jz .done
    test rsi,rsi
    jz .done
    test rdx,rdx
    jz .done
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r15d,ecx          ; preserve timestamp across band aggregation
    mov r12,rdi
    mov r13,rsi
    mov r14,rdx
    xor ebx,ebx
.bar:
    cmp ebx,24
    jae .exit
    mov eax,ebx
    xor edx,edx
    mov esi,12
    div esi
    mov r9d,edx
    mov r10,r13
    test eax,eax
    jz .source
    mov r10,r14
.source:
    imul r9d,21
    xorps xmm0,xmm0
    xor esi,esi
.band:
    movss xmm1,[r10+r9*4]
    ucomiss xmm1,xmm1
    jp .skip
    maxss xmm1,[rel zero]
    minss xmm1,[rel one]
    maxss xmm0,xmm1
.skip:
    inc r9d
    inc esi
    cmp esi,21
    jb .band
    mulss xmm0,[rel twenty]
    cvttss2si eax,xmm0
    cmp eax,20
    jle .clamped
    mov eax,20
.clamped:
    mov edx,[r12+rbx*4]
    cmp eax,edx
    jae .new_peak
    mov r8d,r15d
    sub r8d,[r12+96+rbx*4]
    cmp r8d,300
    jbe .next
    sub r8d,300
    mov eax,r8d
    xor edx,edx
    mov esi,80
    div esi
    ; number of elapsed decay steps (independent of frame rate)
    mov edx,[r12+rbx*4]
    cmp eax,edx
    jb .subtract
    xor edx,edx
    mov [r12+96+rbx*4],r15d   ; fully decayed: reset epoch
    jmp .store
.subtract:
    sub edx,eax
    ; Consume elapsed 80ms steps: never subtract the same time twice.
    imul eax,80
    add [r12+96+rbx*4],eax
.store:
    mov [r12+rbx*4],edx
    jmp .next
.new_peak:
    mov [r12+rbx*4],eax
    mov [r12+96+rbx*4],r15d
.next:
    inc ebx
    jmp .bar
.exit:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
.done:
    ret

; Draw thin bright markers over an already rendered stereo-bar canvas.
; args: (pixels,w,h,const SautiPeakHold *state) => SysV RDI,ESI,EDX,RCX.
; Uses same panel/bar and vertical geometry as sauti_stereo_bars.asm.
global sauti_draw_peak_hold_argb32
sauti_draw_peak_hold_argb32:
    test rdi,rdi
    jz .out
    test rcx,rcx
    jz .out
    cmp esi,256
    jb .out
    cmp esi,4096
    ja .out
    cmp edx,180
    jb .out
    cmp edx,4096
    ja .out
    push rbx
    push r12
    push r13
    push r14
    push r15
    push rbp
    mov r12,rdi
    mov r13d,esi
    mov r14d,edx
    mov r15,rcx
    xor ebx,ebx
.marker:
    cmp ebx,24
    jae .end
    mov r8d,[r15+rbx*4]
    test r8d,r8d
    jz .next
    cmp r8d,20
    jbe .valid
    mov r8d,20
.valid:
    ; Peak marker belongs in the segment immediately ABOVE the held bar.
    ; Levels are counts (1..20), while LED positions are indexed 0..19.
    ; At maximum height there is no 21st cell: use the topmost cell.
    cmp r8d,20
    jne .marker_index_ready
    dec r8d
.marker_index_ready:
    mov eax,ebx
    xor edx,edx
    mov ecx,12
    div ecx
    mov r9d,edx
    mov r10d,r13d
    shr r10d,2
    sub r10d,36
    imul r9d,r10d
    mov eax,r9d
    xor edx,edx
    mov ecx,12
    div ecx
    mov r9d,eax
    mov eax,r13d
    shr eax,1
    cmp ebx,12
    jae .right
    mov ecx,eax
    sub ecx,r10d
    sub ecx,26
    jmp .x_offset
.right:
    mov ecx,eax
    add ecx,26
.x_offset:
    add r9d,ecx
    add r9d,8
    mov ebp,r9d             ; retain column X; r9 is reused for height arithmetic
    mov r10d,r13d
    shr r10d,2
    sub r10d,36
    mov eax,r10d
    xor edx,edx
    mov ecx,12
    div ecx
    sub eax,8
    cmp eax,4
    jge .width
    mov eax,6
.width:
    mov r10d,eax
    mov eax,r14d
    sub eax,54
    shr eax,1
    mov r9d,eax           ; original vertical span
    imul eax,85
    xor edx,edx
    mov ecx,100
    div ecx
    sub r9d,eax           ; reduction for top-anchored compressed display
    mov ecx,r8d
    inc ecx
    imul eax,ecx
    xor edx,edx
    mov ecx,20
    div ecx
    mov r11d,r14d
    mov ecx,r14d
    sub ecx,54
    shr ecx,1
    add r11d,ecx
    shr r11d,1
    sub r11d,4
    ; Must match half-scale segment elevation in stereo bar renderer.
    mov ecx,r14d
    shr ecx,2
    sub ecx,24
    cmp ecx,90
    jle .shift_capped
    mov ecx,90
.shift_capped:
    test ecx,ecx
    jns .shift_ready
    xor ecx,ecx
.shift_ready:
    sub r11d,ecx
    sub r11d,r9d
    add r11d,20            ; match LED stack frame clearance
    sub r11d,eax
    sub r11d,4             ; peak marker above LED face, not in the gap
    cmp r11d,0
    jl .next
    cmp r11d,r14d
    jae .next
    ; Marker is drawn only when its bar has fallen below its held peak,
    ; otherwise it is already lit. Bright line helps peak readability.
    mov eax,r8d
    cmp eax,0
    je .amber
    cmp eax,9
    jb .emerald
    cmp eax,15
    jb .sapphire
    mov esi,0xffffa5b7
    jmp .color
.amber:
    mov esi,0xffffe2a3
    jmp .color
.emerald:
    mov esi,0xffbaffdf
    jmp .color
.sapphire:
    mov esi,0xffc6dfff
.color:
    mov eax,r11d
    imul rax,r13
    add rax,rbp             ; correct bar X, not temporary height delta
    xor ecx,ecx
.pixel:
    cmp ecx,r10d
    jae .next
    mov [r12+rax*4],esi
    inc rax
    inc ecx
    jmp .pixel
.next:
    inc ebx
    jmp .marker
.end:
    pop rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
.out:
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
