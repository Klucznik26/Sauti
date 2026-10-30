; Experimental YM2149 square tone + 17-bit noise, x86-64 SysV NASM.
; Experimental tone/noise/envelope: not cycle-accurate.
default rel
global sauti_ym_tone_step_asm
global sauti_ym_noise_step_asm
section .rodata
; 32 YM2149 amplitudes from emu2149 (Mitsutaka Okazaki).
; Values rescaled by 1/(3*255) to normalize three summed voices.
ym_dac: db 0,1,1,2,2,3,3,4,5,6,7,9,11,13,15,18
        db 22,26,31,37,45,53,63,76,90,106,127,151,180,214,255,255
%ifdef SAUTI_YM_REFERENCE_DAC
; Experimental ST-Sound volume curve (16 entries expanded to 32).
; For diagnostics only: ST-Sound filtering/DC removal is NOT reproduced.
ref_dac: dw 62,62,161,161,265,265,377,377
         dw 580,580,774,774,1155,1155,1575,1575
         dw 2260,2260,3088,3088,4570,4570,6233,6233
         dw 9330,9330,13187,13187,21220,21220,32767,32767
ref_gain: dd 0x372aac00 ; approx 1/(3*32767), diagnostic scale
%else
gain: dd 0x3aab5601 ; ~1/765
%endif
section .text

; uint32_t noise_step(uint32_t *phase_q16, uint32_t *lfsr,
;                     uint32_t step_q16, uint32_t period)
; period in [1,31]. Packed state: bits 0..16 = 17-bit LFSR,
; bit 17 = output inversion flag (0 => high, 1 => low).
; ST-Sound reference: feedback = bit0 XOR bit2; on feedback=0,
; toggle currentNoise. Initial packed state 1 means high output.
sauti_ym_noise_step_asm:
    test ecx,ecx
    jnz .noise_period
    mov ecx,1
.noise_period:
    shl rcx,16
    mov eax,[rdi]
    add rax,rdx
    mov r8d,[rsi]
    test r8d,0x1ffff
    jnz .noise_loop
    or r8d,1
.noise_loop:
    cmp rax,rcx
    jb .noise_done
    sub rax,rcx
    mov r9d,r8d
    shr r9d,2
    xor r9d,r8d
    and r9d,1
    mov r10d,r8d
    and r10d,0x20000
    and r8d,0x1ffff
    shr r8d,1
    shl r9d,16
    or r8d,r9d
    or r8d,r10d
    test r9d,r9d
    jnz .noise_loop
    xor r8d,0x20000
    jmp .noise_loop
.noise_done:
    mov [rdi],eax
    mov [rsi],r8d
    mov eax,r8d
    shr eax,17
    xor eax,1
    ret

; void tone_step(regs, phase3, master_clock_div8_q16, out, noise_bit)
; Each channel's output gate = (tone_disabled || tone_on) &&
;                               (noise_disabled || noise_bit).
; Preserve original provisional bipolar output normalization.
sauti_ym_tone_step_asm:
    push r12
    push r13
    mov r13d,r9d
    mov r12d,r8d
    mov r11d,edx
    xor r8d,r8d
    xorps xmm1,xmm1
.ch:
    movzx eax,byte [rdi+r8*2]
    movzx r9d,byte [rdi+r8*2+1]
    and r9d,15
    shl r9d,8
    or eax,r9d
    test eax,eax
    jnz .have_period
    mov eax,1
.have_period:
    ; ST-Sound: 32-bit free-running phase, with a period-dependent
    ; increment. Unlike period-modulo counters, register writes do not
    ; reinterpret or reset the accumulated phase.
    mov r10d,eax
    mov eax,r11d
    shl rax,15
    xor edx,edx
    div r10
    mov r10d,eax               ; increment per sample
    mov eax,[rsi+r8*4]        ; phase BEFORE increment
    add dword [rsi+r8*4],r10d
    movzx r9d,byte [rdi+r8+8]
    test r9d,16
    jz .fixed_volume
    mov r9d,r13d
    and r9d,31
    jmp .volume_ready
.fixed_volume:
    and r9d,15
    test r9d,r9d
    jz .next
    lea r9d,[r9*2+1]
.volume_ready:
%ifdef SAUTI_YM_REFERENCE_DAC
    lea rdx,[rel ref_dac]
    movzx r9d,word [rdx+r9*2]
    cvtsi2ss xmm0,r9d
    mulss xmm0,[rel ref_gain]
