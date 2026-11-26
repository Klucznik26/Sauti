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

/* ARR rotates the AND result; decimal correction does not alter N/Z/V. */
static void arr(Atari8Cpu *c, uint8_t v)
{
    unsigned input = c->a & v;
    unsigned result = (input >> 1) | ((c->p & ATARI8_CPU_C) ? 128u : 0u);
    nz(c, (uint8_t)result);
    flag(c, ATARI8_CPU_V, ((result ^ (result << 1)) & 64u) != 0);
    if (c->p & ATARI8_CPU_D) {
        if ((input & 15u) + (input & 1u) > 5u)
            result = (result & 0xf0u) | ((result + 6u) & 15u);
        bool high = (input & 0xf0u) + (input & 16u) > 0x50u;
        flag(c, ATARI8_CPU_C, high);
        if (high)
            result += 0x60u;
    } else {
        flag(c, ATARI8_CPU_C, (result & 64u) != 0);
    }
    c->a = (uint8_t)result;
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

static uint8_t rmw(Atari8Cpu *c, Mode mode, Modify kind)
{
    uint16_t a = address(c, mode, true);
    uint8_t old = rd(c, a);
    /* NMOS bus writes the old value before the modified value. */
    wr(c, a, old);
    uint8_t v = modify(c, kind, old);
    wr(c, a, v);
    return v;
}

/* Stable undocumented RMW+ALU combinations use the same bus
 * sequence as a memory RMW. The second operation consumes no
 * additional bus cycle. */
static void compound(Atari8Cpu *c, Mode mode, Modify kind)
{
    uint8_t v = rmw(c, mode, kind);
    switch (kind) {
    case ASL: c->a |= v; nz(c, c->a); break;
    case ROL: c->a &= v; nz(c, c->a); break;
    case LSR: c->a ^= v; nz(c, c->a); break;
    case ROR: adc(c, v); break;
    case DEC: compare(c, c->a, v); break;
    case INC: sbc(c, v); break;
    }
}

static void lax(Atari8Cpu *c, Mode mode)
{
    c->a = c->x = rd(c, address(c, mode, false));
    nz(c, c->a);
}

/* Deterministic NMOS approximation for unstable high-byte stores.
 * The mask uses the unindexed high byte plus one. A page carry can
 * replace the destination high byte with the actual stored value. */
static void masked_store(Atari8Cpu *c, uint8_t op)
{
    uint16_t base;
    if (op == 0x93) {
        uint8_t z = fetch(c);
        base = zp16(c, z);
    } else {
        base = fetch16(c);
    }
    uint8_t offset = op == 0x9c ? c->x : c->y;
    uint16_t dest = indexed(c, base, offset, true);
    uint8_t source;
    if (op == 0x9c)
        source = c->y;
    else if (op == 0x9e)
        source = c->x;
    else {
        source = c->a & c->x;
        if (op == 0x9b)
            c->sp = source;
    }
    uint8_t value = source & (uint8_t)((base >> 8) + 1u);
    if ((base ^ dest) & 0xff00u)
        dest = (uint16_t)(((uint16_t)value << 8) | (dest & 255u));
    wr(c, dest, value);
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
    /* Stable undocumented NMOS opcodes. */
    case 0x03: compound(c, INDX, ASL); break;
    case 0x07: compound(c, ZP, ASL); break;
    case 0x0f: compound(c, ABS, ASL); break;
    case 0x13: compound(c, INDY, ASL); break;
    case 0x17: compound(c, ZPX, ASL); break;
    case 0x1b: compound(c, ABSY, ASL); break;
    case 0x1f: compound(c, ABSX, ASL); break;
    case 0x23: compound(c, INDX, ROL); break;
    case 0x27: compound(c, ZP, ROL); break;
    case 0x2f: compound(c, ABS, ROL); break;
    case 0x33: compound(c, INDY, ROL); break;
    case 0x37: compound(c, ZPX, ROL); break;
    case 0x3b: compound(c, ABSY, ROL); break;
    case 0x3f: compound(c, ABSX, ROL); break;
    case 0x43: compound(c, INDX, LSR); break;
    case 0x47: compound(c, ZP, LSR); break;
    case 0x4f: compound(c, ABS, LSR); break;
    case 0x53: compound(c, INDY, LSR); break;
    case 0x57: compound(c, ZPX, LSR); break;
    case 0x5b: compound(c, ABSY, LSR); break;
    case 0x5f: compound(c, ABSX, LSR); break;
    case 0x63: compound(c, INDX, ROR); break;
    case 0x67: compound(c, ZP, ROR); break;
    case 0x6f: compound(c, ABS, ROR); break;
    case 0x73: compound(c, INDY, ROR); break;
    case 0x77: compound(c, ZPX, ROR); break;
    case 0x7b: compound(c, ABSY, ROR); break;
    case 0x7f: compound(c, ABSX, ROR); break;
    case 0xc3: compound(c, INDX, DEC); break;
    case 0xc7: compound(c, ZP, DEC); break;
    case 0xcf: compound(c, ABS, DEC); break;
    case 0xd3: compound(c, INDY, DEC); break;
    case 0xd7: compound(c, ZPX, DEC); break;
    case 0xdb: compound(c, ABSY, DEC); break;
    case 0xdf: compound(c, ABSX, DEC); break;
    case 0xe3: compound(c, INDX, INC); break;
    case 0xe7: compound(c, ZP, INC); break;
    case 0xef: compound(c, ABS, INC); break;
    case 0xf3: compound(c, INDY, INC); break;
    case 0xf7: compound(c, ZPX, INC); break;
    case 0xfb: compound(c, ABSY, INC); break;
    case 0xff: compound(c, ABSX, INC); break;
    case 0xa3: lax(c, INDX); break;
    case 0xa7: lax(c, ZP); break;
    case 0xaf: lax(c, ABS); break;
    case 0xb3: lax(c, INDY); break;
    case 0xb7: lax(c, ZPY); break;
    case 0xbf: lax(c, ABSY); break;
    case 0x83: wr(c, address(c, INDX, true), (uint8_t)(c->a & c->x)); break;
    case 0x87: wr(c, address(c, ZP, true), (uint8_t)(c->a & c->x)); break;
    case 0x8f: wr(c, address(c, ABS, true), (uint8_t)(c->a & c->x)); break;
    case 0x97: wr(c, address(c, ZPY, true), (uint8_t)(c->a & c->x)); break;
    case 0x1a: case 0x3a: case 0x5a: case 0x7a: case 0xda: case 0xfa:
        (void)rd(c, c->pc); break;
    case 0x80: case 0x82: case 0x89: case 0xc2: case 0xe2:
        (void)rd(c, address(c, IMM, false)); break;
    case 0x04: case 0x44: case 0x64:
        (void)rd(c, address(c, ZP, false)); break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xd4: case 0xf4:
        (void)rd(c, address(c, ZPX, false)); break;
    case 0x0c:
        (void)rd(c, address(c, ABS, false)); break;
    case 0x1c: case 0x3c: case 0x5c: case 0x7c: case 0xdc: case 0xfc:
        (void)rd(c, address(c, ABSX, false)); break;
    case 0x0b: case 0x2b: /* ANC */
        c->a &= fetch(c);
        nz(c, c->a);
        flag(c, ATARI8_CPU_C, c->a & 128u);
        break;
    case 0x4b: /* ALR */
        c->a &= fetch(c);
        c->a = modify(c, LSR, c->a);
        break;
    case 0xcb: /* AXS/SBX: subtract without input carry or decimal mode */
        v = fetch(c);
        a = c->a & c->x;
        compare(c, (uint8_t)a, v);
        c->x = (uint8_t)(a - v);
        break;
    case 0xeb: sbc(c, fetch(c)); break;
    case 0x6b: arr(c, fetch(c)); break;
    case 0x8b: /* XAA/ANE: fixed magic constant, not silicon prediction */
        v = fetch(c);
        c->a = (uint8_t)((c->a | 0xeeu) & c->x & v);
        nz(c, c->a);
        break;
    case 0xab: /* LXA: same deterministic magic constant */
        v = fetch(c);
        c->a = c->x = (uint8_t)((c->a | 0xeeu) & v);
        nz(c, c->a);
        break;
    case 0xbb: /* LAS */
        v = rd(c, address(c, ABSY, false));
        c->a = c->x = c->sp = (uint8_t)(v & c->sp);
        nz(c, c->a);
        break;
    case 0x93: case 0x9f: case 0x9b: case 0x9c: case 0x9e:
        masked_store(c, op);
        break;
    case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52:
    case 0x62: case 0x72: case 0x92: case 0xb2: case 0xd2: case 0xf2:
        /* JAM: halt the player immediately. Not a pin-level lockup. */
        c->jammed = true;
        --c->pc;
        c->bad_opcode = op;
        c->stopped = true;
        return 0;
    default:
        --c->pc;
        c->bad_opcode = op;
        c->stopped = true;
        return 0;
    }
    return (unsigned)(c->cycles - before);
}
