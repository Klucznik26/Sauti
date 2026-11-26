#include "atari8_cpu.h"

static uint8_t rd(Atari8Cpu *c, uint16_t a)
{
    ++c->cycles;
    return c->read(c->bus, a);
}

static void wr(Atari8Cpu *c, uint16_t a, uint8_t v)
{
    ++c->cycles;
    c->write(c->bus, a, v);
}

static uint8_t fetch(Atari8Cpu *c)
{
    uint8_t v = rd(c, c->pc);
    ++c->pc;
    return v;
}

static uint16_t fetch16(Atari8Cpu *c)
{
    uint16_t lo = fetch(c);
    return (uint16_t)(lo | ((uint16_t)fetch(c) << 8));
}

static void nz(Atari8Cpu *c, uint8_t v)
{
    c->p = (uint8_t)((c->p & ~(ATARI8_CPU_N | ATARI8_CPU_Z))
                     | (v & 128u) | (v == 0 ? ATARI8_CPU_Z : 0));
}

static void flag(Atari8Cpu *c, uint8_t mask, bool on)
{
    if (on)
        c->p |= mask;
    else
        c->p &= (uint8_t)~mask;
}

/* NMOS decimal ADC: Z from binary sum, N/V from intermediate sum
 * before high-digit correction, C from final decimal correction.
 * Low digit carries at most once, including invalid BCD digits. */
static void adc(Atari8Cpu *c, uint8_t v)
{
    unsigned a = c->a;
    unsigned carry = (c->p & ATARI8_CPU_C) != 0;
    unsigned binary = a + v + carry;
    unsigned result = binary;
    if (c->p & ATARI8_CPU_D) {
        unsigned low = (a & 15u) + (v & 15u) + carry;
        if (low >= 10)
            low = ((low + 6u) & 15u) + 16u;
        result = (a & 0xf0u) + (v & 0xf0u) + low;
    }
    flag(c, ATARI8_CPU_Z, (uint8_t)binary == 0);
    flag(c, ATARI8_CPU_N, (result & 128u) != 0);
    flag(c, ATARI8_CPU_V, ((~(a ^ v) & (a ^ result)) & 128u) != 0);
    if ((c->p & ATARI8_CPU_D) && result >= 0xa0u)
        result += 0x60u;
    flag(c, ATARI8_CPU_C, result > 255u);
    c->a = (uint8_t)result;
}

/* NMOS decimal SBC: flags come from the binary subtraction, while
 * the stored accumulator receives the nibble corrections. */
static void sbc(Atari8Cpu *c, uint8_t v)
{
    int a = c->a;
    int borrow = (c->p & ATARI8_CPU_C) ? 0 : 1;
    int binary = a - v - borrow;
    uint8_t raw = (uint8_t)binary;
    flag(c, ATARI8_CPU_C, binary >= 0);
    flag(c, ATARI8_CPU_V, ((a ^ v) & (a ^ raw) & 128) != 0);
    nz(c, raw);
    if (c->p & ATARI8_CPU_D) {
        int low = (a & 15) - (v & 15) - borrow;
        int high = (a >> 4) - (v >> 4);
        if (low < 0) {
            low -= 6;
            --high;
        }
        if (high < 0)
            high -= 6;
        raw = (uint8_t)((high * 16) + ((unsigned)low & 15u));
    }
    c->a = raw;
}

static void compare(Atari8Cpu *c, uint8_t reg, uint8_t v)
{
    flag(c, ATARI8_CPU_C, reg >= v);
    nz(c, (uint8_t)(reg - v));
}

static void bit_test(Atari8Cpu *c, uint8_t v)
{
    flag(c, ATARI8_CPU_Z, !(c->a & v));
    flag(c, ATARI8_CPU_N, (v & 128u) != 0);
    flag(c, ATARI8_CPU_V, (v & 64u) != 0);
}

static void push(Atari8Cpu *c, uint8_t v)
{
    wr(c, (uint16_t)(0x100u | c->sp), v);
    --c->sp;
}

