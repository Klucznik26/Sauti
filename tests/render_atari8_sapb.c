/* Standalone listening and compatibility diagnostic. No external decoder. */
#include "../src/decoders/retro/atari8/atari8_sapb.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>

typedef struct {
    FILE *file;
    uint64_t frames, limit;
    unsigned channels;
    bool failed;
} Output;

static bool le(FILE *f, uint32_t value, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; ++i)
        if (fputc((int)((value >> (8u*i)) & 255u), f) == EOF)
            return false;
    return true;
}

static bool header(Output *out)
{
    FILE *f = out->file;
    uint32_t bytes = (uint32_t)(out->limit * out->channels * 2u);
    unsigned align = out->channels * 2u;
    return fwrite("RIFF",1,4,f)==4 && le(f,36u+bytes,4)
        && fwrite("WAVEfmt ",1,8,f)==8 && le(f,16,4)
        && le(f,1,2) && le(f,out->channels,2)
        && le(f,48000,4) && le(f,48000u*align,4)
        && le(f,align,2) && le(f,16,2)
        && fwrite("data",1,4,f)==4 && le(f,bytes,4);
}

static void sink(void *context, const int16_t frame[2])
{
    Output *out = context;
    if (out->failed || out->frames >= out->limit)
        return;
    for (unsigned i=0; i<out->channels; ++i)
        if (!le(out->file,(uint16_t)frame[i],2)) {
            out->failed=true;
            return;
        }
    ++out->frames;
}

static bool integer(const char *s, long low, long high, long *v)
{
    char *end;
    errno=0;
    long n=strtol(s,&end,10);
    if (errno || end==s || *end || n<low || n>high)
        return false;
    *v=n;
    return true;
}

static void trace_line(const Atari8Trace *t)
{
    fprintf(stderr," PC=%04X op=%02X %02X %02X A=%02X X=%02X Y=%02X SP=%02X P=%02X IRQ=%u NMI=%u vec=%04X cycle=%" PRIu64,
            (unsigned)t->pc,(unsigned)t->opcode,(unsigned)t->operand1,(unsigned)t->operand2,
            (unsigned)t->a,(unsigned)t->x,(unsigned)t->y,(unsigned)t->sp,(unsigned)t->p,
            (unsigned)t->irq,(unsigned)t->nmi,(unsigned)t->irq_vector,t->cycle);
}

static void diagnostic(const Atari8Sapb *s, const char *stage, Atari8SapbResult r)
{
    fprintf(stderr,"%s failed: SAP-B status %d",stage,(int)r);
    if (r==ATARI8_SAPB_RUNTIME) {
        const Atari8Machine *m=&s->machine;
        fprintf(stderr,", machine status %d, PC=%04X, cycles=%" PRIu64,
                (int)m->status,(unsigned)m->cpu.pc,m->master_cycles);
        if (m->status==ATARI8_MACHINE_IO) {
            fprintf(stderr,", unsupported I/O %s $%04X",
                m->fault_write?"write":"read",(unsigned)m->fault_address);
            if (m->fault_write)
                fprintf(stderr," value=$%02X",(unsigned)m->fault_value);
        }
        if (m->status==ATARI8_MACHINE_BUDGET) {
            fprintf(stderr,"; completed calls=%" PRIu64,(uint64_t)s->intervals);
            if (m->entered_zero) {
                fputs("; first transition to $0000 from",stderr);
                trace_line(&m->first_zero);
                fputs("; instructions before first zero:",stderr);
                for (unsigned j=0;j<m->before_zero_count;++j) {
                    trace_line(&m->before_zero[j]);
                    fputc(';',stderr);
                }
            }
            fputs("; recent instructions:",stderr);
            for (unsigned j=0;j<m->trace_count;++j) {
                unsigned i=(m->trace_next+ATARI8_TRACE_SIZE-m->trace_count+j)%ATARI8_TRACE_SIZE;
                trace_line(&m->trace[i]);
                fputc(';',stderr);
            }
        }
        if (m->status==ATARI8_MACHINE_CPU_STOP)
            fprintf(stderr,", stopped opcode $%02X",(unsigned)m->cpu.bad_opcode);
    }
    fputc('\n',stderr);
}

