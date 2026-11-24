; Suma jednego glosu Pauli.  Bajty, czyszczenie prefiksu, maska EFx, glosnosc i panorama.
; RC, filtr LED i postep fazy zostaja w C.  Wynik doklada sie do acc[0] (lewy) i acc[1] (prawy).
; Dzielenie jest w strone zera, tak jak dzielenie calkowite w C.
;
; void maktabasauti_mod_voice_add(int32_t acc[2], const uint8_t *sample,
;     const uint8_t *flip, uint32_t idx, uint32_t length, uint32_t clear_n,
;     int32_t vol, uint32_t pan);
default rel
global maktabasauti_mod_voice_add
section .text
maktabasauti_mod_voice_add:
    test rsi, rsi
    jz .ret
    cmp ecx, r8d
    jae .ret
    mov r10, rdi
    mov r11, rdx
    xor eax, eax
    cmp ecx, r9d
    jb .gotb
    movzx eax, byte [rsi + rcx]
.gotb:
    test r11, r11
    jz .sx
    mov edx, ecx
    shr edx, 3
    movzx edx, byte [r11 + rdx]
    and ecx, 7
    mov r11d, 1
    shl r11d, cl
    test edx, r11d
    jz .sx
    xor eax, 0xFF
.sx:
    movsx rax, al
    movsxd rdx, dword [rsp + 8]
    imul rax, rdx
    mov r11, rax
    mov edx, 255
    sub edx, dword [rsp + 16]
    movsxd rdx, edx
    imul rax, rdx
    cqo
    mov rcx, 255
    idiv rcx
    add dword [r10], eax
    mov rax, r11
    movsxd rdx, dword [rsp + 16]
    imul rax, rdx
    cqo
    mov rcx, 255
    idiv rcx
    add dword [r10 + 4], eax
.ret:
    ret
