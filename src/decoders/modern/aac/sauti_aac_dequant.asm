; AAC inverse quantization finishing kernel, original x86-64 NASM.
; SysV: rdi=q, rsi=positive_pow43, rdx=scale, rcx=dst, r8=count
; All values in positive_pow43 are nonnegative. Apply sign(q) and gain.
default rel
global sauti_aac_dequant_scale_asm
section .text
sauti_aac_dequant_scale_asm:
    xor eax,eax
    test rdi,rdi
    jz .done
    test rsi,rsi
    jz .done
    test rdx,rdx
    jz .done
    test rcx,rcx
    jz .done
    test r8,r8
    jz .done
    cmp r8,1024
    ja .done
    movss xmm1,[rdx]
    xor r9d,r9d
.loop:
    movss xmm0,[rsi+r9*4]
    mulss xmm0,xmm1
    mov edx,[rdi+r9*4]
    test edx,edx
    jns .store
    xorps xmm0,[rel sign_mask]
.store:
    movss [rcx+r9*4],xmm0
    inc r9
    cmp r9,r8
    jb .loop
    mov eax,1
.done:
    ret
section .rodata align=16
sign_mask: dd 0x80000000,0,0,0
