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
    /* CONSOL bit 3 drives the one-bit speaker (active low). D-only
     * experimental mix; relative analog gain needs reference testing.
     * Keep the PCM input within its documented 0..60 range. */
    if (m->sap_continuous && !(m->console_latch & 8u)) {
        for (unsigned ch=0; ch<(m->core.stereo ? 2u : 1u); ++ch) {
            unsigned level = levels[ch] + 8u;
            levels[ch] = (uint8_t)(level > 60u ? 60u : level);
        }
    }
    bool ready = atari8_pcm_feed(&m->pcm, levels, frame);
    atari8_core_step(&m->core, 1);
    ++m->master_cycles;
    /* Approximate VBI status at scanline 248; independent of NMIEN.
     * The SAP player profile deliberately has no ANTIC NMI delivery. */
    if (m->sap_host_scheduled &&
        m->master_cycles % ((m->ntsc ? 262u : 312u) * 114u) == 248u * 114u)
        m->antic_nmist = 0x40;
    sync_irq(m);
    if (ready) {
        ++m->frames;
        if (m->sink)
            m->sink(m->sink_context, frame);
    }
}

/* ANTIC with display DMA disabled still refreshes DRAM at cycles
 * 25,29,...,57 of each 114-cycle scanline. This first model stalls
 * both bus directions; real write-cycle deferral is not modeled.
 * Idle time and WSYNC already advance master time. */
