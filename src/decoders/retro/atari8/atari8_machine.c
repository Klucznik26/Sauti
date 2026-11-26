#include "atari8_machine.h"
#include <string.h>

static bool io_address(uint16_t a)
{
    return a >= 0xd000u && a < 0xd800u;
}

static void sync_irq(Atari8Machine *m)
{
    atari8_cpu_set_irq(&m->cpu, atari8_core_irq(&m->core));
}

static void tick(Atari8Machine *m)
{
    uint8_t levels[2];
    int16_t frame[2];
    atari8_core_levels(&m->core, levels);
    bool ready = atari8_pcm_feed(&m->pcm, levels, frame);
    atari8_core_step(&m->core, 1);
    ++m->master_cycles;
    sync_irq(m);
    if (ready) {
        ++m->frames;
        if (m->sink)
            m->sink(m->sink_context, frame);
    }
}

static void io_fault(Atari8Machine *m, uint16_t a, bool writing)
{
    m->status = ATARI8_MACHINE_IO;
    m->fault_address = a;
    m->fault_write = writing;
    m->fault_value = 0;
}

/* Sample/read or commit/write at the end of the modeled bus cycle.
 * Every real or dummy CPU access advances POKEY and the PCM integrator. */
static uint8_t bus_read(void *context, uint16_t a)
{
    Atari8Machine *m = context;
    tick(m);
    if (m->status != ATARI8_MACHINE_OK)
        return 0xff;
    if (!io_address(a))
        return m->ram[a];
    if ((a & 0xff00u) == 0xd000u) {
        unsigned reg = a & 31u;
        if (reg < 16) return 0; /* no video collisions in headless mode */
        if (reg < 20) return 1; /* released joystick triggers */
        if (reg == 20) return m->ntsc ? 15 : 1;
        if (reg == 31) return (uint8_t)(15u & ~m->console_latch);
        return 0; /* unused GTIA read bits */
    }
    if ((a & 0xff00u) == 0xd400u) {
        unsigned reg = a & 15u;
        if (reg == 11)
            return (uint8_t)((m->master_cycles / 114u %
                              (m->ntsc ? 262u : 312u)) / 2u);
        if (reg == 10 || reg == 14)
            return 0xff; /* write-only registers, idle bus approximation */
    }
    /* XL-style bus pullups, with no PBI device or cartridge attached.
     * This is not the floating-bus behavior of every Atari model. */
    if ((a & 0xff00u) == 0xd100u || a >= 0xd500u)
        return 0xff;
    uint8_t value;
    if (atari8_core_read(&m->core, a, &value))
        return value;
    io_fault(m, a, false);
    return 0xff;
}

static void bus_write(void *context, uint16_t a, uint8_t value)
{
    Atari8Machine *m = context;
    tick(m);
    if (m->status != ATARI8_MACHINE_OK)
        return;
    if (!io_address(a)) {
        m->ram[a] = value;
        return;
    }
    if ((a & 0xff00u) == 0xd000u) {
        unsigned reg = a & 31u;
        /* No display or collision hardware in this music runtime. */
        if (reg == 31) m->console_latch = value & 15u;
        return;
    }
    if ((a & 0xff00u) == 0xd400u) {
        unsigned reg = a & 15u;
        if (reg == 10) {
            /* Approximate WSYNC: suspend the CPU until the next
             * scanline boundary, keeping audio/master time running. */
            unsigned wait = 114u - (unsigned)(m->master_cycles % 114u);
            while (wait--) tick(m);
            return;
        }
        if (reg == 11) return; /* VCOUNT is read-only */
        if (reg == 14 && !(value & 0xc0u)) return; /* NMI disabled */
    }
    if ((a & 0xff00u) == 0xd100u || a >= 0xd500u)
        return; /* no expansion device to receive the write */
    if (!atari8_core_write(&m->core, a, value)) {
        io_fault(m, a, true);
        m->fault_value = value;
        return;
    }
    sync_irq(m);
}

bool atari8_machine_init(Atari8Machine *m, bool ntsc, bool stereo,
                         uint32_t rate, bool dc_block,
                         Atari8MachineSink sink, void *context)
{
    if (!m)
        return false;
    memset(m, 0, sizeof(*m));
    atari8_core_reset(&m->core, ntsc, stereo);
    if (!atari8_pcm_init(&m->pcm, m->core.pokey[0].clock_hz, rate,
                         stereo ? 2u : 1u, dc_block)) {
        m->status = ATARI8_MACHINE_BAD_ARGUMENT;
        return false;
    }
    m->ntsc = ntsc;
    m->console_latch = 8; /* OS-style idle console */
    m->sink = sink;
    m->sink_context = context;
    atari8_cpu_init(&m->cpu, bus_read, bus_write, m, 0);
    return true;
}

bool atari8_machine_load(Atari8Machine *m, uint16_t address,
                         const uint8_t *data, size_t size)
{
    if (!m || m->status != ATARI8_MACHINE_OK ||
        (!data && size) || size > 65536u - (size_t)address)
        return false;
    if (size && address < 0xd800u && (size_t)address + size > 0xd000u)
        return false;
    if (size)
        memmove(m->ram + address, data, size);
    return true;
}

Atari8MachineResult atari8_machine_step(Atari8Machine *m)
{
    if (!m)
        return ATARI8_MACHINE_BAD_ARGUMENT;
    if (m->status != ATARI8_MACHINE_OK)
        return m->status;
    sync_irq(m);
    unsigned cycles = atari8_cpu_step(&m->cpu);
    if (!cycles && m->status == ATARI8_MACHINE_OK)
        m->status = ATARI8_MACHINE_CPU_STOP;
    return m->status;
}

Atari8MachineResult atari8_machine_idle(Atari8Machine *m, uint32_t cycles)
{
    if (!m)
        return ATARI8_MACHINE_BAD_ARGUMENT;
    if (m->status != ATARI8_MACHINE_OK)
        return m->status;
    while (cycles--)
        tick(m);
    return m->status;
}

Atari8MachineResult atari8_machine_call(Atari8Machine *m, uint16_t entry,
                                        uint64_t cycle_budget)
{
    if (!m)
        return ATARI8_MACHINE_BAD_ARGUMENT;
    if (m->status != ATARI8_MACHINE_OK)
        return m->status;
    if (!cycle_budget || entry == 0xffffu || io_address(entry))
        return ATARI8_MACHINE_BAD_ARGUMENT;
    if (m->cpu.stopped)
        return m->status = ATARI8_MACHINE_CPU_STOP;
    uint8_t sp = m->cpu.sp;
    /* RTS increments the pulled address: $FFFE -> $FFFF. */
    m->ram[0x100u | m->cpu.sp] = 0xff;
    --m->cpu.sp;
    m->ram[0x100u | m->cpu.sp] = 0xfe;
    --m->cpu.sp;
    m->cpu.pc = entry;
    uint64_t start = m->master_cycles;
    while (m->master_cycles - start < cycle_budget) {
        Atari8MachineResult r = atari8_machine_step(m);
        if (r != ATARI8_MACHINE_OK)
            return r;
        if (m->master_cycles - start > cycle_budget)
            break;
        if (m->cpu.pc == 0xffffu && m->cpu.sp == sp)
            return ATARI8_MACHINE_OK;
    }
    return m->status = ATARI8_MACHINE_BUDGET;
}