static uint8_t pull(Atari8Cpu *c)
{
    ++c->sp;
    return rd(c, (uint16_t)(0x100u | c->sp));
}

typedef enum { IMM, ZP, ZPX, ZPY, ABS, ABSX, ABSY, INDX, INDY } Mode;

static uint16_t zp16(Atari8Cpu *c, uint8_t a)
{
    uint16_t lo = rd(c, a);
    return (uint16_t)(lo | ((uint16_t)rd(c, (uint8_t)(a + 1u)) << 8));
}

static uint16_t indexed(Atari8Cpu *c, uint16_t base, uint8_t offset, bool store)
{
    uint16_t address = (uint16_t)(base + offset);
    if (store || ((address ^ base) & 0xff00u))
        (void)rd(c, (uint16_t)((base & 0xff00u) | (address & 255u)));
    return address;
}

static uint16_t address(Atari8Cpu *c, Mode mode, bool store)
{
    uint8_t z;
    uint16_t a;
    switch (mode) {
    case IMM: a = c->pc; ++c->pc; return a;
    case ZP: return fetch(c);
    case ZPX: case ZPY:
        z = fetch(c);
        (void)rd(c, z);
        return (uint8_t)(z + (mode == ZPX ? c->x : c->y));
    case ABS: return fetch16(c);
    case ABSX: case ABSY:
        a = fetch16(c);
        return indexed(c, a, mode == ABSX ? c->x : c->y, store);
    case INDX:
        z = fetch(c);
        (void)rd(c, z);
        return zp16(c, (uint8_t)(z + c->x));
    case INDY:
        z = fetch(c);
        a = zp16(c, z);
        return indexed(c, a, c->y, store);
    }
    return 0;
}

typedef enum { ASL, ROL, LSR, ROR, DEC, INC } Modify;

static uint8_t modify(Atari8Cpu *c, Modify kind, uint8_t v)
{
    unsigned carry = (c->p & ATARI8_CPU_C) != 0;
    switch (kind) {
    case ASL: flag(c, ATARI8_CPU_C, v & 128u); v = (uint8_t)(v << 1); break;
    case ROL: flag(c, ATARI8_CPU_C, v & 128u); v = (uint8_t)((v << 1) | carry); break;
    case LSR: flag(c, ATARI8_CPU_C, v & 1u); v >>= 1; break;
    case ROR: flag(c, ATARI8_CPU_C, v & 1u); v = (uint8_t)((v >> 1) | (carry << 7)); break;
    case DEC: --v; break;
    case INC: ++v; break;
    }
    nz(c, v);
    return v;
}

static void rmw(Atari8Cpu *c, Mode mode, Modify kind)
{
    uint16_t a = address(c, mode, true);
    uint8_t old = rd(c, a);
    /* NMOS bus writes the old value before the modified value. */
    wr(c, a, old);
    wr(c, a, modify(c, kind, old));
}

static void interrupt_vector(Atari8Cpu *c, uint16_t vector, bool brk)
{
    push(c, (uint8_t)(c->pc >> 8));
    push(c, (uint8_t)c->pc);
    push(c, (uint8_t)((c->p & ~ATARI8_CPU_B) | ATARI8_CPU_U |
                      (brk ? ATARI8_CPU_B : 0)));
    c->p |= ATARI8_CPU_I;
    uint16_t lo = rd(c, vector);
    c->pc = (uint16_t)(lo | ((uint16_t)rd(c, (uint16_t)(vector + 1u)) << 8));
}

void atari8_cpu_set_irq(Atari8Cpu *c, bool asserted)
{
    c->irq_line = asserted;
}

void atari8_cpu_set_nmi(Atari8Cpu *c, bool asserted)
{
    if (asserted && !c->nmi_line)
        c->nmi_pending = true;
    c->nmi_line = asserted;
}

static void branch(Atari8Cpu *c, bool take)
{
    unsigned raw = fetch(c);
    if (!take)
        return;
    uint16_t old = c->pc;
    int delta = raw < 128u ? (int)raw : (int)raw - 256;
    uint16_t dest = (uint16_t)(old + delta);
    (void)rd(c, old);
    if ((old ^ dest) & 0xff00u)
        (void)rd(c, (uint16_t)((old & 0xff00u) | (dest & 255u)));
    c->pc = dest;
}

