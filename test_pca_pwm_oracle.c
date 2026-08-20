/* test_pca_pwm_oracle.c — PCA/PWM absolute-value duty-cycle verification.
 *
 * Verifies the PWM comparator polarity, double-buffered reload, 9-bit
 * endpoints, and absolute duty cycle at 5 compare values against the
 * datasheet formula: duty_high = (256 − compare) / 256.
 *
 * Build: gcc -O2 -o test_pca_pwm_oracle test_pca_pwm_oracle.c core.c opcodes.c disasm.c stc12.c
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

#define MAX_EDGES 16384
static uint64_t edge_ns[MAX_EDGES];
static int      edge_drive[MAX_EDGES];
static int      n_edges;

static void pin_cb(int port, int bit, enum stc12_pin_mode mode,
                    bool drive_high, void *ud)
{
    (void)mode; (void)ud;
    if (port == 1 && bit == 3 && n_edges < MAX_EDGES) {
        edge_ns[n_edges] = stc12_get_time_ns(&stc);
        edge_drive[n_edges] = drive_high ? 1 : 0;
        n_edges++;
    }
}

static void setup(void) {
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
    cpu.mMachineCycleScale = 1;
    cpu.skip_timers = true;
    n_edges = 0;
    stc12_set_board_callbacks(&stc, pin_cb, NULL, NULL, NULL, NULL);
}

static void teardown(void) {
    free(cpu.mCodeMem); free(cpu.mExtData); free(cpu.mUpperData);
}

static void run_ns(uint64_t target_ns) {
    while (stc12_get_time_ns(&stc) < target_ns) {
        tick(&cpu); stc12_tick(&cpu, &stc);
    }
}

/* Measure duty cycle from edge timestamps.
 * Returns fraction of time HIGH over complete periods. */
static double measure_duty(int *out_periods) {
    /* Find complete periods: rising→falling→rising triplets */
    uint64_t total_high = 0, total_period = 0;
    int periods = 0;

    for (int i = 0; i < n_edges - 2; i++) {
        /* Rising edge at i */
        if (edge_drive[i] == 1 && i + 2 < n_edges) {
            /* Find next falling edge */
            int fall = -1;
            for (int j = i + 1; j < n_edges; j++) {
                if (edge_drive[j] == 0) { fall = j; break; }
            }
            if (fall < 0) break;
            /* Find next rising edge */
            int rise2 = -1;
            for (int j = fall + 1; j < n_edges; j++) {
                if (edge_drive[j] == 1) { rise2 = j; break; }
            }
            if (rise2 < 0) break;

            uint64_t high_time = edge_ns[fall] - edge_ns[i];
            uint64_t period = edge_ns[rise2] - edge_ns[i];
            total_high += high_time;
            total_period += period;
            periods++;
            i = rise2 - 1; /* advance to next period */
        }
    }

    if (out_periods) *out_periods = periods;
    if (total_period == 0) return -1.0;
    return (double)total_high / total_period;
}

/* ================================================================== *
 * Set up PCA PWM: CPS=SYSclk/12 (CPS=000), module 0 on P1.3         *
 * ================================================================== */
static void setup_pca_pwm(uint8_t compare) {
    /* CMOD: CPS=000 (SYSclk/12), no CIDL, no ECF */
    cpu.mSFR[STC_REG_CMOD] = 0x00;

    /* CCAPM0: ECOM + PWM = 0x42 (8-bit PWM mode) */
    cpu.mSFR[STC_REG_CCAPM0] = 0x42;

    /* Set both L and H to the same value (no double-buffer delay) */
    cpu.mSFR[STC_REG_CCAP0L] = compare;
    cpu.mSFR[STC_REG_CCAP0H] = compare;

    /* Clear PCA counter */
    cpu.mSFR[STC_REG_CL] = 0;
    cpu.mSFR[STC_REG_CH] = 0;

    /* Start PCA counter: CCON.CR = 1 */
    cpu.mSFR[STC_REG_CCON] = CCON_CR;
}

/* ================================================================== *
 * Test 1: PWM duty cycle at 5 compare values                         *
 * ================================================================== */
