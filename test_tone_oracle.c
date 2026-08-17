/* test_tone_oracle.c — buzzer/tone timing oracle.
 *
 * Generates PCA-toggle tones at known frequencies, measures actual edge
 * periods, and verifies against the expected period. Also tests T0CLKO
 * (STC15) and software-toggle (timer ISR) paths.
 *
 * Target tones: 440 Hz (A4), 1000 Hz, 2000 Hz, 4000 Hz
 * PCA toggle: compare value = FOSC / (2 × freq), CPS=SYSclk
 *
 * Build: gcc -O2 -o test_tone_oracle test_tone_oracle.c core.c opcodes.c disasm.c stc12.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int pass_count = 0;
static int fail_count = 0;

#define PASS(msg) do { printf("  PASS: %s\n", msg); pass_count++; } while(0)
#define FAIL(msg) do { printf("  FAIL: %s\n", msg); fail_count++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(msg); else FAIL(msg); } while(0)

/* ================================================================== *
 * Pin edge recorder                                                    *
 * ================================================================== */

#define MAX_EDGES 8192
static uint64_t edge_ns[MAX_EDGES];
static int      edge_drive[MAX_EDGES];
static int      edge_port_filter, edge_bit_filter;
static int      n_edges;

static void tone_pin_cb(int port, int bit, enum stc12_pin_mode mode,
                         bool drive_high, void *ud)
{
    (void)mode; (void)ud;
    if (port == edge_port_filter && bit == edge_bit_filter && n_edges < MAX_EDGES) {
        edge_ns[n_edges] = stc12_get_time_ns(&stc);
        edge_drive[n_edges] = drive_high ? 1 : 0;
        n_edges++;
    }
}

/* ================================================================== *
 * Helpers                                                              *
 * ================================================================== */

static void setup_stc12(void) {
    memset(&cpu, 0, sizeof(cpu));
    memset(&stc, 0, sizeof(stc));
    cpu.mCodeMemMaxIdx = 65535;
    cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535;
    cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    reset(&cpu, 1);
    stc.fosc = 11059200;
    stc12_init(&cpu, &stc);
    cpu.mMachineCycleScale = 1; /* STC12 1T */
    cpu.skip_timers = true;     /* STC12/15: timers handled by stc12_tick */
    n_edges = 0;
    stc12_set_board_callbacks(&stc, tone_pin_cb, NULL, NULL, NULL, NULL);
}

static void setup_stc15(void) {
    setup_stc12();
    stc12_set_part(&stc, 1); /* PART_STC15 */
    cpu.mMachineCycleScale = 1;
    /* Re-register callback after set_part */
    stc12_set_board_callbacks(&stc, tone_pin_cb, NULL, NULL, NULL, NULL);
}

static void teardown(void) {
    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
}

static void run_ns(uint64_t target_ns) {
    while (stc12_get_time_ns(&stc) < target_ns) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);
    }
}

/* Measure frequency from recorded edges.
 * Returns measured frequency in Hz (from mean full-cycle period). */
static double measure_frequency(int *out_cycles) {
    /* Find consecutive same-polarity edges to get full periods */
    int cycles = 0;
    uint64_t total_period = 0;

    for (int i = 2; i < n_edges; i++) {
        /* Find pairs of rising edges (or falling) */
        if (edge_drive[i] == edge_drive[i-2] && edge_drive[i] != edge_drive[i-1]) {
            uint64_t period = edge_ns[i] - edge_ns[i-2];
            total_period += period;
            cycles++;
        }
    }

    if (out_cycles) *out_cycles = cycles;
    if (cycles == 0) return 0.0;

    double mean_period_ns = (double)total_period / cycles;
    return 1e9 / mean_period_ns;
}

/* ================================================================== *
 * PCA toggle firmware generator                                        *
 *                                                                      *
 * Generates machine code that sets up PCA module 0 in toggle mode      *
 * with a given compare value. Output on P1.3 (CCP0 default pin).      *
 * ================================================================== */

static uint16_t pca_compare_val; /* stashed for ISR to use */