void atari8_cpu_init(Atari8Cpu *c, Atari8CpuRead read, Atari8CpuWrite write,
                     void *bus, uint16_t entry)
{
    *c = (Atari8Cpu){0};
    c->read = read;
    c->write = write;
    c->bus = bus;
    c->pc = entry;
    c->sp = 0xfd;
    c->p = ATARI8_CPU_U | ATARI8_CPU_I;
}

unsigned atari8_cpu_step(Atari8Cpu *c)
{
    if (c->stopped)
        return 0;
    uint64_t before = c->cycles;
    /* Instruction-boundary IRQ sampling; exact CLI/SEI/PLP timing and
     * NMI vector hijacking are not modeled at this milestone. */
    if (c->nmi_pending || (c->irq_line && !(c->p & ATARI8_CPU_I))) {
        uint16_t vector = c->nmi_pending ? 0xfffa : 0xfffe;
        c->nmi_pending = false;
        (void)rd(c, c->pc);
        (void)rd(c, c->pc);
        interrupt_vector(c, vector, false);
        return (unsigned)(c->cycles - before);
    }
    uint8_t op = fetch(c);
    uint16_t a;
    uint8_t v;
    switch (op) {
    case 0xa9: c->a = rd(c, address(c, IMM, false)); nz(c, c->a); break;
    case 0xa5: c->a = rd(c, address(c, ZP, false)); nz(c, c->a); break;
    case 0xb5: c->a = rd(c, address(c, ZPX, false)); nz(c, c->a); break;
    case 0xad: c->a = rd(c, address(c, ABS, false)); nz(c, c->a); break;
    case 0xbd: c->a = rd(c, address(c, ABSX, false)); nz(c, c->a); break;
    case 0xb9: c->a = rd(c, address(c, ABSY, false)); nz(c, c->a); break;
    case 0xa1: c->a = rd(c, address(c, INDX, false)); nz(c, c->a); break;
    case 0xb1: c->a = rd(c, address(c, INDY, false)); nz(c, c->a); break;
    case 0xa2: c->x = rd(c, address(c, IMM, false)); nz(c, c->x); break;
    case 0xa6: c->x = rd(c, address(c, ZP, false)); nz(c, c->x); break;
    case 0xb6: c->x = rd(c, address(c, ZPY, false)); nz(c, c->x); break;
    case 0xae: c->x = rd(c, address(c, ABS, false)); nz(c, c->x); break;
    case 0xbe: c->x = rd(c, address(c, ABSY, false)); nz(c, c->x); break;
    case 0xa0: c->y = rd(c, address(c, IMM, false)); nz(c, c->y); break;
    case 0xa4: c->y = rd(c, address(c, ZP, false)); nz(c, c->y); break;
    case 0xb4: c->y = rd(c, address(c, ZPX, false)); nz(c, c->y); break;
    case 0xac: c->y = rd(c, address(c, ABS, false)); nz(c, c->y); break;
    case 0xbc: c->y = rd(c, address(c, ABSX, false)); nz(c, c->y); break;
    case 0x85: wr(c, address(c, ZP, true), c->a); break;
    case 0x95: wr(c, address(c, ZPX, true), c->a); break;
    case 0x8d: wr(c, address(c, ABS, true), c->a); break;
    case 0x9d: wr(c, address(c, ABSX, true), c->a); break;
    case 0x99: wr(c, address(c, ABSY, true), c->a); break;
    case 0x81: wr(c, address(c, INDX, true), c->a); break;
    case 0x91: wr(c, address(c, INDY, true), c->a); break;
    case 0x86: wr(c, address(c, ZP, true), c->x); break;
    case 0x96: wr(c, address(c, ZPY, true), c->x); break;
    case 0x8e: wr(c, address(c, ABS, true), c->x); break;
    case 0x84: wr(c, address(c, ZP, true), c->y); break;
    case 0x94: wr(c, address(c, ZPX, true), c->y); break;
    case 0x8c: wr(c, address(c, ABS, true), c->y); break;
    case 0xaa: (void)rd(c, c->pc); c->x = c->a; nz(c, c->x); break;
    case 0xa8: (void)rd(c, c->pc); c->y = c->a; nz(c, c->y); break;
    case 0x8a: (void)rd(c, c->pc); c->a = c->x; nz(c, c->a); break;
    case 0x98: (void)rd(c, c->pc); c->a = c->y; nz(c, c->a); break;
    case 0xba: (void)rd(c, c->pc); c->x = c->sp; nz(c, c->x); break;
    case 0x9a: (void)rd(c, c->pc); c->sp = c->x; break;
    case 0xe8: (void)rd(c, c->pc); ++c->x; nz(c, c->x); break;
    case 0xca: (void)rd(c, c->pc); --c->x; nz(c, c->x); break;
    case 0xc8: (void)rd(c, c->pc); ++c->y; nz(c, c->y); break;
    case 0x88: (void)rd(c, c->pc); --c->y; nz(c, c->y); break;
    case 0x18: (void)rd(c, c->pc); c->p &= (uint8_t)~ ATARI8_CPU_C; break;
    case 0x38: (void)rd(c, c->pc); c->p |= ATARI8_CPU_C; break;
    case 0x58: (void)rd(c, c->pc); c->p &= (uint8_t)~ ATARI8_CPU_I; break;
    case 0x78: (void)rd(c, c->pc); c->p |= ATARI8_CPU_I; break;
    case 0xd8: (void)rd(c, c->pc); c->p &= (uint8_t)~ ATARI8_CPU_D; break;
    case 0xf8: (void)rd(c, c->pc); c->p |= ATARI8_CPU_D; break;
    case 0xb8: (void)rd(c, c->pc); c->p &= (uint8_t)~ ATARI8_CPU_V; break;
    case 0x10: branch(c, (c->p & ATARI8_CPU_N) == 0); break;
    case 0x30: branch(c, (c->p & ATARI8_CPU_N) != 0); break;
    case 0x50: branch(c, (c->p & ATARI8_CPU_V) == 0); break;
    case 0x70: branch(c, (c->p & ATARI8_CPU_V) != 0); break;
    case 0x90: branch(c, (c->p & ATARI8_CPU_C) == 0); break;
    case 0xb0: branch(c, (c->p & ATARI8_CPU_C) != 0); break;
    case 0xd0: branch(c, (c->p & ATARI8_CPU_Z) == 0); break;
    case 0xf0: branch(c, (c->p & ATARI8_CPU_Z) != 0); break;
    case 0x09: c->a |= rd(c, address(c, IMM, false)); nz(c, c->a); break;
    case 0x05: c->a |= rd(c, address(c, ZP, false)); nz(c, c->a); break;
    case 0x15: c->a |= rd(c, address(c, ZPX, false)); nz(c, c->a); break;
    case 0x0d: c->a |= rd(c, address(c, ABS, false)); nz(c, c->a); break;
    case 0x1d: c->a |= rd(c, address(c, ABSX, false)); nz(c, c->a); break;
    case 0x19: c->a |= rd(c, address(c, ABSY, false)); nz(c, c->a); break;
    case 0x01: c->a |= rd(c, address(c, INDX, false)); nz(c, c->a); break;
    case 0x11: c->a |= rd(c, address(c, INDY, false)); nz(c, c->a); break;
    case 0x29: c->a &= rd(c, address(c, IMM, false)); nz(c, c->a); break;
    case 0x25: c->a &= rd(c, address(c, ZP, false)); nz(c, c->a); break;
    case 0x35: c->a &= rd(c, address(c, ZPX, false)); nz(c, c->a); break;
    case 0x2d: c->a &= rd(c, address(c, ABS, false)); nz(c, c->a); break;
    case 0x3d: c->a &= rd(c, address(c, ABSX, false)); nz(c, c->a); break;
    case 0x39: c->a &= rd(c, address(c, ABSY, false)); nz(c, c->a); break;
    case 0x21: c->a &= rd(c, address(c, INDX, false)); nz(c, c->a); break;
    case 0x31: c->a &= rd(c, address(c, INDY, false)); nz(c, c->a); break;
    case 0x49: c->a ^= rd(c, address(c, IMM, false)); nz(c, c->a); break;
    case 0x45: c->a ^= rd(c, address(c, ZP, false)); nz(c, c->a); break;
    case 0x55: c->a ^= rd(c, address(c, ZPX, false)); nz(c, c->a); break;
    case 0x4d: c->a ^= rd(c, address(c, ABS, false)); nz(c, c->a); break;
    case 0x5d: c->a ^= rd(c, address(c, ABSX, false)); nz(c, c->a); break;
    case 0x59: c->a ^= rd(c, address(c, ABSY, false)); nz(c, c->a); break;
    case 0x41: c->a ^= rd(c, address(c, INDX, false)); nz(c, c->a); break;
    case 0x51: c->a ^= rd(c, address(c, INDY, false)); nz(c, c->a); break;
    case 0x69: adc(c, rd(c, address(c, IMM, false))); break;
    case 0x65: adc(c, rd(c, address(c, ZP, false))); break;
    case 0x75: adc(c, rd(c, address(c, ZPX, false))); break;
    case 0x6d: adc(c, rd(c, address(c, ABS, false))); break;
    case 0x7d: adc(c, rd(c, address(c, ABSX, false))); break;
    case 0x79: adc(c, rd(c, address(c, ABSY, false))); break;
    case 0x61: adc(c, rd(c, address(c, INDX, false))); break;
    case 0x71: adc(c, rd(c, address(c, INDY, false))); break;
    case 0xc9: compare(c, c->a, rd(c, address(c, IMM, false))); break;
    case 0xc5: compare(c, c->a, rd(c, address(c, ZP, false))); break;
    case 0xd5: compare(c, c->a, rd(c, address(c, ZPX, false))); break;
    case 0xcd: compare(c, c->a, rd(c, address(c, ABS, false))); break;
    case 0xdd: compare(c, c->a, rd(c, address(c, ABSX, false))); break;
    case 0xd9: compare(c, c->a, rd(c, address(c, ABSY, false))); break;
    case 0xc1: compare(c, c->a, rd(c, address(c, INDX, false))); break;
    case 0xd1: compare(c, c->a, rd(c, address(c, INDY, false))); break;
    case 0xe9: sbc(c, rd(c, address(c, IMM, false))); break;
    case 0xe5: sbc(c, rd(c, address(c, ZP, false))); break;
    case 0xf5: sbc(c, rd(c, address(c, ZPX, false))); break;
    case 0xed: sbc(c, rd(c, address(c, ABS, false))); break;
    case 0xfd: sbc(c, rd(c, address(c, ABSX, false))); break;
    case 0xf9: sbc(c, rd(c, address(c, ABSY, false))); break;
    case 0xe1: sbc(c, rd(c, address(c, INDX, false))); break;
    case 0xf1: sbc(c, rd(c, address(c, INDY, false))); break;
    case 0xe0: compare(c, c->x, rd(c, address(c, IMM, false))); break;
    case 0xe4: compare(c, c->x, rd(c, address(c, ZP, false))); break;
    case 0xec: compare(c, c->x, rd(c, address(c, ABS, false))); break;
    case 0xc0: compare(c, c->y, rd(c, address(c, IMM, false))); break;
    case 0xc4: compare(c, c->y, rd(c, address(c, ZP, false))); break;
    case 0xcc: compare(c, c->y, rd(c, address(c, ABS, false))); break;
    case 0x24: bit_test(c, rd(c, address(c, ZP, false))); break;
    case 0x2c: bit_test(c, rd(c, address(c, ABS, false))); break;
    case 0x0a: (void)rd(c, c->pc); c->a = modify(c, ASL, c->a); break;
    case 0x06: rmw(c, ZP, ASL); break;
    case 0x16: rmw(c, ZPX, ASL); break;
    case 0x0e: rmw(c, ABS, ASL); break;
    case 0x1e: rmw(c, ABSX, ASL); break;
    case 0x2a: (void)rd(c, c->pc); c->a = modify(c, ROL, c->a); break;
    case 0x26: rmw(c, ZP, ROL); break;
    case 0x36: rmw(c, ZPX, ROL); break;
    case 0x2e: rmw(c, ABS, ROL); break;
    case 0x3e: rmw(c, ABSX, ROL); break;
    case 0x4a: (void)rd(c, c->pc); c->a = modify(c, LSR, c->a); break;
    case 0x46: rmw(c, ZP, LSR); break;
    case 0x56: rmw(c, ZPX, LSR); break;
    case 0x4e: rmw(c, ABS, LSR); break;
    case 0x5e: rmw(c, ABSX, LSR); break;
    case 0x6a: (void)rd(c, c->pc); c->a = modify(c, ROR, c->a); break;
    case 0x66: rmw(c, ZP, ROR); break;
    case 0x76: rmw(c, ZPX, ROR); break;
    case 0x6e: rmw(c, ABS, ROR); break;
    case 0x7e: rmw(c, ABSX, ROR); break;
    case 0xc6: rmw(c, ZP, DEC); break;
    case 0xd6: rmw(c, ZPX, DEC); break;
    case 0xce: rmw(c, ABS, DEC); break;
    case 0xde: rmw(c, ABSX, DEC); break;
    case 0xe6: rmw(c, ZP, INC); break;
    case 0xf6: rmw(c, ZPX, INC); break;
    case 0xee: rmw(c, ABS, INC); break;
    case 0xfe: rmw(c, ABSX, INC); break;
    case 0x00:
        (void)fetch(c); /* BRK padding byte */
        interrupt_vector(c, 0xfffe, true);
        break;
    case 0x40:
        (void)rd(c, c->pc);
        (void)rd(c, (uint16_t)(0x100u | c->sp));
        c->p = (uint8_t)((pull(c) & ~ATARI8_CPU_B) | ATARI8_CPU_U);
        a = pull(c);
        c->pc = (uint16_t)(a | ((uint16_t)pull(c) << 8));
        break;
    case 0xea: (void)rd(c, c->pc); break;
    case 0x4c: c->pc = fetch16(c); break;
    case 0x6c:
        a = fetch16(c);
        v = rd(c, a);
        c->pc = (uint16_t)(v | ((uint16_t)rd(c,
            (uint16_t)((a & 0xff00u) | ((a + 1u) & 255u))) << 8));
        break;
    case 0x20:
        v = fetch(c);
        (void)rd(c, (uint16_t)(0x100u | c->sp));
        push(c, (uint8_t)(c->pc >> 8));
        push(c, (uint8_t)c->pc);
        c->pc = (uint16_t)(v | ((uint16_t)fetch(c) << 8));
        break;
    case 0x60:
        (void)rd(c, c->pc);
        (void)rd(c, (uint16_t)(0x100u | c->sp));
        a = pull(c);
        a |= (uint16_t)((uint16_t)pull(c) << 8);
        (void)rd(c, a);
        c->pc = (uint16_t)(a + 1u);
        break;
    case 0x48: case 0x08:
        (void)rd(c, c->pc);
        push(c, op == 0x48 ? c->a : (uint8_t)(c->p | ATARI8_CPU_B | ATARI8_CPU_U));
        break;
    case 0x68: case 0x28:
        (void)rd(c, c->pc);
        (void)rd(c, (uint16_t)(0x100u | c->sp));
        v = pull(c);
        if (op == 0x68) { c->a = v; nz(c, v); }
        else c->p = (uint8_t)((v & ~ATARI8_CPU_B) | ATARI8_CPU_U);
        break;
    default:
        --c->pc;
        c->bad_opcode = op;
        c->stopped = true;
        return 0;
    }
    return (unsigned)(c->cycles - before);
}
