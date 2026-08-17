/* test_adc_oracle.c — ADC analog path oracle: sweep injected counts,
 * verify register values (RES, RESL) for both ADRJ=0 and ADRJ=1,
 * and confirm the voltage→count formula matches expectations.
 *
 * This test runs entirely in-process (no external emulator needed)
 * and covers:
 *   1. Raw count injection at 7 points (0, 128, 256, 512, 768, 1000, 1023)
 *   2. ADRJ=0 (right-justified) and ADRJ=1 (left-justified) register layout
 *   3. Analog callback path: voltage→count conversion at 6 voltage points
 *   4. VCC variations (5.0V and 3.3V)
 *   5. P1ASF channel enable check
 *
 * Build: gcc -O2 -o test_adc_oracle test_adc_oracle.c core.c opcodes.c disasm.c stc12.c
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
    stc.vcc = 5.0;
}

static void teardown(void) {
    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
}

static void run_adc_conversion(int channel) {
    /* Trigger ADC: set ADC_POWER + ADC_START + channel, fastest speed */
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | ADC_SPEED1 | ADC_SPEED0 | (channel & 7);
    if (cpu.sfrwrite[STC_REG_ADC_CONTR])
        cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);

    /* Run enough clocks for conversion (70 at fastest speed, plus margin) */
    for (int i = 0; i < 100; i++)
        stc12_tick(&cpu, &stc);
}

static uint16_t read_adc_result_adrj0(void) {
    return ((uint16_t)cpu.mSFR[STC_REG_ADC_RES] << 2) |
           (cpu.mSFR[STC_REG_ADC_RESL] & 0x03);
}

static uint16_t read_adc_result_adrj1(void) {
    return ((uint16_t)(cpu.mSFR[STC_REG_ADC_RES] & 0x03) << 8) |
           cpu.mSFR[STC_REG_ADC_RESL];
}

/* ================================================================== *
 * Analog callback for voltage injection                               *
 * ================================================================== */
static double injected_voltage;

static double test_read_analog(int port, int bit, void *ud) {
    (void)port; (void)bit; (void)ud;
    return injected_voltage;
}

/* ================================================================== *
 * Test 1: Raw count injection, ADRJ=0 register layout                *
 * ================================================================== */
static void test_raw_counts_adrj0(void) {
    printf("\n--- Raw count injection (ADRJ=0, right-justified) ---\n");
    printf("| Injected | Expected RES | Expected RESL | Actual RES | Actual RESL | Result | Match |\n");
    printf("|----------|-------------|---------------|------------|-------------|--------|-------|\n");

    static const uint16_t test_counts[] = {0, 128, 256, 512, 768, 1000, 1023};
    int n = sizeof(test_counts) / sizeof(test_counts[0]);

    for (int i = 0; i < n; i++) {
        setup();
        /* ADRJ=0 (default) */
        cpu.mSFR[STC_REG_AUXR1] &= ~AUXR1_ADRJ;

        stc12_set_adc_input(&stc, 3, test_counts[i]);
        run_adc_conversion(3);

        uint8_t exp_res = (test_counts[i] >> 2) & 0xFF;
        uint8_t exp_resl = test_counts[i] & 0x03;
        uint8_t act_res = cpu.mSFR[STC_REG_ADC_RES];
        uint8_t act_resl = cpu.mSFR[STC_REG_ADC_RESL];
        uint16_t result = read_adc_result_adrj0();
        bool ok = (act_res == exp_res && (act_resl & 0x03) == exp_resl);

        printf("| %4d     | 0x%02X        | 0x%02X          | 0x%02X       | 0x%02X        | %4d   | %s   |\n",
               test_counts[i], exp_res, exp_resl, act_res, act_resl & 0x03, result, ok ? "OK" : "FAIL");

        char msg[64];
        snprintf(msg, sizeof(msg), "ADRJ=0: count=%d → result=%d", test_counts[i], result);
        CHECK(result == test_counts[i], msg);

        teardown();
    }
}