static void inject_pca_toggle_firmware(uint16_t compare) {
    int pc;
    pca_compare_val = compare;

    /* Vector: 0x0000 → LJMP main, 0x003B → PCA ISR */
    pc = 0;
    cpu.mCodeMem[pc++] = 0x02; /* LJMP */
    cpu.mCodeMem[pc++] = 0x00;
    cpu.mCodeMem[pc++] = 0x50; /* main at 0x50 */

    /* PCA ISR at 0x003B — update CCAP0 to next toggle point.
     * New compare = old compare + step.
     * We add the step to CCAP0L:CCAP0H (16-bit add). */
    pc = 0x3B;
    /* Clear CCF0: ANL CCON, #0xFE — clears bit 0 (CCF0), preserves CR (bit 6) */
    cpu.mCodeMem[pc++] = 0x53; /* ANL direct, #imm */
    cpu.mCodeMem[pc++] = 0xD8; /* CCON */
    cpu.mCodeMem[pc++] = 0xFE; /* 1111 1110 — only clear bit 0 */

    /* Add step to CCAP0L: MOV A, CCAP0L; ADD A, #lo; MOV CCAP0L, A */
    cpu.mCodeMem[pc++] = 0xE5; /* MOV A, direct */
    cpu.mCodeMem[pc++] = 0xEA; /* CCAP0L */
    cpu.mCodeMem[pc++] = 0x24; /* ADD A, #imm */
    cpu.mCodeMem[pc++] = compare & 0xFF;
    cpu.mCodeMem[pc++] = 0xF5; /* MOV direct, A */
    cpu.mCodeMem[pc++] = 0xEA; /* CCAP0L */

    /* Add carry to CCAP0H: MOV A, CCAP0H; ADDC A, #hi; MOV CCAP0H, A */
    cpu.mCodeMem[pc++] = 0xE5; /* MOV A, direct */
    cpu.mCodeMem[pc++] = 0xFA; /* CCAP0H */
    cpu.mCodeMem[pc++] = 0x34; /* ADDC A, #imm */
    cpu.mCodeMem[pc++] = (compare >> 8) & 0xFF;
    cpu.mCodeMem[pc++] = 0xF5; /* MOV direct, A */
    cpu.mCodeMem[pc++] = 0xFA; /* CCAP0H */

    /* RETI */
    cpu.mCodeMem[pc++] = 0x32;

    /* main at 0x50 */
    pc = 0x50;
    /* MOV CMOD, #0x08 — CPS=SYSclk */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0xD9;
    cpu.mCodeMem[pc++] = 0x08;

    /* MOV CCAPM0, #0x4C — ECOM+MAT+TOG */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0xDA;
    cpu.mCodeMem[pc++] = 0x4C;

    /* MOV CCAP0L, #lo */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0xEA;
    cpu.mCodeMem[pc++] = compare & 0xFF;

    /* MOV CCAP0H, #hi */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0xFA;
    cpu.mCodeMem[pc++] = (compare >> 8) & 0xFF;

    /* MOV CL, #0 */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0xE9;
    cpu.mCodeMem[pc++] = 0x00;

    /* MOV CH, #0 */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0xF9;
    cpu.mCodeMem[pc++] = 0x00;

    /* Enable PCA interrupt: CCAPM0 already has ECCF (bit 0) = 0.
     * We need ECCF in CCAPM0 for the module interrupt.
     * CCAPM0 = 0x4D = ECOM+MAT+TOG+ECCF */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0xDA; /* CCAPM0 */
    cpu.mCodeMem[pc++] = 0x4D; /* +ECCF */

    /* MOV CMOD, #0x08 — CIDL=0, no watchdog, CPS=SYSclk, ECF=0 */
    /* Actually we need ECF=1 in CMOD for PCA interrupt to fire.
     * Wait: the PCA interrupt fires if CCAPMn.ECCF is set and CCFn is set.
     * But the PCA interrupt vector 0x33 fires when the PCA interrupt is
     * enabled. Let's check: IE doesn't have a PCA bit on STC12.
     * The PCA interrupt is controlled by CMOD.ECF (bit 0) for counter overflow,
     * and CCAPMn.ECCF for module match. The interrupt fires to vector 0x33
     * when either (CMOD.ECF && CCON.CF) or (CCAPMn.ECCF && CCON.CCFn).
     * But the interrupt must also be enabled globally (EA=1).
     * There is no IE bit for PCA — it uses its own mask bits. */

    /* MOV IE, #0x80 — EA=1 only */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0xA8;
    cpu.mCodeMem[pc++] = 0x80;

    /* ORL CCON, #0x40 — CR=1, start counter */
    cpu.mCodeMem[pc++] = 0x43;
    cpu.mCodeMem[pc++] = 0xD8;
    cpu.mCodeMem[pc++] = 0x40;

    /* SJMP $ */
    cpu.mCodeMem[pc++] = 0x80;
    cpu.mCodeMem[pc++] = 0xFE;
}