int main(int argc,char **argv)
{
    bool restart=argc>1 && !strcmp(argv[argc-1],"--frame-restart");
    if (restart) --argc;
    long seconds=15, song=-1;
    if (argc<3 || argc>5 || (argc>=4 && !integer(argv[3],1,300,&seconds))
        || (argc==5 && !integer(argv[4],-1,255,&song))) {
        fprintf(stderr,"Usage: %s INPUT.sap OUTPUT.wav [seconds 1..300] [song -1..255] [--frame-restart]\n",argv[0]);
        return 2;
    }
    FILE *in=fopen(argv[1],"rb");
    if (!in) { perror(argv[1]); return 2; }
    if (fseek(in,0,SEEK_END)) { fclose(in); return 2; }
    long n=ftell(in);
    if (n<=0 || n>8L*1024*1024 || fseek(in,0,SEEK_SET)) {
        fclose(in); return 2;
    }
    uint8_t *data=malloc((size_t)n);
    if (!data) { fclose(in); return 2; }
    bool ok=fread(data,1,(size_t)n,in)==(size_t)n;
    if (fclose(in)) ok=false;
    if (!ok) { free(data); return 2; }
    Atari8Sapb s={0};
    Output out={0};
    Atari8SapbResult r=atari8_sapb_open_mode(&s,data,(size_t)n,(int)song,48000,sink,&out,restart);
    free(data);
    if (r!=ATARI8_SAPB_OK) { diagnostic(&s,"INIT/parse",r); return 1; }
    if (restart) fputs("Experimental frame-restart policy enabled\n",stderr);
    out.channels=s.info.stereo?2u:1u;
    out.limit=(uint64_t)seconds*48000u;
    out.file=fopen(argv[2],"wb");
    if (!out.file) { perror(argv[2]); return 2; }
    ok=header(&out);
    fprintf(stderr,"SAP-%c %s, %u channel(s), song %u/%u (zero-based), interval %u cycles\n",
        s.info.type,s.info.ntsc?"NTSC":"PAL",out.channels,
        song<0?s.info.default_song:(unsigned)song,s.info.songs,s.info.interval);
    while (ok && !out.failed && out.frames<out.limit) {
        r=atari8_sapb_interval(&s);
        if (r!=ATARI8_SAPB_OK) {
            diagnostic(&s,"PLAYER",r);
            ok=false;
        }
    }
    if (fclose(out.file)) ok=false;
    if (!ok || out.failed || out.frames!=out.limit) {
        remove(argv[2]);
        return 1;
    }
    if (s.info.type=='S') {
        printf("Wrote %" PRIu64 " PCM frames at 48000 Hz, %" PRIu64 " SoftSynth RAM ticks, PC=%04X, clock=%02X/%02X\n",
            out.frames,s.intervals,(unsigned)s.machine.cpu.pc,
            (unsigned)s.machine.ram[0x45],(unsigned)s.machine.ram[0xb07b]);
    }
    else if (s.info.type=='D') {
        printf("Wrote %" PRIu64 " PCM frames at 48000 Hz, %" PRIu64 " continuous CPU chunks, %" PRIu64 " player calls, %" PRIu64 " skipped deadlines\n",
            out.frames,s.intervals,s.continuous_player_calls,s.missed_intervals);
        printf("D audio writes: POKEY=%" PRIu64 " CONSOL=%" PRIu64 " edges=%" PRIu64 " PC=%04X IRQ=%02X SKCTL=%02X\n",
            s.machine.sap_pokey_audio_writes,s.machine.sap_consol_writes,s.machine.sap_consol_edges,
            (unsigned)s.machine.cpu.pc,(unsigned)s.machine.core.pokey[0].irq_enable,
            (unsigned)s.machine.core.pokey[0].skctl);
        printf("D headless video DMA requests: %" PRIu64 "\n",s.machine.sap_video_dma_requests);
        printf("D unmodeled IRQEN requests: %" PRIu64 ", serial clock requests: %" PRIu64 "\n",
            s.machine.sap_non_timer_irq_requests,s.machine.sap_serial_clock_requests);
    }
    else
        printf("Wrote %" PRIu64 " PCM frames at 48000 Hz, %" PRIu64 " player calls, %" PRIu64 " skipped deadlines, %" PRIu64 " legacy VBI returns\n",
        out.frames,s.intervals,s.missed_intervals,s.machine.sap_vbi_returns);
    if (restart) printf("Abandoned calls: %" PRIu64 "\n",s.abandoned_calls);
    return 0;
}