static void test_pwm_duty_sweep(void) {
    printf("\n=== PWM duty cycle sweep (SYSclk/12, module 0 on P1.3) ===\n\n");
    printf("| Compare | Expected duty | Measured duty | Error   | Periods |\n");
    printf("|---------|--------------|---------------|---------|--------|\n");

    /* Datasheet: duty_high = (256 - compare) / 256
     * compare=0   → duty=100%  (permanently high)
     * compare=64  → duty=75%
     * compare=128 → duty=50%
     * compare=192 → duty=25%
     * compare=255 → duty=0.39% (1/256 cycle high) */
    struct { uint8_t compare; double expected; } tests[] = {
        { 0,   1.0 },       /* always high — {0,CL} always >= {0,0} */
        { 64,  0.75 },
        { 128, 0.5 },
        { 192, 0.25 },
        { 255, 1.0/256.0 }, /* almost always low */
    };
    int n = sizeof(tests) / sizeof(tests[0]);

    for (int i = 0; i < n; i++) {
        setup();
        setup_pca_pwm(tests[i].compare);

        /* Run 5ms — at SYSclk/12 = 921.6 kHz, PCA period = 256 ticks
         * = 277.8 µs. In 5ms we get ~18 complete PWM periods. */
        run_ns(5000000ULL);

        int periods;
        double duty = measure_duty(&periods);

        double error = -1;
        bool ok;
        if (tests[i].expected >= 0.999) {
            /* 100% duty: should have 0 falling edges on P1.3
             * (it stays permanently high) */
            ok = (n_edges <= 1); /* at most 1 initial rising edge */
            if (ok) duty = 1.0;
            error = 0;
        } else {
            error = fabs(duty - tests[i].expected);
            ok = (error < 0.02); /* 2% tolerance */
        }

        printf("| %7d | %12.4f | %13.4f | %7.4f | %6d |\n",
               tests[i].compare, tests[i].expected, duty, error, periods);

        char msg[80];
        snprintf(msg, sizeof(msg), "PWM compare=%d: duty=%.4f (exp %.4f, err %.4f)",
                 tests[i].compare, duty, tests[i].expected, error);
        CHECK(ok, msg);

        teardown();
    }
}

/* ================================================================== *
 * Test 2: PWM polarity — verify the datasheet's inverted convention   *
 *                                                                      *
 * "A LARGER compare value means a LONGER low time."                    *
 * compare=64 → 75% high; compare=192 → 25% high.                     *
 * ================================================================== */
static void test_pwm_polarity(void) {
    printf("\n=== PWM polarity trap ===\n");

    /* Run compare=64 and compare=192, verify duty ordering */
    double duty_64, duty_192;

    setup();
    setup_pca_pwm(64);
    run_ns(5000000ULL);
    int p1;
    duty_64 = measure_duty(&p1);
    teardown();

    setup();
    setup_pca_pwm(192);
    run_ns(5000000ULL);
    int p2;
    duty_192 = measure_duty(&p2);
    teardown();

    printf("  compare=64:  duty=%.4f\n", duty_64);
    printf("  compare=192: duty=%.4f\n", duty_192);
    CHECK(duty_64 > duty_192,
          "polarity: compare=64 has HIGHER duty than compare=192");
    CHECK(duty_64 > 0.7 && duty_64 < 0.8,
          "polarity: compare=64 duty ≈ 75%");
    CHECK(duty_192 > 0.2 && duty_192 < 0.3,
          "polarity: compare=192 duty ≈ 25%");
}

/* ================================================================== *
 * Test 3: Double-buffered reload                                       *
 *                                                                      *
 * Write a new duty to CCAPnH while PWM is running. The old duty        *
 * should continue until CL overflows, then the new one takes effect.   *
 * ================================================================== */
static void test_double_buffer(void) {
    printf("\n=== Double-buffered CCAPnH reload ===\n");
    setup();

    /* Start with compare=128 (50% duty) */
    setup_pca_pwm(128);

    /* Run a few PWM periods */
    run_ns(2000000ULL);

    int periods_before;
    double duty_before = measure_duty(&periods_before);
    printf("  Before: duty=%.4f (%d periods)\n", duty_before, periods_before);
    CHECK(fabs(duty_before - 0.5) < 0.02,
          "double-buffer: starts at 50% duty");

    /* Now write CCAPnH=192 (25% duty), but CCAPnL should stay at 128
     * until the next CL overflow */
    cpu.mSFR[STC_REG_CCAP0H] = 192;

    /* The current CL is somewhere mid-cycle. The new value won't
     * take effect until the current period ends. Run enough for
     * the transition + several new periods. */
    n_edges = 0;
    run_ns(5000000ULL);

    int periods_after;
    double duty_after = measure_duty(&periods_after);
    printf("  After CCAPnH=192: duty=%.4f (%d periods)\n", duty_after, periods_after);

    /* The duty should now be ~25% (the new value) */
    CHECK(fabs(duty_after - 0.25) < 0.03,
          "double-buffer: reloaded to 25% duty");
    CHECK(periods_after > 5,
          "double-buffer: enough periods measured after reload");

    teardown();
}

/* ================================================================== *
 * Test 4: 9-bit endpoints — EPCnL for permanently-low                 *
 *                                                                      *
 * {EPCnL=1, CCAPnL=0x00} = compare value 256, meaning CL is ALWAYS    *
 * less than 256, so output is ALWAYS LOW.                              *
 * ================================================================== */
