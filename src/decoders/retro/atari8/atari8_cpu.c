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
