#include "atari8_sapb.h"
#include <string.h>

static bool eq(const uint8_t *p, size_t n, const char *s)
{
    return n == strlen(s) && memcmp(p, s, n) == 0;
}

static bool num(const uint8_t *p, size_t n, unsigned base,
                unsigned max, unsigned *out)
{
    unsigned v = 0;
    if (!n || (base == 16 && n > 4))
        return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned d;
        if (p[i] >= '0' && p[i] <= '9') d = p[i] - '0';
        else if (p[i] >= 'A' && p[i] <= 'F') d = p[i] - 'A' + 10u;
        else if (p[i] >= 'a' && p[i] <= 'f') d = p[i] - 'a' + 10u;
        else return false;
        if (d >= base || d > max || v > (max - d) / base)
            return false;
        v = v * base + d;
    }
    *out = v;
    return true;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static bool valid_entry(unsigned address)
{
    return address < 0xffffu && !(address >= 0xd000u && address < 0xd800u);
}

static uint16_t init_entry(const Atari8SapbInfo *info)
{
    return info->type=='C' ? (uint16_t)(info->player+3u) : info->init;
}

static uint16_t play_entry(const Atari8SapbInfo *info)
{
    return info->type=='C' ? (uint16_t)(info->player+6u) : info->player;
}

/* Validate every segment before execution; optionally load it. */
static Atari8SapbResult segments(const uint8_t *data, size_t size,
                                const Atari8SapbInfo *info, Atari8Machine *m)
{
    size_t pos = info->binary_offset;
    bool any = false, init = false, player = info->type=='D', music = info->type!='C';
    if (info->type=='C' && info->player>0xfff8u)
        return ATARI8_SAPB_UNSUPPORTED;
    unsigned entry_init=init_entry(info), entry_play=play_entry(info);
    if (!valid_entry(entry_init) || (info->type!='D' &&
        (!valid_entry(info->player) || !valid_entry(entry_play))))
        return ATARI8_SAPB_UNSUPPORTED;
    while (pos < size) {
        if (size - pos >= 2 && le16(data + pos) == 0xffffu)
            pos += 2;
        if (size - pos < 4)
            return ATARI8_SAPB_BAD_FORMAT;
        uint16_t start = le16(data + pos), end = le16(data + pos + 2);
        pos += 4;
        if (end < start)
            return ATARI8_SAPB_BAD_FORMAT;
        size_t n = (size_t)end - start + 1u;
        if (n > size - pos)
            return ATARI8_SAPB_BAD_FORMAT;
        if (entry_init >= start && entry_init <= end) init = true;
        if (entry_play >= start && entry_play <= end) player = true;
        if (info->type=='C' && info->music >= start && info->music <= end) music = true;
        /* SAP blocks are memory images, not writes to device registers.
         * Preserve backing bytes even for a segment spanning the I/O
         * window. CPU bus access still follows the hardware map. */
        if (m) memcpy(m->ram + start, data + pos, n);
        any = true;
        pos += n;
    }
    return any && init && player && music ? ATARI8_SAPB_OK : ATARI8_SAPB_BAD_FORMAT;
}