static void cpu_bus_tick(Atari8Machine *m)
{
    if (m->sap_host_scheduled) {
        unsigned h = (unsigned)(m->master_cycles % 114u);
        if (h >= 25u && h <= 57u && (h - 25u) % 4u == 0) {
            tick(m);
            ++m->refresh_stalls;
        }
    }
    tick(m);
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
    cpu_bus_tick(m);
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
    if (m->sap_continuous) {
        switch (a & 0xff03u) {
        case 0xd300u:
            return (m->pia_pactl & 4u)
                ? (uint8_t)((m->pia_porta & m->pia_ddra) |
                            (uint8_t)~m->pia_ddra)
                : m->pia_ddra;
        case 0xd301u:
            /* Input pins are pulled high; bit 2 selects data vs DDR. */
            return (m->pia_pbctl & 4u)
                ? (uint8_t)((m->pia_portb & m->pia_ddrb) |
                            (uint8_t)~m->pia_ddrb)
                : m->pia_ddrb;
        case 0xd302u: return m->pia_pactl;
        case 0xd303u: return m->pia_pbctl;
        default: break;
        }
    }
    if ((a & 0xff00u) == 0xd400u) {
        unsigned reg = a & 15u;
        if (m->sap_continuous && reg<=9u)
            return 0xff; /* write-only video registers and unused 6/8 */
        if (reg == 11)
            return (uint8_t)((m->master_cycles / 114u %
                              (m->ntsc ? 262u : 312u)) / 2u);
        if (reg == 15 && m->sap_host_scheduled)
            return (uint8_t)(m->antic_nmist | 0x1fu);
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
    cpu_bus_tick(m);
    if (m->status != ATARI8_MACHINE_OK)
        return;
    if (!io_address(a)) {
        m->ram[a] = value;
        return;
    }
    if ((a & 0xff00u) == 0xd000u) {
        unsigned reg = a & 31u;
        /* No display or collision hardware in this music runtime. */
        if (reg == 31) {
            if (m->sap_continuous) {
                ++m->sap_consol_writes;
                if ((m->console_latch ^ value) & 8u) ++m->sap_consol_edges;
            }
            m->console_latch = value & 15u;
        }
        return;
    }
    if (m->sap_continuous && (a & 0xff03u)==0xd300u) {
        if (m->pia_pactl & 4u) m->pia_porta = value;
        else m->pia_ddra = value;
        return;
    }
    if (m->sap_continuous && (a & 0xff03u)==0xd301u) {
        /* SAP has a flat 64 KiB RAM map, not XL/XE ROMs or extra banks.
         * Preserve PIA state without remapping the guest music image. */
        if (m->pia_pbctl & 4u) m->pia_portb = value;
        else m->pia_ddrb = value;
        return;
    }
    if (m->sap_continuous && (a & 0xff03u)==0xd303u) {
        if (!(value & 1u) && ((value & 0x20u) || !(value & 8u))) {
            m->pia_pbctl = value & 0x3fu;
            return;
        }
    }
    if (m->sap_continuous && (a & 0xff03u)==0xd302u) {
        /* Headless port A, no attached peripheral. Support control
         * writes without enabled CA1/CA2 interrupts. */
        if (!(value & 1u) && ((value & 0x20u) || !(value & 8u))) {
            m->pia_pactl = value & 0x3fu;
            return;
        }
    }
    if ((a & 0xff00u) == 0xd400u) {
        unsigned reg = a & 15u;
        /* SAP timing assumes graphics DMA disabled, even when a rip
         * carries original video setup. Record requests explicitly;
         * do not run a display list or invent video bus stalls. */
        if (m->sap_continuous &&
            (reg<=5u || reg==7u || reg==9u)) {
            m->antic_video[reg]=value;
            if (reg==0) {
                m->antic_dmactl = value & 0x3fu;
                if (value & 0x2cu) ++m->sap_video_dma_requests;
            }
            return;
        }
        if (reg == 10) {
            /* Approximate WSYNC: suspend the CPU until the next
             * scanline boundary, keeping audio/master time running. */
            unsigned wait = 114u - (unsigned)(m->master_cycles % 114u);
            while (wait--) tick(m);
            return;
        }
        if (reg == 11) return; /* VCOUNT is read-only */
        if (reg == 14 && (m->sap_host_scheduled || !(value & 0xc0u))) {
            /* SAP B/C use host calls, not ANTIC NMI dispatch.
             * Strict machine mode still rejects active ANTIC sources. */
            m->antic_nmien = value & 0xc0u;
            return;
        }
        if (reg == 15 && m->sap_host_scheduled) {
            m->antic_nmist = 0;
            return;
        }
    }
    if ((a & 0xff00u) == 0xd100u || a >= 0xd500u)
        return; /* no expansion device to receive the write */
    if (m->sap_continuous && (a & 0xff00u)==0xd200u &&
        (a & 15u)<=8u)
        ++m->sap_pokey_audio_writes;
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
    /* Virtual XL-style port B with all RAM visible and no peripherals. */
    m->pia_pactl = 0x3c;
    m->pia_porta = 0xff;
    m->pia_pbctl = 0x3c;
    m->pia_ddrb = 0xff;
    m->pia_portb = 0xfe;
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
    Atari8Trace *t = &m->trace[m->trace_next];
    *t = (Atari8Trace){
        .pc = m->cpu.pc, .opcode = m->ram[m->cpu.pc],
        .operand1 = m->ram[(uint16_t)(m->cpu.pc + 1u)],
        .operand2 = m->ram[(uint16_t)(m->cpu.pc + 2u)],
        .a = m->cpu.a, .x = m->cpu.x, .y = m->cpu.y,
        .sp = m->cpu.sp, .p = m->cpu.p,
        .irq = m->cpu.irq_line, .nmi = m->cpu.nmi_pending,
        .irq_vector = (uint16_t)(m->ram[0xfffe] | ((uint16_t)m->ram[0xffff] << 8)),
        .cycle = m->master_cycles
    };
    m->trace_next = (m->trace_next + 1u) % ATARI8_TRACE_SIZE;
    if (m->trace_count < ATARI8_TRACE_SIZE) ++m->trace_count;
    unsigned cycles = atari8_cpu_step(&m->cpu);
    if (m->cpu.pc == 0 && t->pc != 0 && !m->entered_zero) {
        m->entered_zero = true;
        m->first_zero = *t;
        m->before_zero_count = m->trace_count;
        for (unsigned i = 0; i < m->trace_count; ++i)
            m->before_zero[i] = m->trace[(m->trace_next + ATARI8_TRACE_SIZE - m->trace_count + i) % ATARI8_TRACE_SIZE];
    }
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

/* A few legacy SAP-B images invoke an original VBI handler directly.
 * They restore their saved scratch area and tail-jump through an
 * uninstalled previous VBI vector instead of executing RTS.
 * Accept only a recognized chain site and an intact host stack frame.
 * This is a host ABI adaptation, never a change to the CPU instruction. */
static bool sap_vbi_return(const Atari8Machine *m, uint8_t sp, uint16_t entry)
{
    uint16_t vector = (uint16_t)(m->ram[0x224] | ((uint16_t)m->ram[0x225] << 8));
    if (!m->sap_host_scheduled || m->cpu.pc != 0 ||
        m->cpu.sp != (uint8_t)(sp-2u) || !m->trace_count ||
        (vector != 0 && vector != entry) || m->ram[0] ||
        m->ram[0x100u | sp] != 0xff ||
        m->ram[0x100u | (uint8_t)(sp-1u)] != 0xfe)
        return false;
    const Atari8Trace *t = &m->trace[(m->trace_next+ATARI8_TRACE_SIZE-1u)%ATARI8_TRACE_SIZE];
    return t->opcode == 0x4c && !t->operand1 && !t->operand2 &&
           !t->nmi && (!t->irq || (t->p & ATARI8_CPU_I)) &&
           (m->sap_vbi_tails[t->pc/8u] & (1u << (t->pc%8u)));
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
    m->trace_next = m->trace_count = 0;
    m->entered_zero = false;
    m->before_zero_count = 0;
    uint64_t start = m->master_cycles;
    while (m->master_cycles - start < cycle_budget) {
        Atari8MachineResult r = atari8_machine_step(m);
        if (r != ATARI8_MACHINE_OK)
            return r;
        if (m->master_cycles - start > cycle_budget)
            break;
        if (m->cpu.pc == 0xffffu && m->cpu.sp == sp)
            return ATARI8_MACHINE_OK;
        if (sap_vbi_return(m,sp,entry)) {
            /* Consume the synthetic host frame without executing
             * arbitrary zero-page data. As with host call setup, no
             * guest instruction or bus cycles are fabricated. */
            m->cpu.sp = sp;
            m->cpu.pc = 0xffffu;
            ++m->sap_vbi_returns;
            return ATARI8_MACHINE_OK;
        }
    }
    return m->status = ATARI8_MACHINE_BUDGET;
}
