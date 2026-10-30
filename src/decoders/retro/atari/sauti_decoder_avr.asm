; AVR PCM parser and sample conversion, hand-written NASM x86-64 (SysV ABI).
; No external codec or operating system calls.
default rel
global sauti_avr_parse_asm
global sauti_avr_pcm_to_f32_asm
section .rodata
scale8: dd 0x3c000000
scale16: dd 0x38000000
section .text
; int parse(const uint8_t *data, size_t len, AVRMeta *out)
; AVRMeta: uint32 rate, channels, bits, is_signed; uint64 frames; const uint8_t *audio
sauti_avr_parse_asm:
    xor eax,eax
    test rdi,rdi
    jz .fail
    test rdx,rdx
    jz .fail
    cmp rsi,128
    jb .fail
    cmp dword [rdi],0x54494232  ; "2BIT"
    jne .fail
    movzx ecx,word [rdi+12]
    xchg cl,ch
    cmp cx,0
    je .mono
    cmp cx,0xffff
    jne .fail
    mov r8d,2
    jmp .channel_ok
.mono:
    mov r8d,1
.channel_ok:
    movzx ecx,word [rdi+14]
    xchg cl,ch
    cmp ecx,8
    je .depth_ok
    cmp ecx,16
    jne .fail
.depth_ok:
    mov r9d,ecx
    movzx ecx,word [rdi+16]
    xchg cl,ch
    cmp cx,0
    je .unsigned
    cmp cx,0xffff
    jne .fail
    mov r10d,1
    jmp .signed_ok
.unsigned:
    xor r10d,r10d
.signed_ok:
    cmp word [rdi+40],0 ; reserved compression must be zero
    jne .fail
    mov ecx,[rdi+22]
    bswap ecx
    and ecx,0x00ffffff
    jz .fail
    mov [rdx],ecx
    mov [rdx+4],r8d
    mov [rdx+8],r9d
    mov [rdx+12],r10d
    mov ecx,[rdi+26]
    bswap ecx
    test ecx,ecx
    jz .fail
    ; Sample units must divide evenly into complete frames.
    mov eax,ecx
    xor r11d,r11d
    ; Preserve metadata pointer in rdx: integer division requires edx.
    mov r10,rdx
    xor edx,edx
    div r8d
    test edx,edx
    jnz .fail
    mov [r10+16],rax
    ; required bytes = sample units * (bits/8), within supplied byte view
    mov eax,ecx
    mov r11d,r9d
    shr r11d,3
    imul rax,r11
    jo .fail
    sub rsi,128
    cmp rax,rsi
    ja .fail
    lea rax,[rdi+128]
    mov [r10+24],rax
    mov eax,1
    ret
.fail:
    xor eax,eax
    ret

; void convert(const uint8_t* src, float* dst, size_t samples,
;              uint32_t bits, uint32_t is_signed)
sauti_avr_pcm_to_f32_asm:
    test rdx,rdx
    jz .done
    cmp ecx,8
    jne .wordloop
.byteloop:
    movzx eax,byte [rdi]
    test r8d,r8d
    jz .byte_unsigned
    movsx eax,al
    jmp .byte_convert
.byte_unsigned:
    sub eax,128
.byte_convert:
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel scale8]
    movss [rsi],xmm0
    inc rdi
    add rsi,4
    dec rdx
    jnz .byteloop
    ret
.wordloop:
    movzx eax,word [rdi]
    rol ax,8
    test r8d,r8d
    jz .word_unsigned
    movsx eax,ax
    jmp .word_convert
.word_unsigned:
    sub eax,32768
.word_convert:
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel scale16]
    movss [rsi],xmm0
    add rdi,2
    add rsi,4
    dec rdx
    jnz .wordloop
.done:
    ret