static void test_9bit_endpoint(void) {
    printf("\n=== 9-bit endpoint (EPCnL=1 → permanently low) ===\n");
    setup();

    /* CMOD=SYSclk/12, CCAPM0=PWM mode */
    cpu.mSFR[STC_REG_CMOD] = 0x00;
    cpu.mSFR[STC_REG_CCAPM0] = 0x42;
    cpu.mSFR[STC_REG_CCAP0L] = 0x00;
    cpu.mSFR[STC_REG_CCAP0H] = 0x00;
    /* Set EPCnL=1 (bit 0 of PCA_PWM0) and EPCnH=1 (bit 1) */
    cpu.mSFR[STC_REG_PCA_PWM0] = 0x03; /* EPCnH=1, EPCnL=1 */
    cpu.mSFR[STC_REG_CL] = 0;
    cpu.mSFR[STC_REG_CH] = 0;
    cpu.mSFR[STC_REG_CCON] = CCON_CR;

    run_ns(2000000ULL);

    /* With 9-bit compare=256, CL (0..255) is ALWAYS < 256,
     * so output should be ALWAYS LOW. No rising edges. */
    int rising = 0;
    for (int i = 0; i < n_edges; i++)
        if (edge_drive[i] == 1) rising++;

    printf("  EPCnL=1, CCAPnL=0: rising edges=%d (expect 0)\n", rising);
    CHECK(rising == 0, "9-bit: EPCnL=1 → permanently low (no rising edges)");

    teardown();
}

/* ================================================================== *
 * Test 5: PCA clock sources — absolute PWM frequency                  *
 *                                                                      *
 * Verify the PWM period matches FOSC / prescaler / 256.                *
 * ================================================================== */
static void test_pwm_frequency(void) {
    printf("\n=== PWM frequency vs clock source ===\n");
    printf("| CPS | Prescaler | Expected Hz | Measured Hz | Error |\n");
    printf("|-----|-----------|------------|-------------|-------|\n");

    struct { uint8_t cmod; int prescaler; const char *name; } clocks[] = {
        { 0x00, 12, "SYSclk/12" },   /* CPS=000 */
        { 0x02, 2,  "SYSclk/2" },    /* CPS=001 */
        { 0x08, 1,  "SYSclk" },      /* CPS=100 */
        { 0x0A, 4,  "SYSclk/4" },    /* CPS=101 */
    };
    int n = sizeof(clocks) / sizeof(clocks[0]);

    for (int i = 0; i < n; i++) {
        setup();

        cpu.mSFR[STC_REG_CMOD] = clocks[i].cmod;
        cpu.mSFR[STC_REG_CCAPM0] = 0x42; /* PWM mode */
        cpu.mSFR[STC_REG_CCAP0L] = 128;  /* 50% duty for clear edges */
        cpu.mSFR[STC_REG_CCAP0H] = 128;
        cpu.mSFR[STC_REG_CL] = 0;
        cpu.mSFR[STC_REG_CH] = 0;
        cpu.mSFR[STC_REG_CCON] = CCON_CR;

        /* Run enough time for at least 10 periods */
        double expected_hz = (double)stc.fosc / clocks[i].prescaler / 256.0;
        uint64_t run_time = (uint64_t)(20.0 / expected_hz * 1e9);
        if (run_time < 1000000) run_time = 1000000;
        run_ns(run_time);

        /* Measure frequency from rising edges */
        int rising_count = 0;
        uint64_t first_rise = 0, last_rise = 0;
        for (int j = 0; j < n_edges; j++) {
            if (edge_drive[j] == 1) {
                if (rising_count == 0) first_rise = edge_ns[j];
                last_rise = edge_ns[j];
                rising_count++;
            }
        }

        double meas_hz = 0;
        if (rising_count > 2) {
            double period = (double)(last_rise - first_rise) / (rising_count - 1);
            meas_hz = 1e9 / period;
        }
        double error_pct = (meas_hz > 0) ? fabs(meas_hz - expected_hz) / expected_hz * 100 : 100;

        printf("| %3d | %-9s | %10.1f | %11.1f | %4.2f%% |\n",
               (clocks[i].cmod & 0x0E) >> 1, clocks[i].name,
               expected_hz, meas_hz, error_pct);

        char msg[80];
        snprintf(msg, sizeof(msg), "PWM freq CPS=%s: %.0f Hz (exp %.0f, err %.2f%%)",
                 clocks[i].name, meas_hz, expected_hz, error_pct);
        CHECK(error_pct < 1.0, msg);

        teardown();
    }
}

int main(void) {
    printf("=== PCA/PWM Absolute-Value Oracle ===\n");
    printf("FOSC = %d Hz\n", 11059200);

    test_pwm_duty_sweep();
    test_pwm_polarity();
    test_double_buffer();
    test_9bit_endpoint();
    test_pwm_frequency();

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
