; Sauti segmented true-stereo display. x86-64 SysV.
; void sauti_render_stereo_bars_argb32(u32 *pixels,u32 w,u32 h,
;                                     const float *left256,const float *right256,
;                                     u32 phase)
; 24 columns; 20 segments each; no heap, no OS API. Rectangular neon approximation.
bits 64
default rel
section .rodata
zero: dd 0.0
one: dd 1.0
palette: dd 0xffffbf40,0xff20c997,0xff327df4,0xffe73d65
dark: dd 0xff272331
bg: dd 0xff101016
section .text
global sauti_render_stereo_bars_argb32
sauti_render_stereo_bars_argb32:
    test rdi,rdi
    jz .done
    test rcx,rcx
    jz .done
    test r8,r8
    jz .done
    cmp esi,256
    jb .done
    cmp esi,4096
    ja .done
    cmp edx,180
    jb .done
    cmp edx,4096
    ja .done
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    sub rsp,64
    mov r12,rdi
    mov r13d,esi
    mov r14d,edx
    mov [rsp],rcx
    mov [rsp+8],r8
    mov [rsp+16],r9d
    mov eax,[rel bg]
    mov ecx,r13d
    imul rcx,r14
    rep stosd
    ; Two panels, each half width minus center gap.
    mov eax,r13d
    shr eax,2
    sub eax,36
    mov [rsp+24],eax       ; panel width
    mov eax,r14d
    sub eax,54
    shr eax,1
    mov [rsp+60],eax       ; original panel height for anchored top
    imul eax,85
    xor edx,edx
    mov ecx,100
    div ecx
    mov [rsp+28],eax       ; compressed 85% vertical span
    mov ecx,[rsp+60]
    sub ecx,eax
    mov [rsp+60],ecx       ; vertical reduction: hold panel top fixed
    ; Raise full visual group, but preserve a safe top border on small windows.
    mov eax,r14d
    shr eax,2
    sub eax,24
    cmp eax,90
    jle .shift_capped
    mov eax,90
.shift_capped:
    test eax,eax
    jns .shift_ready
    xor eax,eax
.shift_ready:
    mov [rsp+56],eax
    xor r15d,r15d         ; bar index 0..23
.bar:
    cmp r15d,24
    jae .exit
    mov eax,r15d
    xor edx,edx
    mov ecx,12
    div ecx
    mov ebx,edx           ; position in panel
    test eax,eax
    jz .left
    mov r10,[rsp+8]
    mov ebp,r13d
    shr ebp,1
    add ebp,26
    jmp .source
.left:
    mov r10,[rsp]
    mov ebp,r13d
    shr ebp,1
    sub ebp,[rsp+24]
    sub ebp,26
.source:
    ; Aggregate a WHOLE log-frequency slice, not just one FFT band.
    ; A narrow spectral peak must not disappear between sampled bins.
    imul ebx,21
    xorps xmm0,xmm0
    xor ecx,ecx
.aggregate:
    movss xmm1,[r10+rbx*4]
    ucomiss xmm1,xmm1
    jp .skip_bin
    maxss xmm1,[rel zero]
    minss xmm1,[rel one]
    maxss xmm0,xmm1
.skip_bin:
    inc ebx
    inc ecx
    cmp ecx,21
    jb .aggregate
    mulss xmm0,[rel twenty]
    cvttss2si ebx,xmm0
.level:
    mov [rsp+32],ebx      ; number of lit segments
    mov eax,r15d
    xor edx,edx
    mov ecx,12
    div ecx
    mov ebx,edx
    mov eax,[rsp+24]
    imul eax,ebx
    xor edx,edx
    mov ecx,12
    div ecx
    add ebp,eax
    add ebp,8
    mov [rsp+36],ebp      ; bar x
    mov eax,[rsp+24]
    xor edx,edx
    mov ecx,12
    div ecx
    sub eax,8
    cmp eax,4
    jge .width_ok
    mov eax,6
.width_ok:
    mov [rsp+40],eax
    xor ebx,ebx
.segment:
    cmp ebx,20
    jae .next_bar
    ; y = h-12 - (segment+1) * (h-30)/20
    mov eax,[rsp+28]
    mov ecx,ebx
    inc ecx
    imul eax,ecx
    xor edx,edx
    mov ecx,20
    div ecx
    mov ebp,r14d
    add ebp,[rsp+28]
    shr ebp,1
    sub ebp,4
    sub ebp,[rsp+56]
    sub ebp,[rsp+60]      ; keep segment top anchored as height shrinks
    add ebp,20             ; clearance from top frame; balanced bottom margin
    sub ebp,eax
    mov [rsp+44],ebp
    mov eax,[rsp+28]
    xor edx,edx
    mov ecx,20
    div ecx
    shr eax,1
    cmp eax,4
    jge .height_ok
    mov eax,4
.height_ok:
    mov [rsp+48],eax
    mov eax,[rel dark]
    cmp ebx,[rsp+32]
    jae .paint
    mov eax,[rel palette]
    cmp ebx,0
    je .paint
    mov eax,[rel palette+4]
    cmp ebx,9
    jb .paint
    mov eax,[rel palette+8]
    cmp ebx,15
    jb .paint
    mov eax,[rel palette+12]
.paint:
    mov [rsp+52],eax
    ; Layered alpha blending gives a real halo instead of hard side strips.
    cmp ebx,[rsp+32]
    jae .no_halo
    mov eax,[rsp+16]
    shr eax,7
    add eax,ebx
    add eax,r15d
    and eax,3
    mov r9d,3
    test eax,2
    jnz .outer_bright
    mov r9d,4