/* ================================================================== *
 * T0CLKO firmware generator (STC15)                                   *
 *                                                                      *
 * Timer 0 in mode 2 (8-bit auto-reload), 1T mode (AUXR.T0x12=1).     *
 * Toggles P3.5 on every overflow via INT_CLKO.T0CLKO.                 *
 * Frequency = FOSC / (2 × (256 - TH0))                                *
 * ================================================================== */

static void inject_t0clko_firmware(uint8_t reload) {
    int pc = 0;

    /* MOV AUXR, #0x80 — T0x12=1 (1T mode for Timer 0) */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x8E; /* AUXR */
    cpu.mCodeMem[pc++] = 0x80;

    /* MOV TMOD, #0x02 — Timer 0 mode 2 (8-bit auto-reload) */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x89; /* TMOD */
    cpu.mCodeMem[pc++] = 0x02;

    /* MOV TH0, #reload — auto-reload value */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x8C; /* TH0 */
    cpu.mCodeMem[pc++] = reload;

    /* MOV TL0, #reload — initial value */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x8A; /* TL0 */
    cpu.mCodeMem[pc++] = reload;

    /* MOV INT_CLKO, #0x01 — T0CLKO enable */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x8F; /* INT_CLKO */
    cpu.mCodeMem[pc++] = 0x01;

    /* SETB TCON.4 (TR0 — start Timer 0)
     * ORL TCON, #0x10 */
    cpu.mCodeMem[pc++] = 0x43;
    cpu.mCodeMem[pc++] = 0x88; /* TCON */
    cpu.mCodeMem[pc++] = 0x10;

    /* SJMP $ */
    cpu.mCodeMem[pc++] = 0x80;
    cpu.mCodeMem[pc++] = 0xFE;
}

/* ================================================================== *
 * Software toggle firmware (Timer 0 ISR toggles P1.0)                 *
 *                                                                      *
 * Timer 0 mode 1 (16-bit), ISR at 0x000B toggles P1.0 and reloads.   *
 * Works on all parts (STC12, STC15, STC89).                            *
 * Frequency = FOSC / (2 × (65536 - reload))                           *
 * ================================================================== */

static void inject_sw_toggle_firmware(uint16_t reload) {
    int pc;

    /* Vector table: 0x0000 → LJMP main, 0x000B → ISR */
    pc = 0;
    cpu.mCodeMem[pc++] = 0x02; /* LJMP */
    cpu.mCodeMem[pc++] = 0x00;
    cpu.mCodeMem[pc++] = 0x30; /* main at 0x0030 */

    /* Timer 0 ISR at 0x000B */
    pc = 0x0B;
    /* CPL P1.0 */
    cpu.mCodeMem[pc++] = 0xB2;
    cpu.mCodeMem[pc++] = 0x90; /* P1.0 bit address */
    /* MOV TH0, #hi */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x8C;
    cpu.mCodeMem[pc++] = (reload >> 8) & 0xFF;
    /* MOV TL0, #lo */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x8A;
    cpu.mCodeMem[pc++] = reload & 0xFF;
    /* RETI */
    cpu.mCodeMem[pc++] = 0x32;

    /* main at 0x0030 */
    pc = 0x30;
    /* MOV AUXR, #0x80 — T0x12=1 (1T mode) */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x8E;
    cpu.mCodeMem[pc++] = 0x80;
    /* MOV TMOD, #0x01 — Timer 0 mode 1 (16-bit) */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x89;
    cpu.mCodeMem[pc++] = 0x01;
    /* MOV TH0, #hi */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x8C;
    cpu.mCodeMem[pc++] = (reload >> 8) & 0xFF;
    /* MOV TL0, #lo */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0x8A;
    cpu.mCodeMem[pc++] = reload & 0xFF;
    /* MOV IE, #0x82 — EA=1, ET0=1 */
    cpu.mCodeMem[pc++] = 0x75;
    cpu.mCodeMem[pc++] = 0xA8;
    cpu.mCodeMem[pc++] = 0x82;
    /* ORL TCON, #0x10 — TR0=1 */
    cpu.mCodeMem[pc++] = 0x43;
    cpu.mCodeMem[pc++] = 0x88;
    cpu.mCodeMem[pc++] = 0x10;
    /* SJMP $ */
    cpu.mCodeMem[pc++] = 0x80;
    cpu.mCodeMem[pc++] = 0xFE;
}