%else
    lea rdx,[rel ym_dac]
    movzx r9d,byte [rdx+r9]
    test r9d,r9d
    jz .next
    cvtsi2ss xmm0,r9d
    mulss xmm0,[rel gain]
%endif
    movzx edx,byte [rdi+7]
    bt edx,r8d
    jc .tone_ok
    test eax,0x80000000
    jnz .negative
.tone_ok:
    lea r9d,[r8+3]
    bt edx,r9d
    jc .positive
    test r12d,r12d
    jz .negative
.positive:
    addss xmm1,xmm0
    jmp .next
.negative:
    subss xmm1,xmm0
.next:
    inc r8d
    cmp r8d,3
    jne .ch
    movss [rcx],xmm1
    pop r13
    pop r12
    ret

; YM2149 32-level envelope (YM, not 16-level AY).
; struct Env { uint32_t phase, index, attack, alternate, hold, holding; }
; void env_init(Env *state, uint32_t shape)
global sauti_ym_env_init_asm
sauti_ym_env_init_asm:
    mov dword [rdi],0
    mov dword [rdi+4],31
    mov eax,esi
    and eax,15
    mov ecx,eax
    shr ecx,2
    and ecx,1
    neg ecx
    and ecx,31
    mov [rdi+8],ecx
    test eax,8
    jnz .cont
    mov eax,[rdi+8]
    shr eax,4
    and eax,1
    mov [rdi+12],eax
    mov dword [rdi+16],1
    jmp .finish
.cont:
    mov ecx,eax
    shr ecx,1
    and ecx,1
    mov [rdi+12],ecx
    and eax,1
    mov [rdi+16],eax
.finish:
    mov dword [rdi+20],0
    ret

; uint32_t env_step(Env *state, uint32_t step_q16, uint32_t period)
; Each decrement tick is (period * 65536), 32 steps/cycle.
global sauti_ym_env_step_asm
sauti_ym_env_step_asm:
    test edx,edx
    jnz .period
    mov edx,1
.period:
    mov rcx,rdx
    shl rcx,16
%ifdef SAUTI_YM_ENV_SAMPLE_BEFORE_TICK
    ; A/B only: return the level sampled before updating state.
    ; Do not alter the production step path or accumulated phase.
    mov r9d,[rdi+4]
    xor r9d,[rdi+8]
%endif
    mov eax,[rdi]
    add rax,rsi
.tick:
    cmp rax,rcx
    jb .save
    sub rax,rcx
    cmp dword [rdi+20],0
    jne .tick
    mov r8d,[rdi+4]
    test r8d,r8d
    jnz .decrement
    cmp dword [rdi+16],0
    je .repeat
    mov dword [rdi+20],1
    cmp dword [rdi+12],0
    je .held
    xor dword [rdi+8],31
.held:
    mov dword [rdi+4],0
    jmp .tick
.repeat:
    cmp dword [rdi+12],0
    je .noalternate
    xor dword [rdi+8],31
.noalternate:
    mov dword [rdi+4],31
    jmp .tick
.decrement:
    dec dword [rdi+4]
    jmp .tick
.save:
    mov [rdi],eax
%ifdef SAUTI_YM_ENV_SAMPLE_BEFORE_TICK
    mov eax,r9d
%else
    mov edx,[rdi+4]
    xor edx,[rdi+8]
    mov eax,edx
%endif
    ret

; Convert one embedded YM digi-drum sample to a normalized mono contribution.
; float sample(const uint8_t *byte, uint32_t attributes)
; attr bit 2: unsigned ST 4-bit (0..15); bit 1: signed 8-bit.
; A single drum occupies one of the three YM voices.
section .rodata
drum_gain8: dd 0.002604166666666667
drum_gain4: dd 0.041666666666666664
section .text
global sauti_ym_drum_sample_asm
sauti_ym_drum_sample_asm:
    movzx eax, byte [rdi]
    test esi,4
    jz .eight
    and eax,15
    lea eax,[rax*2-15]
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel drum_gain4]
    ret
.eight:
    test esi,2
    jz .unsigned
    movsx eax,byte [rdi]
    jmp .scaled
.unsigned:
    sub eax,128
.scaled:
    cvtsi2ss xmm0,eax
    mulss xmm0,[rel drum_gain8]
    ret