Atari8SapbResult atari8_sapb_parse(const uint8_t *data, size_t size,
                                   Atari8SapbInfo *info)
{
    if (!data || !info || size < 7 || size > 8u * 1024u * 1024u ||
        memcmp(data, "SAP\r\n", 5))
        return ATARI8_SAPB_BAD_FORMAT;
    Atari8SapbInfo x = {0};
    x.songs = 1;
    unsigned seen = 0, fast = 0, times = 0;
    size_t pos = 5;
    enum { TYPE=1, INIT=2, PLAYER=4, FAST=8, SONGS=16, DEF=32, STEREO=64, NTSC=128, MUSIC=256 };
    while (pos < 16384u && pos < size) {
        if (size - pos >= 2 && le16(data + pos) == 0xffffu) {
            x.binary_offset = pos;
            break;
        }
        size_t start = pos;
        while (pos < size && pos < 16384u && data[pos] != '\r') {
            if (data[pos] < 32 || data[pos] > 126)
                return ATARI8_SAPB_BAD_FORMAT;
            ++pos;
        }
        if (pos >= 16384u || size - pos < 2 || data[pos+1] != '\n')
            return ATARI8_SAPB_BAD_FORMAT;
        size_t len = pos - start;
        const uint8_t *line = data + start;
        pos += 2;
        /* Accept empty header lines. */
        if (len == 0)
            continue;
        size_t k = 0;
        while (k < len && line[k] != ' ') ++k;
        size_t arg = k < len ? k + 1 : k;
        const uint8_t *a = line + arg;
        size_t n = len - arg;
        unsigned bit = 0, v;
        if (eq(line,k,"TYPE")) {
            bit = TYPE;
            if (n != 1) return ATARI8_SAPB_BAD_FORMAT;
            if (*a != 'B' && *a != 'C' && *a != 'D') return ATARI8_SAPB_UNSUPPORTED;
            x.type=(char)*a;
        } else if (eq(line,k,"INIT") || eq(line,k,"PLAYER") || eq(line,k,"MUSIC")) {
            bit = eq(line,k,"INIT") ? INIT : (eq(line,k,"PLAYER") ? PLAYER : MUSIC);
            if (!num(a,n,16,65535u,&v)) return ATARI8_SAPB_BAD_FORMAT;
            if (v == 65535u && bit != MUSIC) return ATARI8_SAPB_UNSUPPORTED;
            if (bit == INIT) x.init = (uint16_t)v;
            else if (bit == PLAYER) x.player = (uint16_t)v;
            else x.music = (uint16_t)v;
        } else if (eq(line,k,"FASTPLAY")) {
            bit = FAST;
            if (!num(a,n,10,32767u,&fast) || !fast) return ATARI8_SAPB_BAD_FORMAT;
        } else if (eq(line,k,"SONGS") || eq(line,k,"DEFSONG")) {
            bit = eq(line,k,"SONGS") ? SONGS : DEF;
            if (!num(a,n,10,bit == SONGS ? 256u : 255u,&v))
                return ATARI8_SAPB_BAD_FORMAT;
            if (bit == SONGS) {
                if (!v) return ATARI8_SAPB_BAD_FORMAT;
                x.songs = v;
            } else x.default_song = v;
        } else if (eq(line,k,"STEREO") || eq(line,k,"NTSC")) {
            bit = eq(line,k,"STEREO") ? STEREO : NTSC;
            if (arg != len || k != len) return ATARI8_SAPB_BAD_FORMAT;
            if (bit == STEREO) x.stereo = true; else x.ntsc = true;
        } else if (eq(line,k,"AUTHOR") || eq(line,k,"NAME") || eq(line,k,"DATE")) {
            if (n < 2 || a[0] != '"' || a[n-1] != '"') return ATARI8_SAPB_BAD_FORMAT;
        } else if (eq(line,k,"TIME")) {
            /* Metadata only in this milestone; no automatic loop/EOF. */
            if (!n || ++times > 256u) return ATARI8_SAPB_BAD_FORMAT;
        } else {
            return ATARI8_SAPB_UNSUPPORTED;
        }
        if (seen & bit) return ATARI8_SAPB_BAD_FORMAT;
        seen |= bit;
    }
    if (!(seen & TYPE) || !x.binary_offset ||
        x.default_song >= x.songs || times > x.songs)
        return ATARI8_SAPB_BAD_FORMAT;
    if (x.type=='C') {
        if (!(seen & PLAYER) || !(seen & MUSIC) || (seen & INIT))
            return ATARI8_SAPB_BAD_FORMAT;
    } else {
        if (!(seen & INIT) || (seen & MUSIC) ||
            (x.type=='B' && !(seen & PLAYER)))
            return ATARI8_SAPB_BAD_FORMAT;
        /* D1: periodic PLAYER injection is not implemented yet. */
        if (x.type=='D' && (seen & PLAYER))
            return ATARI8_SAPB_UNSUPPORTED;
    }
    x.interval = (fast ? fast : (x.ntsc ? 262u : 312u)) * 114u;
    Atari8SapbResult r = segments(data,size,&x,NULL);
    if (r == ATARI8_SAPB_OK) *info = x;
    return r;
}

/* Recognize the standard 6502 idiom that saves the Atari OS immediate
 * VBI vector (VVBLKI at $0224/$0225) into the operand of an absolute JMP.
 * No file name, author, fixed player address or guest patch is used.
 * The two load/store pairs can occur in either byte order. */
static void recognize_vbi_tails(Atari8Machine *m)
{
    const uint8_t *ram=m->ram;
    for (unsigned i=0; i+11u<65536u; ++i) {
        const uint8_t *p=ram+i;
        if (p[0]!=0xad || p[2]!=2 || p[3]!=0x8d ||
            p[6]!=0xad || p[8]!=2 || p[9]!=0x8d)
            continue;
        unsigned lo, hi;
        if (p[1]==0x24 && p[7]==0x25) {
            lo=le16(p+4); hi=le16(p+10);
        } else if (p[1]==0x25 && p[7]==0x24) {
            hi=le16(p+4); lo=le16(p+10);
        } else continue;
        if (!lo || lo>=65535u || hi!=lo+1u ||
            ram[lo-1u]!=0x4c || ram[lo] || ram[hi])
            continue;
        unsigned tail=lo-1u;
        m->sap_vbi_tails[tail/8u] |= (uint8_t)(1u<<(tail%8u));
    }
}

