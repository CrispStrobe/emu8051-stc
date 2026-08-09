/* trace.c — headless differential execution trace emitter.
 * Copyright 2024 CrispStrobe (MIT)
 *
 * Runs a firmware image in STC12 mode and emits a line-per-event trace
 * to stdout in the format defined by spec-updates/001-differential-trace-format.md.
 *
 * Usage: ./emu_trace -fosc 11059200 [-cycles N] firmware.hex > trace.tsv
 *
 * Build: gcc -O2 -o emu_trace trace.c core.c opcodes.c disasm.c stc12.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "emu8051.h"
#include "stc12.h"
#include "debug.h"

static struct em8051 cpu;
static struct stc12_state stc;
static FILE *trace_out;

/* SFR watch list — only these generate SFR events */
static const uint8_t sfr_watch[] = {
    0x8E, /* AUXR */
    0x89, /* TMOD */
    0x88, /* TCON */
    0x80, /* P0 */
    0x90, /* P1 */
    0xA0, /* P2 */
    0xB0, /* P3 */
    0xC0, /* P4 */
    0x94, /* P0M0 */
    0x93, /* P0M1 */
    0x92, /* P1M0 */
    0x91, /* P1M1 */
    0x96, /* P2M0 */
    0x95, /* P2M1 */
    0xB2, /* P3M0 */
    0xB1, /* P3M1 */
    0xBC, /* ADC_CONTR */
    0xD8, /* CCON */
    0xD9, /* CMOD */
    0xDA, /* CCAPM0 */
    0xDB, /* CCAPM1 */
};
#define N_SFR_WATCH (sizeof(sfr_watch) / sizeof(sfr_watch[0]))

/* Shadow for SFR change detection */
static uint8_t sfr_shadow[128];

static uint64_t get_ns(void) {
    return stc12_get_time_ns(&stc);
}

static const char *mode_str(enum stc12_pin_mode m) {
    switch (m) {
    case PIN_QUASI:     return "Q";
    case PIN_PUSHPULL:  return "PP";
    case PIN_INPUT:     return "IN";
    case PIN_OPENDRAIN: return "OD";
    }
    return "?";
}

/* Pin change callback for trace */
static void trace_pin_change(int port, int bit,
                             enum stc12_pin_mode mode,
                             bool drive_high, void *ud)
{
    (void)ud;
    fprintf(trace_out, "%llu\tPIN\t%d.%d %s %c\n",
            (unsigned long long)get_ns(),
            port, bit, mode_str(mode), drive_high ? 'H' : 'L');
}

static void trace_exception(struct em8051 *aCPU, int aCode) {
    (void)aCPU; (void)aCode;
}

