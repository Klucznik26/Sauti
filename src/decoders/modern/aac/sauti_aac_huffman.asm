; Generic bounded Huffman prefix decode, original NASM x86-64.
; Each SautiAacHuffNode is 8 bytes: child0 u16, child1 u16,
; symbol s16, terminal u16. Graph must be built/validated by C builder.
; int decode(const uint8_t *data, size_t length, size_t *bit_offset,
;            const Node *nodes, size_t node_count, int32_t *out)
; MSB-first, all outputs stay unchanged upon failure. No allocations,
; no external runtime libraries and no platform calls.
default rel
global sauti_aac_huffman_decode_asm
section .text

sauti_aac_huffman_decode_asm:
    push rbx
    xor eax,eax
    test rdi,rdi
    jz .fail
    test rdx,rdx
    jz .fail
    test rcx,rcx
    jz .fail
    test r9,r9
    jz .fail
    cmp r8,1
    jbe .fail
    cmp r8,4096
    ja .fail
    mov rax,0x1fffffffffffffff
    cmp rsi,rax
    ja .fail
    shl rsi,3                   ; bits in the input buffer
    mov r11,[rdx]              ; local position, committed on success
    cmp r11,rsi
    jae .fail
    mov rbx,rcx                ; preserve node pointer across CL shifts
    xor r10d,r10d              ; root node index 0
.traverse:
    mov eax,r10d
    shl rax,3
    test word [rbx+rax+6],1    ; is_terminal
    jnz .found
    cmp r11,rsi
    jae .fail
    mov rax,r11
    shr rax,3
    movzx eax,byte [rdi+rax]  ; safe: bitpos < length*8
    mov ecx,r11d
    and ecx,7
    xor ecx,7                  ; bit position in byte (MSB-first)
    shr eax,cl
    and eax,1
    mov ecx,r10d
    shl rcx,3
    test eax,eax
    jnz .right
    movzx eax,word [rbx+rcx+0]
    jmp .next
.right:
    movzx eax,word [rbx+rcx+2]
.next:
    cmp eax,0xffff
    je .fail
    cmp rax,r8
    jae .fail
    mov r10d,eax
    inc r11
    jmp .traverse
.found:
    movsx eax,word [rbx+rax+4]
    mov [r9],eax
    mov [rdx],r11
    mov eax,1
    pop rbx
    ret
.fail:
    xor eax,eax
    pop rbx
    ret