Atari8SapbResult atari8_sapb_open_mode(Atari8Sapb *s, const uint8_t *data,
                                  size_t size, int song, uint32_t rate,
                                  Atari8MachineSink sink, void *context,
                                  bool frame_restart)
{
    if (!s) return ATARI8_SAPB_BAD_FORMAT;
    Atari8SapbInfo info;
    Atari8SapbResult r = atari8_sapb_parse(data,size,&info);
    if (r != ATARI8_SAPB_OK) return s->status = r;
    if (song < -1 || (song >= 0 && (unsigned)song >= info.songs))
        return s->status = ATARI8_SAPB_BAD_FORMAT;
    if (frame_restart && info.type!='B') return s->status=ATARI8_SAPB_UNSUPPORTED;
    s->info = info;
    s->intervals = 0;
    s->missed_intervals = 0;
    s->frame_restart = frame_restart;
    s->next_deadline = s->abandoned_calls = 0;
    if (!atari8_machine_init(&s->machine,info.ntsc,info.stereo,rate,true,NULL,NULL))
        return s->status = ATARI8_SAPB_UNSUPPORTED;
    s->machine.sap_host_scheduled = true;
    r = segments(data,size,&info,&s->machine);
    if (r != ATARI8_SAPB_OK) return s->status = r;
    s->machine.cpu.a = (uint8_t)(song < 0 ? info.default_song : (unsigned)song);
    if (info.type=='D') {
        s->machine.sap_continuous = true;
        /* INIT is the running program, not a host subroutine. Audio
         * begins at entry; preserve CPU and master phase across chunks. */
        s->machine.cpu.pc = info.init;
        s->machine.cpu.sp = 0xff;
        s->machine.cpu.p = ATARI8_CPU_U;
        s->next_deadline = 0;
        s->machine.sink = sink;
        s->machine.sink_context = context;
        return s->status = ATARI8_SAPB_OK;
    }
    if (frame_restart) {
        s->machine.cpu.sp = 0xff;
        s->machine.cpu.p = ATARI8_CPU_U | (s->machine.cpu.a ? 0 : ATARI8_CPU_Z);
        if (s->machine.cpu.a & 0x80u) s->machine.cpu.p |= ATARI8_CPU_N;
        if (atari8_machine_idle(&s->machine,9) != ATARI8_MACHINE_OK)
            return s->status = ATARI8_SAPB_RUNTIME;
    }
    uint64_t init_budget=(uint64_t)s->machine.pcm.clock_hz*2u;
    if (info.type=='C') {
        uint8_t selected=s->machine.cpu.a;
        s->machine.cpu.a=0x70;
        s->machine.cpu.x=(uint8_t)info.music;
        s->machine.cpu.y=(uint8_t)(info.music>>8);
        if (atari8_machine_call(&s->machine,init_entry(&info),init_budget)!=ATARI8_MACHINE_OK)
            return s->status=ATARI8_SAPB_RUNTIME;
        s->machine.cpu.a=0;
        s->machine.cpu.x=selected;
    }
    if (atari8_machine_call(&s->machine,init_entry(&info),init_budget)!=ATARI8_MACHINE_OK)
        return s->status=ATARI8_SAPB_RUNTIME;
    recognize_vbi_tails(&s->machine);
    if (frame_restart) {
        /* Begin scheduled playback on the next FASTPLAY grid point,
         * offset nine master cycles; discard INIT and alignment audio. */
        uint32_t phase = (uint32_t)(s->machine.master_cycles % info.interval);
        uint32_t wait = (info.interval + 9u - phase) % info.interval;
        if (atari8_machine_idle(&s->machine,wait) != ATARI8_MACHINE_OK)
            return s->status = ATARI8_SAPB_RUNTIME;
        s->next_deadline = s->machine.master_cycles;
    }
    /* INIT is not part of the output timeline. Keep hardware phase, but
     * start a fresh sample integrator and output frame count. */
    if (!atari8_pcm_init(&s->machine.pcm,s->machine.core.pokey[0].clock_hz,
                         rate,info.stereo ? 2u : 1u,true))
        return s->status = ATARI8_SAPB_RUNTIME;
    s->machine.frames = 0;
    s->machine.sink = sink;
    s->machine.sink_context = context;
    return s->status = ATARI8_SAPB_OK;
}