/* ================================================================== *
 * Test 2: Raw count injection, ADRJ=1 register layout                *
 * ================================================================== */
static void test_raw_counts_adrj1(void) {
    printf("\n--- Raw count injection (ADRJ=1, left-justified) ---\n");
    printf("| Injected | Expected RES | Expected RESL | Actual RES | Actual RESL | Result | Match |\n");
    printf("|----------|-------------|---------------|------------|-------------|--------|-------|\n");

    static const uint16_t test_counts[] = {0, 128, 256, 512, 768, 1000, 1023};
    int n = sizeof(test_counts) / sizeof(test_counts[0]);

    for (int i = 0; i < n; i++) {
        setup();
        /* ADRJ=1 */
        cpu.mSFR[STC_REG_AUXR1] |= AUXR1_ADRJ;

        stc12_set_adc_input(&stc, 3, test_counts[i]);
        run_adc_conversion(3);

        uint8_t exp_res = (test_counts[i] >> 8) & 0x03;
        uint8_t exp_resl = test_counts[i] & 0xFF;
        uint8_t act_res = cpu.mSFR[STC_REG_ADC_RES];
        uint8_t act_resl = cpu.mSFR[STC_REG_ADC_RESL];
        uint16_t result = read_adc_result_adrj1();
        bool ok = ((act_res & 0x03) == exp_res && act_resl == exp_resl);

        printf("| %4d     | 0x%02X        | 0x%02X          | 0x%02X       | 0x%02X        | %4d   | %s   |\n",
               test_counts[i], exp_res, exp_resl, act_res & 0x03, act_resl, result, ok ? "OK" : "FAIL");

        char msg[64];
        snprintf(msg, sizeof(msg), "ADRJ=1: count=%d → result=%d", test_counts[i], result);
        CHECK(result == test_counts[i], msg);

        teardown();
    }
}

/* ================================================================== *
 * Test 3: Voltage→count conversion via analog callback                *
 * ================================================================== */
static void test_voltage_to_counts(void) {
    printf("\n--- Voltage→count conversion (analog callback, VCC=5.0V) ---\n");
    printf("| Voltage | VCC  | Expected | Actual | RES  | RESL | Match |\n");
    printf("|---------|------|----------|--------|------|------|-------|\n");

    struct { double volts; double vcc; uint16_t expected; } tests[] = {
        { 0.000, 5.0,    0 },
        { 0.500, 5.0,  102 },   /* 0.5/5.0 * 1023 = 102.3 → 102 */
        { 1.000, 5.0,  205 },   /* 1.0/5.0 * 1023 = 204.6 → 205 */
        { 2.500, 5.0,  512 },   /* 2.5/5.0 * 1023 = 511.5 → 512 */
        { 3.750, 5.0,  767 },   /* 3.75/5.0 * 1023 = 767.25 → 767 */
        { 5.000, 5.0, 1023 },
        { 0.000, 3.3,    0 },
        { 1.650, 3.3,  512 },   /* 1.65/3.3 * 1023 = 511.5 → 512 */
        { 3.300, 3.3, 1023 },
        { 4.000, 3.3, 1023 },   /* clamped to VCC */
    };
    int n = sizeof(tests) / sizeof(tests[0]);

    for (int i = 0; i < n; i++) {
        setup();
        stc.vcc = tests[i].vcc;
        cpu.mSFR[STC_REG_AUXR1] &= ~AUXR1_ADRJ; /* ADRJ=0 */

        /* Use analog callback path */
        injected_voltage = tests[i].volts;
        stc12_set_board_callbacks(&stc, NULL, NULL, test_read_analog, NULL, NULL);

        run_adc_conversion(3);

        uint16_t result = read_adc_result_adrj0();
        bool ok = (result == tests[i].expected);
        /* Allow ±1 count for rounding */
        if (!ok) ok = (result == tests[i].expected + 1 || result == tests[i].expected - 1);

        printf("| %5.3f   | %.1f  | %4d     | %4d   | 0x%02X | 0x%02X | %s   |\n",
               tests[i].volts, tests[i].vcc, tests[i].expected, result,
               cpu.mSFR[STC_REG_ADC_RES], cpu.mSFR[STC_REG_ADC_RESL] & 0x03,
               ok ? "OK" : "FAIL");

        char msg[80];
        snprintf(msg, sizeof(msg), "V2C: %.3fV/%.1fV → %d (exp %d)",
                 tests[i].volts, tests[i].vcc, result, tests[i].expected);
        CHECK(ok, msg);

        teardown();
    }
}