/* ================================================================== *
 * Test 1: PCA toggle at 4 target frequencies                         *
 * ================================================================== */
static void test_pca_tones(void) {
    printf("\n=== PCA toggle tones (STC12, CPS=SYSclk, output on P1.3) ===\n\n");
    printf("| Target Hz | Compare | Expected period (ns) | Measured period (ns) | Measured Hz | Error | Cycles |\n");
    printf("|-----------|---------|---------------------|---------------------|-------------|-------|--------|\n");

    struct { double freq; uint16_t compare; } tones[] = {
        { 440.0,  (uint16_t)(11059200.0 / (2 * 440) + 0.5)  },  /* A4 */
        { 1000.0, (uint16_t)(11059200.0 / (2 * 1000) + 0.5) },  /* 1 kHz */
        { 2000.0, (uint16_t)(11059200.0 / (2 * 2000) + 0.5) },  /* 2 kHz */
        { 4000.0, (uint16_t)(11059200.0 / (2 * 4000) + 0.5) },  /* 4 kHz */
    };
    int n = sizeof(tones) / sizeof(tones[0]);

    for (int i = 0; i < n; i++) {
        setup_stc12();
        edge_port_filter = 1; edge_bit_filter = 3; /* P1.3 = CCP0 */

        inject_pca_toggle_firmware(tones[i].compare);

        /* Run for enough time to capture many cycles.
         * For 440 Hz, need ~2.3 ms per cycle, run 50 ms = ~22 cycles */
        run_ns(50000000ULL); /* 50 ms */

        int cycles;
        double meas_hz = measure_frequency(&cycles);
        double meas_period = (meas_hz > 0) ? 1e9 / meas_hz : 0;
        /* Exact expected Hz from the compare value actually programmed */
        double exact_hz = (double)stc.fosc / (2.0 * tones[i].compare);
        double exact_period = 1e9 / exact_hz;
        double error_ppm = (meas_hz > 0) ? fabs(meas_hz - exact_hz) / exact_hz * 1e6 : 999999;

        printf("| %7.0f   | %5d   | %18.1f   | %18.1f   | %11.3f | %5.0f ppm | %4d   |\n",
               tones[i].freq, tones[i].compare, exact_period, meas_period, meas_hz, error_ppm, cycles);

        char msg[80];
        snprintf(msg, sizeof(msg), "PCA %.0f Hz: measured %.3f Hz (%d ppm)",
                 tones[i].freq, meas_hz, (int)error_ppm);
        CHECK(error_ppm < 200, msg);
        CHECK(cycles >= 5, "PCA: enough cycles measured");

        teardown();
    }
}

/* ================================================================== *
 * Test 2: T0CLKO on STC15 (high-frequency tones)                     *
 * ================================================================== */
