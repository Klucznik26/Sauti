; AAC-family bit reader and ADTS AAC-LC frame header, hand-written NASM.
; No external libraries, no allocation, no OS calls, x86-64 System V ABI.
; Does NOT decompress AAC (LC/SBR/PS DSP is separate future work).
default rel

global sauti_aac_adts_parse_asm
global sauti_aac_bits_read_asm

section .rodata
align 4
sauti_aac_rates:
    dd 96000, 88200, 64000, 48000, 44100, 32000, 24000
    dd 22050, 16000, 12000, 11025, 8000, 7350

section .text
; int parse(const u8* bytes, size_t size, SautiAacAdts* out)
; Output: eight uint32_t fields; pointer validation precedes every load.
sauti_aac_adts_parse_asm:
    xor eax,eax
    test rdi,rdi
    jz .fail
    test rdx,rdx
    jz .fail
    cmp rsi,7
    jb .fail
    cmp byte [rdi],0xff
    jne .fail
    movzx ecx,byte [rdi+1]
    mov r8d,ecx
    and r8d,0xf6            ; sync continuation, layer=00, MPEG ID ignored
    cmp r8d,0xf0
    jne .fail
    mov r9d,7
    test cl,1               ; protection_absent
    jnz .header_ok
    mov r9d,9               ; 16-bit CRC follows fixed 7-byte header
.header_ok:
    cmp rsi,r9
    jb .fail
    movzx ecx,byte [rdi+2]
    mov r8d,ecx
    shr r8d,6               ; profile = 1 means AAC-LC (AOT 2)
    cmp r8d,1
    jne .fail
    mov r10d,ecx
    shr r10d,2
    and r10d,15             ; sampling_frequency_index
    cmp r10d,12             ; 13-15 are reserved/escape for ADTS
    ja .fail
    lea rax,[rel sauti_aac_rates]
    mov r10d,[rax+r10*4]
    mov r8d,ecx
    and r8d,1
    shl r8d,2
    movzx eax,byte [rdi+3]
    shr eax,6
    or r8d,eax              ; channel_configuration, core channels only
    cmp r8d,1               ; PCE configuration 0 postponed
    jb .fail
    cmp r8d,2               ; current decoder design mono and stereo
    ja .fail
    movzx eax,byte [rdi+3]
    and eax,3
    shl eax,11
    movzx ecx,byte [rdi+4]
    shl ecx,3
    or eax,ecx
    movzx ecx,byte [rdi+5]
    shr ecx,5
    or eax,ecx             ; aac_frame_length, 13 bits
    cmp eax,r9d            ; frame must contain full header
    jb .fail
    cmp rax,rsi            ; no partial frames accepted
    ja .fail
    mov r11d,eax
    movzx ecx,byte [rdi+6]
    and ecx,3
    inc ecx                ; 1-4 raw_data_blocks
    mov [rdx+20],ecx
    movzx ecx,byte [rdi+1]
    shr ecx,3
    and ecx,1
    mov [rdx+24],ecx      ; MPEG-2 ADTS ID bit
    movzx ecx,byte [rdi+1]
    not ecx
    and ecx,1
    mov [rdx+28],ecx      ; crc_present
    mov [rdx+0],r10d
    mov [rdx+4],r8d
    mov dword [rdx+8],2
    mov [rdx+12],r11d
    mov [rdx+16],r9d
    mov eax,1
    ret
.fail:
    xor eax,eax               ; success must never leak through error paths
    ret

; int read_bits(const u8 *data, size_t bytes, size_t *bit_offset,
;               unsigned count (1..32), uint32_t *value)
; MSB-first ISO bit order; updates both output parameters only on success.
sauti_aac_bits_read_asm:
    xor eax,eax
    test rdi,rdi
    jz .read_fail
    test rdx,rdx
    jz .read_fail
    test r8,r8
    jz .read_fail
    test ecx,ecx
    jz .read_fail
    cmp ecx,32
    ja .read_fail
    mov rax,0x1fffffffffffffff ; prevent 8*size overflow
    cmp rsi,rax
    ja .read_fail
    lea rax,[rsi*8]
    mov r10,[rdx]
    cmp r10,rax
    ja .read_fail
    sub rax,r10
    cmp rax,rcx
    jb .read_fail
    mov r11d,ecx
    xor r9d,r9d
.read_loop:
    mov rax,r10
    shr rax,3
    movzx eax,byte [rdi+rax]
    mov ecx,r10d
    and ecx,7
    mov esi,7
    sub esi,ecx
    mov ecx,esi
    shr eax,cl
    and eax,1
    shl r9d,1
    or r9d,eax
    inc r10
    dec r11d
    jnz .read_loop
    mov [r8],r9d
    mov [rdx],r10
    mov eax,1
    ret
.read_fail:
    xor eax,eax
    ret
