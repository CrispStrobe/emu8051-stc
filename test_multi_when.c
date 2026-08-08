/* test_multi_when.c — test cooperative scheduling with Timer 0 interrupts.
 *
 * The 04-multi-when firmware has two concurrent tasks:
 *   Task 0: LED1 (P1.0) blinks at 200 ms half-period
 *   Task 1: LED2 (P1.1) blinks at 500 ms half-period
 *
 * This exercises Timer 0 interrupts (ET0 + EA), the ISR at vector 0x0B,
 * and the cooperative yield-based scheduler. It's the pattern that all
 * multi-WHEN BrickWright programs use.
 *
 * Build: gcc -O2 -o test_multi_when test_multi_when.c core.c opcodes.c disasm.c stc12.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;

static void test_exception(struct em8051 *aCPU, int aCode) {
    (void)aCPU; (void)aCode;
}

static void setup(void) {
    memset(&cpu, 0, sizeof(cpu));
    cpu.mCodeMemMaxIdx = 65535;
    cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535;
    cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    cpu.except = test_exception;
    reset(&cpu, 1);
    stc12_init(&cpu, &stc);
    cpu.skip_timers = true;
}

static int run_ms(int ms) {
    /* Run approximately ms milliseconds of simulated time.
     * At FOSC=11059200, 1 ms = 11059.2 osc clocks. */
    int clocks = (int)((double)ms * 11059200.0 / 1000.0);
    int count = 0;
    for (int i = 0; i < clocks; i++) {
        bool ticked = tick(&cpu);
        stc12_tick(&cpu, &stc);
        if (ticked) count++;
    }
    return count;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <hex-file>\n", argv[0]);
        return 1;
    }

    setup();

    int rc = load_obj(&cpu, argv[1]);
    if (rc != 0) {
        fprintf(stderr, "Failed to load %s (error %d)\n", argv[1], rc);
        return 1;
    }
    printf("Loaded %s\n\n", argv[1]);

    /* Run init: port mode setup, timer config, interrupt enable.
     * Note: bw_now() temporarily clears ET0 for atomic 16-bit read,
     * so we check that EA was set and that TR0 is running. ET0 may be
     * transiently 0 if we sample during a bw_now() call. */
    run_ms(5);

    uint8_t ie = cpu.mSFR[REG_IE];
    printf("IE = 0x%02X\n", ie);
    printf("  EA  (bit 7) = %d (expect 1)\n", (ie >> 7) & 1);
    assert(ie & 0x80); /* EA must be set */

    /* Timer should be running */
    assert(cpu.mSFR[REG_TCON] & TCONMASK_TR0);
    printf("  TR0 = 1 (timer running)\n");

    /* Verify ET0 is set when not inside bw_now(): run a few more clocks
     * and sample until we catch it set (it's only cleared for ~3 clocks
     * per bw_now() call). */
    int et0_seen = 0;
    for (int i = 0; i < 1000; i++) {
        tick(&cpu); stc12_tick(&cpu, &stc);
        if (cpu.mSFR[REG_IE] & 0x02) { et0_seen = 1; break; }
    }
    assert(et0_seen);
    printf("  ET0 = 1 (confirmed after sampling)\n");
    printf("PASS: Interrupt setup — EA=1, ET0=1, TR0=1\n\n");

    /* At this point, bw_ms should be incrementing via interrupts.
     * Both LEDs should be ON (tasks set them on immediately).
     * LED1 (P1.0) = 0 (active low ON), LED2 (P1.1) = 0 (active low ON) */
    int led1 = (cpu.mSFR[REG_P1] >> 0) & 1;
    int led2 = (cpu.mSFR[REG_P1] >> 1) & 1;
    printf("After init: LED1=%d LED2=%d\n", led1, led2);
    assert(led1 == 0 && led2 == 0);
    printf("PASS: Both LEDs ON after init\n\n");

    /* Run 200 ms — LED1 should toggle OFF, LED2 still ON */
    printf("Running 200 ms...\n");
    run_ms(200);
    led1 = (cpu.mSFR[REG_P1] >> 0) & 1;
    led2 = (cpu.mSFR[REG_P1] >> 1) & 1;
    printf("  LED1=%d (expect 1=OFF), LED2=%d (expect 0=ON)\n", led1, led2);
    assert(led1 == 1); /* LED1 toggles at 200ms */
    assert(led2 == 0); /* LED2 stays ON until 500ms */
    printf("PASS: LED1 OFF at 200ms, LED2 still ON\n\n");

    /* Track LED transitions by running in 10ms steps.
     * This is more robust than sampling at exact times, since
     * the cooperative scheduler adds a few ms of jitter. */
    int led1_toggles = 0, led2_toggles = 0;
    int prev_led1 = led1, prev_led2 = led2;

    for (int t = 10; t <= 1200; t += 10) {
        run_ms(10);
        led1 = (cpu.mSFR[REG_P1] >> 0) & 1;
        led2 = (cpu.mSFR[REG_P1] >> 1) & 1;
        if (led1 != prev_led1) {
            led1_toggles++;
            printf("  LED1 toggled at ~%d ms (now %s)\n", t, led1 ? "OFF" : "ON");
            prev_led1 = led1;
        }
        if (led2 != prev_led2) {
            led2_toggles++;
            printf("  LED2 toggled at ~%d ms (now %s)\n", t, led2 ? "OFF" : "ON");
            prev_led2 = led2;
        }
    }

    printf("\nLED1 toggles in 1200ms: %d (expect ~6 at 200ms period)\n", led1_toggles);
    printf("LED2 toggles in 1200ms: %d (expect ~2 at 500ms period)\n", led2_toggles);

    /* LED1 at 200ms half-period: ~6 toggles in 1200ms (±1 for jitter) */
    assert(led1_toggles >= 5 && led1_toggles <= 7);
    printf("PASS: LED1 toggles at ~200ms rate\n");

    /* LED2 at 500ms half-period: ~2 toggles in 1200ms (±1) */
    assert(led2_toggles >= 1 && led2_toggles <= 3);
    printf("PASS: LED2 toggles at ~500ms rate\n");

    /* The two rates are independent — LED1 toggles ~3x more than LED2 */
    assert(led1_toggles > led2_toggles);
    printf("PASS: LED1 toggles faster than LED2 (independent scheduling)\n");

    printf("\n=== 04-multi-when test complete ===\n");
    printf("Timer 0 interrupt-driven cooperative scheduling works.\n");
    printf("Two concurrent tasks blink independently at 200ms and 500ms.\n");

    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
    return 0;
}