Atari8SapbResult atari8_sapb_open(Atari8Sapb *s, const uint8_t *data,
                                  size_t size, int song, uint32_t rate,
                                  Atari8MachineSink sink, void *context)
{
    return atari8_sapb_open_mode(s,data,size,song,rate,sink,context,false);
}

/* Experimental scheduler based on observed frame-boundary behavior.
 * Guest state is retained, including the stack of an unfinished call.
 * The host models a six-cycle call and uses a reserved I/O address as
 * its return marker, without executing host code in guest RAM.
 * Each invocation is bounded by one interval plus an instruction;
 * JAM and unsupported I/O still fail. BRK is executed normally. */
static Atari8SapbResult restart_interval(Atari8Sapb *s)
{
    Atari8Machine *m=&s->machine;
    uint8_t sp=m->cpu.sp;
    m->ram[0x100u | m->cpu.sp--]=0xd2;
    m->ram[0x100u | m->cpu.sp--]=0x02;
    m->cpu.pc=s->info.player;
    m->trace_next=m->trace_count=m->before_zero_count=0;
    m->entered_zero=false;
    s->next_deadline+=s->info.interval;
    if (atari8_machine_idle(m,6)!=ATARI8_MACHINE_OK)
        return s->status=ATARI8_SAPB_RUNTIME;
    m->cpu.cycles+=6;
    bool returned=false;
    while (m->master_cycles<s->next_deadline) {
        if (atari8_machine_step(m)!=ATARI8_MACHINE_OK)
            return s->status=ATARI8_SAPB_RUNTIME;
        if (m->cpu.pc==0xd203u && m->cpu.sp==sp) {
            returned=true;
            break;
        }
    }
    if (!returned) ++s->abandoned_calls;
    if (m->master_cycles<s->next_deadline &&
        atari8_machine_idle(m,(uint32_t)(s->next_deadline-m->master_cycles))!=ATARI8_MACHINE_OK)
        return s->status=ATARI8_SAPB_RUNTIME;
    /* Preserve a fixed grid even when WSYNC or a final instruction
     * overshoots one or more unusually short FASTPLAY intervals. */
    while (m->master_cycles>=s->next_deadline+s->info.interval) {
        s->next_deadline+=s->info.interval;
        ++s->missed_intervals;
    }
    ++s->intervals;
    return ATARI8_SAPB_OK;
}

Atari8SapbResult atari8_sapb_interval(Atari8Sapb *s)
{
    if (!s) return ATARI8_SAPB_BAD_FORMAT;
    if (s->status != ATARI8_SAPB_OK) return s->status;
    if (s->frame_restart) return restart_interval(s);
    if (s->info.type=='D') {
        /* Absolute boundaries carry instruction/WSYNC overshoot into
         * the next chunk instead of accumulating timing drift. */
        s->next_deadline += s->info.interval;
        while (s->machine.master_cycles < s->next_deadline) {
            if (atari8_machine_step(&s->machine)!=ATARI8_MACHINE_OK)
                return s->status=ATARI8_SAPB_RUNTIME;
        }
        ++s->intervals;
        return ATARI8_SAPB_OK;
    }
    uint64_t start = s->machine.master_cycles;
    /* Compatibility policy: finish an overlong call, never reenter it.
     * Skip elapsed deadlines and return to the original FASTPLAY grid.
     * A non-returning routine still faults within eight periods and
     * at most two seconds of emulated time. */
    uint64_t budget = (uint64_t)s->info.interval * 8u;
    uint64_t ceiling = (uint64_t)s->machine.pcm.clock_hz * 2u;
    if (budget > ceiling) budget = ceiling;
    if (budget < s->info.interval) budget = s->info.interval;
    if (atari8_machine_call(&s->machine,play_entry(&s->info),budget) != ATARI8_MACHINE_OK)
        return s->status = ATARI8_SAPB_RUNTIME;
    uint64_t spent = s->machine.master_cycles - start;
    uint64_t periods = (spent + s->info.interval - 1u) / s->info.interval;
    if (!periods) periods = 1;
    uint32_t remaining = (uint32_t)(periods * s->info.interval - spent);
    if (atari8_machine_idle(&s->machine,remaining) != ATARI8_MACHINE_OK)
        return s->status = ATARI8_SAPB_RUNTIME;
    s->missed_intervals += periods - 1u;
    ++s->intervals;
    return ATARI8_SAPB_OK;
}