static void test_t0clko_tones(void) {
    printf("\n=== T0CLKO tones (STC15, Timer 0 mode 2, output on P3.5) ===\n\n");
    printf("| Reload | Expected Hz | Expected period (ns) | Measured period (ns) | Measured Hz | Error | Cycles |\n");
    printf("|--------|------------|---------------------|---------------------|-------------|-------|--------|\n");

    /* Timer 0 mode 2, 1T: freq = FOSC / (2 × (256 - reload)) */
    struct { uint8_t reload; } tests[] = {
        { 0x00 },   /* 256 - 0 = 256 → FOSC/512 = 21600 Hz */
        { 0x80 },   /* 256 - 128 = 128 → FOSC/256 = 43200 Hz */
        { 0xE0 },   /* 256 - 224 = 32 → FOSC/64 = 172800 Hz */
        { 0xF0 },   /* 256 - 240 = 16 → FOSC/32 = 345600 Hz */
    };
    int n = sizeof(tests) / sizeof(tests[0]);

    for (int i = 0; i < n; i++) {
        setup_stc15();
        edge_port_filter = 3; edge_bit_filter = 5; /* P3.5 = T0CLKO */

        inject_t0clko_firmware(tests[i].reload);

        /* These are high frequencies, 1 ms is enough */
        run_ns(1000000ULL);

        int cycles;
        double meas_hz = measure_frequency(&cycles);
        int period_clocks = 256 - tests[i].reload;
        double exact_hz = (double)stc.fosc / (2.0 * period_clocks);
        double exact_period = 1e9 / exact_hz;
        double meas_period = (meas_hz > 0) ? 1e9 / meas_hz : 0;
        double error_ppm = (meas_hz > 0) ? fabs(meas_hz - exact_hz) / exact_hz * 1e6 : 999999;

        printf("| 0x%02X   | %10.0f | %18.1f   | %18.1f   | %11.3f | %5.0f ppm | %4d   |\n",
               tests[i].reload, exact_hz, exact_period, meas_period, meas_hz, error_ppm, cycles);

        char msg[80];
        snprintf(msg, sizeof(msg), "T0CLKO reload=0x%02X: measured %.0f Hz (%d ppm)",
                 tests[i].reload, meas_hz, (int)error_ppm);
        CHECK(error_ppm < 200, msg);

        teardown();
    }
}

/* ================================================================== *
 * Test 3: Software toggle via Timer 0 ISR (works on all parts)        *
 * ================================================================== */
static void test_sw_toggle_tones(void) {
    printf("\n=== Software toggle tones (Timer 0 ISR, output on P1.0) ===\n\n");
    printf("| Target Hz | Reload | Expected Hz | Measured Hz | Error | Cycles |\n");
    printf("|-----------|--------|------------|-------------|-------|--------|\n");

    /* Timer 0 mode 1, 1T: freq = FOSC / (2 × (65536 - reload))
     * But ISR overhead adds ~10 clocks per interrupt, so actual freq is lower. */
    struct { double target; uint16_t reload; } tones[] = {
        { 440.0,  (uint16_t)(65536 - 11059200.0 / (2 * 440) + 0.5) },
        { 1000.0, (uint16_t)(65536 - 11059200.0 / (2 * 1000) + 0.5) },
        { 2000.0, (uint16_t)(65536 - 11059200.0 / (2 * 2000) + 0.5) },
        { 4000.0, (uint16_t)(65536 - 11059200.0 / (2 * 4000) + 0.5) },
    };
    int n = sizeof(tones) / sizeof(tones[0]);

    for (int i = 0; i < n; i++) {
        setup_stc12();
        edge_port_filter = 1; edge_bit_filter = 0; /* P1.0 */

        inject_sw_toggle_firmware(tones[i].reload);

        run_ns(50000000ULL); /* 50 ms */

        int cycles;
        double meas_hz = measure_frequency(&cycles);
        double ideal_hz = (double)stc.fosc / (2.0 * (65536 - tones[i].reload));
        /* ISR overhead: ~10 clocks for LCALL+CPL+MOV+MOV+RETI, tolerate 1% error */
        double error_pct = (meas_hz > 0) ? fabs(meas_hz - ideal_hz) / ideal_hz * 100 : 100;

        printf("| %7.0f   | 0x%04X | %10.3f | %11.3f | %4.2f%% | %4d   |\n",
               tones[i].target, tones[i].reload, ideal_hz, meas_hz, error_pct, cycles);

        char msg[80];
        snprintf(msg, sizeof(msg), "SW toggle %.0f Hz: measured %.1f Hz (%.2f%% error)",
                 tones[i].target, meas_hz, error_pct);
        /* Allow 2% for ISR overhead */
        CHECK(error_pct < 2.0, msg);

        teardown();
    }
}

int main(void) {
    printf("=== Tone Timing Oracle ===\n");
    printf("FOSC = %d Hz\n", 11059200);

    test_pca_tones();
    test_t0clko_tones();
    test_sw_toggle_tones();

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