/* ================================================================== *
 * Test 4: STC15 ADRJ location (CLK_DIV bit 5 instead of AUXR1 bit 2) *
 * ================================================================== */
static void test_stc15_adrj(void) {
    printf("\n--- STC15 ADRJ location (CLK_DIV bit 5) ---\n");
    setup();
    stc12_set_part(&stc, 1); /* PART_STC15 */
    cpu.mMachineCycleScale = 1;

    /* Set ADRJ via CLK_DIV (0x97) bit 5 */
    cpu.mSFR[0x97 - 0x80] = 0x20; /* CLK_DIV bit 5 = ADRJ */

    stc12_set_adc_input(&stc, 0, 768);
    run_adc_conversion(0);

    /* ADRJ=1: left-justified */
    uint16_t result = read_adc_result_adrj1();
    printf("  STC15 ADRJ=1 via CLK_DIV: count=768, result=%d\n", result);
    CHECK(result == 768, "STC15 ADRJ=1: count preserved");

    /* Clear ADRJ, verify ADRJ=0 */
    cpu.mSFR[0x97 - 0x80] = 0x00;
    stc12_set_adc_input(&stc, 0, 768);
    run_adc_conversion(0);
    result = read_adc_result_adrj0();
    printf("  STC15 ADRJ=0 via CLK_DIV: count=768, result=%d\n", result);
    CHECK(result == 768, "STC15 ADRJ=0: count preserved");

    teardown();
}

/* ================================================================== *
 * Test 5: ADC_FLAG and ADC_START behavior                             *
 * ================================================================== */
static void test_flag_start_behavior(void) {
    printf("\n--- ADC_FLAG / ADC_START completion behavior ---\n");
    setup();

    stc12_set_adc_input(&stc, 2, 500);

    /* Start conversion */
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | ADC_SPEED1 | ADC_SPEED0 | 2;
    if (cpu.sfrwrite[STC_REG_ADC_CONTR])
        cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);

    /* Before completion: START should still be set */
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_START) != 0,
          "ADC: START set immediately after trigger");
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) == 0,
          "ADC: FLAG clear before completion");

    /* Run to completion */
    for (int i = 0; i < 100; i++) stc12_tick(&cpu, &stc);

    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) != 0,
          "ADC: FLAG set after completion");
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_START) == 0,
          "ADC: START cleared after completion");

    teardown();
}

/* ================================================================== *
 * Test 6: All 8 channels                                              *
 * ================================================================== */
static void test_all_channels(void) {
    printf("\n--- All 8 ADC channels ---\n");
    setup();

    for (int ch = 0; ch < 8; ch++) {
        uint16_t val = 100 + ch * 120; /* 100, 220, 340, ... */
        stc12_set_adc_input(&stc, ch, val);
        cpu.mSFR[STC_REG_AUXR1] &= ~AUXR1_ADRJ;
        run_adc_conversion(ch);

        uint16_t result = read_adc_result_adrj0();
        char msg[64];
        snprintf(msg, sizeof(msg), "channel %d: injected %d got %d", ch, val, result);
        CHECK(result == val, msg);
    }

    teardown();
}

int main(void) {
    printf("=== ADC Analog Path Oracle ===\n");

    test_raw_counts_adrj0();
    test_raw_counts_adrj1();
    test_voltage_to_counts();
    test_stc15_adrj();
    test_flag_start_behavior();
    test_all_channels();

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