int main(int argc, char **argv) {
    uint32_t fosc = 11059200;
    int max_cycles = 0;           /* 0 = use until_ns instead */
    uint64_t until_ns = 2000000;  /* default 2 ms */
    int step_pcs = 0;             /* -step-pcs N: emit N PCs, one per line */
    int adc_ch[8] = {0};          /* ADC channel inputs (0-1023) */
    int adc_set = 0;              /* bitmask of channels set */
    int bp_addr = -1;             /* -bp ADDR: code breakpoint */
    int write_space = -1, write_addr = -1, write_val = -1;
    int read_space = -1, read_addr = -1, read_len = 0;
    char *hexfile = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-fosc") == 0 && i + 1 < argc) {
            fosc = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-until-ns") == 0 && i + 1 < argc) {
            until_ns = strtoull(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-cycles") == 0 && i + 1 < argc) {
            max_cycles = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-step-pcs") == 0 && i + 1 < argc) {
            step_pcs = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-adc") == 0 && i + 1 < argc) {
            /* -adc CH,VALUE (e.g. -adc 2,512) */
            int ch, val;
            if (sscanf(argv[++i], "%d,%d", &ch, &val) == 2 && ch >= 0 && ch < 8) {
                adc_ch[ch] = val;
                adc_set |= (1 << ch);
            }
        } else if (strcmp(argv[i], "-bp") == 0 && i + 1 < argc) {
            bp_addr = (int)strtol(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "-write") == 0 && i + 1 < argc) {
            /* -write SPACE,ADDR,VAL (e.g. -write 1,0x30,0x42) */
            sscanf(argv[++i], "%d,%i,%i", &write_space, &write_addr, &write_val);
        } else if (strcmp(argv[i], "-read") == 0 && i + 1 < argc) {
            /* -read SPACE,ADDR,LEN (e.g. -read 1,0x08,2) */
            sscanf(argv[++i], "%d,%i,%d", &read_space, &read_addr, &read_len);
        } else if (argv[i][0] != '-') {
            hexfile = argv[i];
        }
    }

    if (!hexfile) {
        fprintf(stderr, "Usage: %s [-fosc Hz] [-until-ns N] [-adc CH,VAL] firmware.hex\n", argv[0]);
        return 1;
    }

    trace_out = stdout;

    memset(&cpu, 0, sizeof(cpu));
    cpu.mCodeMemMaxIdx = 65535;
    cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535;
    cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    cpu.except = trace_exception;

    reset(&cpu, 1);
    stc12_init(&cpu, &stc);
    stc.fosc = fosc;
    if (fosc > 0)
        stc.ns_per_clock_x16 = (uint64_t)(16.0e9 / fosc + 0.5);
    cpu.skip_timers = true;

    /* Install pin change callback for trace (not in step-pcs or bp mode) */
    if (step_pcs == 0 && bp_addr < 0)
        stc12_set_board_callbacks(&stc, trace_pin_change, NULL, NULL, NULL, NULL);

    if (load_obj(&cpu, hexfile) != 0) {
        fprintf(stderr, "Failed to load %s\n", hexfile);
        return 1;
    }

    /* Apply ADC channel inputs */
    for (int ch = 0; ch < 8; ch++) {
        if (adc_set & (1 << ch))
            stc12_set_adc_input(&stc, ch, adc_ch[ch]);
    }

    /* -step-pcs mode: emit N PCs, one per line, interrupts masked.
     * Per DEBUG-CONTROL-MODEL.md §4: a tick is not a step — advance
     * until a new instruction begins executing. */
    if (step_pcs > 0) {
        /* Mask interrupts: clear EA (IE bit 7) */
        cpu.mSFR[REG_IE] &= ~0x80;

        int emitted = 0;
        /* Emit the initial PC (reset vector) */
        fprintf(trace_out, "%04X\n", cpu.mPC);
        emitted++;

        while (emitted < step_pcs) {
            bool executed = tick(&cpu);
            stc12_tick(&cpu, &stc);
            /* Keep interrupts masked (firmware might enable them) */
            cpu.mSFR[REG_IE] &= ~0x80;

            if (executed) {
                /* An instruction just completed. PC is now at the NEXT
                 * instruction. Emit it. */
                fprintf(trace_out, "%04X\n", cpu.mPC);
                emitted++;
            }
        }
        return 0;
    }

    /* -bp mode: set breakpoint, run to it, dump registers, optionally
     * write and resume. Used for rungs 4-6 of the acceptance ladder. */
    if (bp_addr >= 0) {
        struct dbg_target dbg;
        dbg_init(&dbg, &cpu, &stc);

        struct dbg_breakpoint bp = { .kind = BP_CODE, .addr = (uint16_t)bp_addr };
        int bpid = dbg_set_breakpoint(&dbg, &bp);
        (void)bpid;

        /* Run until breakpoint or timeout */
        dbg_run(&dbg);
        uint64_t limit = until_ns;
        while (dbg_get_state(&dbg) == DBG_RUNNING && get_ns() < limit)
            dbg_tick(&dbg);

        if (dbg_get_state(&dbg) == DBG_HALTED) {
            fprintf(trace_out, "HALT\tPC=%04X\n", dbg_get_pc(&dbg));
            fprintf(trace_out, "REGS\tA=%02X B=%02X DPTR=%04X SP=%02X PSW=%02X\n",
                    dbg_get_acc(&dbg), dbg_get_b(&dbg), dbg_get_dptr(&dbg),
                    dbg_get_sp(&dbg), dbg_get_psw(&dbg));

            /* Optional read */
            if (read_space >= 0 && read_len > 0) {
                uint8_t buf[256];
                int n = read_len > 256 ? 256 : read_len;
                dbg_read_mem(&dbg, (enum dbg_space)read_space, (uint16_t)read_addr, buf, n);
                fprintf(trace_out, "READ\t");
                for (int i = 0; i < n; i++)
                    fprintf(trace_out, "%02X", buf[i]);
                fprintf(trace_out, "\n");
            }

            /* Optional write-while-halted, then resume and dump again */
            if (write_space >= 0) {
                uint8_t v = (uint8_t)write_val;
                dbg_write_mem(&dbg, (enum dbg_space)write_space,
                              (uint16_t)write_addr, &v, 1);
                fprintf(trace_out, "WRITE\t%d 0x%04X = 0x%02X\n",
                        write_space, write_addr, write_val);

                /* Resume, run a bit, then dump state */
                dbg_run(&dbg);
                for (int i = 0; i < 11059 * 10 && dbg_get_state(&dbg) == DBG_RUNNING; i++)
                    dbg_tick(&dbg);

                fprintf(trace_out, "RESUME\tPC=%04X A=%02X\n",
                        dbg_get_pc(&dbg), dbg_get_acc(&dbg));

                /* Read back the written location */
                if (read_space < 0) { read_space = write_space; read_addr = write_addr; read_len = 1; }
                uint8_t rb;
                dbg_read_mem(&dbg, (enum dbg_space)write_space,
                             (uint16_t)write_addr, &rb, 1);
                fprintf(trace_out, "READBACK\t0x%02X\n", rb);
            }
        } else {
            fprintf(trace_out, "TIMEOUT\n");
        }

        free(cpu.mCodeMem); free(cpu.mExtData); free(cpu.mUpperData);
        return 0;
    }

    /* Snapshot initial SFR state */
    memcpy(sfr_shadow, cpu.mSFR, 128);

    uint16_t last_pc = 0xFFFF;
    uint8_t last_tcon = 0;

    for (int cycle = 0; ; cycle++) {
        /* Stop on time bound or cycle bound */
        if (max_cycles > 0 && cycle >= max_cycles) break;
        if (max_cycles == 0 && get_ns() > until_ns) break;
        /* Emit PC event on new instruction */
        if (cpu.mPC != last_pc && cpu.mTickDelay == 0) {
            fprintf(trace_out, "%llu\tPC\t%04X\n",
                    (unsigned long long)get_ns(), cpu.mPC);
            last_pc = cpu.mPC;
        }

        bool ticked = tick(&cpu);
        stc12_tick(&cpu, &stc);

        /* Snapshot ADC_CONTR before SFR shadow update (for ADC event detection) */
        uint8_t old_adc_contr = sfr_shadow[STC_REG_ADC_CONTR];

        /* Detect SFR changes */
        for (unsigned i = 0; i < N_SFR_WATCH; i++) {
            uint8_t idx = sfr_watch[i] - 0x80;
            if (cpu.mSFR[idx] != sfr_shadow[idx]) {
                fprintf(trace_out, "%llu\tSFR\t%02X %02X\n",
                        (unsigned long long)get_ns(),
                        sfr_watch[i], cpu.mSFR[idx]);
                sfr_shadow[idx] = cpu.mSFR[idx];
            }
        }

        /* Detect timer overflow events */
        uint8_t new_tcon = cpu.mSFR[REG_TCON];
        if ((new_tcon & TCONMASK_TF0) && !(last_tcon & TCONMASK_TF0)) {
            fprintf(trace_out, "%llu\tTF\t0\n",
                    (unsigned long long)get_ns());
        }
        if ((new_tcon & TCONMASK_TF1) && !(last_tcon & TCONMASK_TF1)) {
            fprintf(trace_out, "%llu\tTF\t1\n",
                    (unsigned long long)get_ns());
        }
        last_tcon = new_tcon;

        /* Detect ADC completion (use pre-update shadow) */
        if ((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) &&
            !(old_adc_contr & ADC_FLAG)) {
            int ch = cpu.mSFR[STC_REG_ADC_CONTR] & ADC_CHS_MASK;
            uint16_t result;
            if (cpu.mSFR[STC_REG_AUXR1] & AUXR1_ADRJ) {
                result = ((uint16_t)(cpu.mSFR[STC_REG_ADC_RES] & 0x03) << 8)
                         | cpu.mSFR[STC_REG_ADC_RESL];
            } else {
                result = ((uint16_t)cpu.mSFR[STC_REG_ADC_RES] << 2)
                         | (cpu.mSFR[STC_REG_ADC_RESL] & 0x03);
            }
            fprintf(trace_out, "%llu\tADC\t%d %d\n",
                    (unsigned long long)get_ns(), ch, result);
        }

        (void)ticked;
    }

    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
    return 0;
}
