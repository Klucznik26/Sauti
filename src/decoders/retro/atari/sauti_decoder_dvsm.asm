; Atari Falcon WinRec DVSM unpacked PCM, NASM x86-64 SysV
default rel
global sauti_dvsm_pcm_f32_asm
section .rodata
s8: dd 0x3c000000
s16: dd 0x38000000
section .text
; void convert(const uint8_t *src,float *dst,size_t sample_count,uint32_t depth)
sauti_dvsm_pcm_f32_asm:
    test rdx,rdx
    jz .done
    cmp ecx,16
    je .word
.byte:
    movsx eax,byte [rdi]
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s8]
    movss [rsi],xmm0
    inc rdi
    add rsi,4
    dec rdx
    jnz .byte
    ret
.word:
    movzx eax,word [rdi]
    rol ax,8
    movsx eax,ax
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    add rdi,2
    add rsi,4
    dec rdx
    jnz .word
.done:
    ret

; Pack 2 delta mono: seed big-endian signed 16-bit, then table indices.
; void sauti_dvsm_delta_mono_asm(const uint8_t *input, float *out,
;                               size_t frames, const int16_t *steps)
; frames >= 1, input contains (2 + frames - 1) bytes.
global sauti_dvsm_delta_mono_asm
sauti_dvsm_delta_mono_asm:
    test rdx,rdx
    jz .end
    movzx eax,word [rdi]
    rol ax,8
    movsx eax,ax
    add rdi,2
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    add rsi,4
    dec rdx
    jz .end
.loop:
    movzx r9d,byte [rdi]
    movsx r9d,word [rcx+r9*2]
    add eax,r9d
    cmp eax,32767
    jle .mincheck
    mov eax,32767
.mincheck:
    cmp eax,-32768
    jge .emit
    mov eax,-32768
.emit:
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    inc rdi
    add rsi,4
    dec rdx
    jnz .loop
.end:
    ret

; Pack 2 stereo, one block:
; input = big-endian L/R 16-bit seeds followed by interleaved L/R delta bytes.
; void stereo_block(const uint8_t *src, float *dst, size_t frames,
;                   const int16_t *steps), frames >= 1.
global sauti_dvsm_delta_stereo_block_asm
sauti_dvsm_delta_stereo_block_asm:
    test rdx,rdx
    jz .end
    movzx eax,word [rdi]
    rol ax,8
    movsx eax,ax
    movzx r10d,word [rdi+2]
    rol r10w,8
    movsx r10d,r10w
    add rdi,4
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    cvtsi2ss xmm0,r10d
    mulss xmm0,[rel s16]
    movss [rsi+4],xmm0
    add rsi,8
    dec rdx
    jz .end
.loop:
    movzx r8d,byte [rdi]
    movsx r8d,word [rcx+r8*2]
    add eax,r8d
    cmp eax,32767
    jle .left_min
    mov eax,32767
.left_min:
    cmp eax,-32768
    jge .right
    mov eax,-32768
.right:
    movzx r9d,byte [rdi+1]
    movsx r9d,word [rcx+r9*2]
    add r10d,r9d
    cmp r10d,32767
    jle .right_min
    mov r10d,32767
.right_min:
    cmp r10d,-32768
    jge .emit
    mov r10d,-32768
.emit:
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    cvtsi2ss xmm0,r10d
    mulss xmm0,[rel s16]
    movss [rsi+4],xmm0
    add rdi,2
    add rsi,8
    dec rdx
    jnz .loop
.end:
    ret

; WinRec Pro pack 4 Voice: two signed 4-bit delta codes per byte, high first.
; Mono seed BE16 followed by packed nibbles. frames includes seed (max 1+2*bytes).
; void voice_mono(const uint8_t*,float*,size_t,const int16_t* table)
global sauti_dvsm_voice_mono_asm
sauti_dvsm_voice_mono_asm:
    test rdx,rdx
    jz .done
    movzx eax,word [rdi]
    rol ax,8
    movsx eax,ax
    add rdi,2
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    add rsi,4
    dec rdx
    jz .done
.loop:
    movzx r8d,byte [rdi]
    shr r8d,4
    movsx r8d,word [rcx+r8*2]
    add eax,r8d
    cmp eax,32767
    jle .lo1
    mov eax,32767
.lo1:
    cmp eax,-32768
    jge .emit1
    mov eax,-32768
.emit1:
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    add rsi,4
    dec rdx
    jz .done
    movzx r8d,byte [rdi]
    and r8d,15
    movsx r8d,word [rcx+r8*2]
    add eax,r8d
    cmp eax,32767
    jle .lo2
    mov eax,32767
.lo2:
    cmp eax,-32768
    jge .emit2
    mov eax,-32768
.emit2:
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    add rsi,4
    inc rdi
    dec rdx
    jnz .loop
.done:
    ret

; Stereo voice block: L/R BE16 seeds, then one byte per frame,
; high nibble -> left, low nibble -> right, independent saturating states.
global sauti_dvsm_voice_stereo_block_asm
sauti_dvsm_voice_stereo_block_asm:
    test rdx,rdx
    jz .done
    movzx eax,word [rdi]
    rol ax,8
    movsx eax,ax
    movzx r10d,word [rdi+2]
    rol r10w,8
    movsx r10d,r10w
    add rdi,4
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    cvtsi2ss xmm0,r10d
    mulss xmm0,[rel s16]
    movss [rsi+4],xmm0
    add rsi,8
    dec rdx
    jz .done
.loop:
    movzx r8d,byte [rdi]
    mov r9d,r8d
    shr r8d,4
    and r9d,15
    movsx r8d,word [rcx+r8*2]
    movsx r9d,word [rcx+r9*2]
    add eax,r8d
    cmp eax,32767
    jle .left_low
    mov eax,32767
.left_low:
    cmp eax,-32768
    jge .right
    mov eax,-32768
.right:
    add r10d,r9d
    cmp r10d,32767
    jle .right_low
    mov r10d,32767
.right_low:
    cmp r10d,-32768
    jge .emit
    mov r10d,-32768
.emit:
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel s16]
    movss [rsi],xmm0
    cvtsi2ss xmm0,r10d
    mulss xmm0,[rel s16]
    movss [rsi+4],xmm0
    inc rdi
    add rsi,8
    dec rdx
    jnz .loop
.done:
    ret