.outer_bright:
    mov esi,[rsp+36]
    sub esi,8
    mov edi,[rsp+44]
    sub edi,6
    mov edx,[rsp+40]
    add edx,16
    mov ecx,[rsp+48]
    add ecx,12
    mov r8d,[rsp+52]
    call .blend_rect
    mov esi,[rsp+36]
    sub esi,4
    mov edi,[rsp+44]
    sub edi,3
    mov edx,[rsp+40]
    add edx,8
    mov ecx,[rsp+48]
    add ecx,6
    mov r8d,[rsp+52]
    mov r9d,2
    call .blend_rect
.no_halo:
    xor r10d,r10d
.y:
    cmp r10d,[rsp+48]
    jae .next_seg
    mov eax,[rsp+44]
    add eax,r10d
    cmp eax,0
    jl .advance_y
    cmp eax,r14d
    jae .advance_y
    imul rax,r13
    mov edx,[rsp+36]
    add rax,rdx
    xor r11d,r11d
.x:
    cmp r11d,[rsp+40]
    jae .advance_y
    ; Crisp glowing face, with a paler inset lip on lit segments.
    mov edx,[rsp+52]
    cmp ebx,[rsp+32]
    jae .set_pixel
    test r10d,r10d
    jz .set_pixel
    mov ecx,[rsp+48]
    dec ecx
    cmp r10d,ecx
    jae .set_pixel
    test r11d,r11d
    jz .set_pixel
    mov ecx,[rsp+40]
    dec ecx
    cmp r11d,ecx
    jae .set_pixel
    ; Add ~25% of remaining white to center, saturating each RGB lane.
    mov ecx,edx
    and ecx,0x00fefefe
    shr ecx,2
    and ecx,0x003f3f3f
    add edx,0x003f3f3f
    sub edx,ecx
.set_pixel:
    ; Round only the four extreme corner pixels (radius ~1px).
    mov ecx,[rsp+48]
    dec ecx
    cmp r10d,0
    je .corner_row
    cmp r10d,ecx
    jne .write_pixel
.corner_row:
    test r11d,r11d
    jz .round_skip
    mov ecx,[rsp+40]
    dec ecx
    cmp r11d,ecx
    je .round_skip
.write_pixel:
    mov [r12+rax*4],edx
.round_skip:
    inc rax
    inc r11d
    jmp .x
.advance_y:
    inc r10d
    jmp .y
.next_seg:
    inc ebx
    jmp .segment
.next_bar:
    inc r15d
    jmp .bar
.exit:
    add rsp,64
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
.done:
    ret
; Clip and alpha-composite RGB rectangle, preserving caller nonvolatile state.
; esi=x, edi=y, edx=width, ecx=height, r8d=core RGB, r9d=shift 2..4.
.blend_rect:
    test edx,edx
    jle .blend_done
    test ecx,ecx
    jle .blend_done
    sub rsp,48
    mov [rsp+0],r8d
    mov [rsp+4],r9d
    mov r10d,esi
    add r10d,edx
    mov r11d,edi
    add r11d,ecx
    test esi,esi
    jns .clip_x
    xor esi,esi
.clip_x:
    test edi,edi
    jns .clip_y
    xor edi,edi
.clip_y:
    cmp r10d,r13d
    jle .clip_right
    mov r10d,r13d
.clip_right:
    cmp r11d,r14d
    jle .clip_bottom
    mov r11d,r14d
.clip_bottom:
    mov [rsp+8],esi
    mov [rsp+12],edi
    mov [rsp+16],r10d
    mov [rsp+20],r11d
    cmp esi,r10d
    jge .blend_exit
    cmp edi,r11d
    jge .blend_exit
.blend_row:
    mov esi,[rsp+8]
.blend_pixel:
    mov eax,[rsp+12]
    imul rax,r13
    add rax,rsi
    mov r10d,[r12+rax*4]
    mov ecx,[rsp+4]
    mov r9d,1
    shl r9d,cl
    dec r9d
    ; Work per 8-bit channel to avoid 32-bit packed RGB overflow.
    ; blue
    mov edx,r10d
    and edx,255
    imul edx,r9d
    mov edi,[rsp+0]
    and edi,255
    add edx,edi
    shr edx,cl
    mov [rsp+24],edx
    ; green
    mov edx,r10d
    shr edx,8
    and edx,255
    imul edx,r9d
    mov edi,[rsp+0]
    shr edi,8
    and edi,255
    add edx,edi
    shr edx,cl
    shl edx,8
    or [rsp+24],edx
    ; red
    mov edx,r10d
    shr edx,16
    and edx,255
    imul edx,r9d
    mov edi,[rsp+0]
    shr edi,16
    and edi,255
    add edx,edi
    shr edx,cl
    shl edx,16
    mov edi,[rsp+24]
    or edi,edx
    or edi,0xff000000
    mov [r12+rax*4],edi
    inc esi
    cmp esi,[rsp+16]
    jl .blend_pixel
    mov eax,[rsp+12]
    inc eax
    mov [rsp+12],eax
    cmp eax,[rsp+20]
    jl .blend_row
.blend_exit:
    add rsp,48
.blend_done:
    ret

section .rodata
twenty: dd 20.0
section .note.GNU-stack noalloc noexec nowrite progbits
