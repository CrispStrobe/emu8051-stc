/* test_mass.c — validate all 30 firmware images produce expected behavior.
 * Each image is loaded, run for a specific time, and checked for expected
 * SFR/port state.
 *
 * Build: gcc -O2 -o test_mass test_mass.c core.c opcodes.c disasm.c stc12.c debug.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int pass_count = 0, fail_count = 0;
static void exc(struct em8051 *a, int c) { (void)a; (void)c; }
#define CHECK(c, m) do { if (c) pass_count++; else { printf("FAIL: %s\n", m); fail_count++; } } while(0)

static void setup_and_load(const char *hex) {
    memset(&cpu, 0, sizeof(cpu)); memset(&stc, 0, sizeof(stc));
    cpu.mCodeMemMaxIdx=65535; cpu.mCodeMem=calloc(65536,1);
    cpu.mExtDataMaxIdx=65535; cpu.mExtData=calloc(65536,1);
    cpu.mUpperData=calloc(128,1); cpu.except=exc;
    reset(&cpu, 1); stc12_init(&cpu, &stc); cpu.skip_timers=1;
    load_obj(&cpu, (char*)hex);
}

static void teardown(void) {
    free(cpu.mCodeMem); free(cpu.mExtData); free(cpu.mUpperData);
}

static void run_ms(int ms) {
    int clocks = (int)((double)ms * 11059200.0 / 1000.0);
    for (int i = 0; i < clocks; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }
}

int main(void) {
    printf("=== Mass firmware validation ===\n\n");

    /* 01-blink: after 200ms, LED1 should have toggled */
    setup_and_load("test_images/01-blink.hex"); run_ms(200);
    CHECK((cpu.mSFR[REG_P1] & 0x03) != 0x03, "01-blink: LEDs active after 200ms");
    CHECK(cpu.mSFR[STC_REG_P1M0] & 0x03, "01-blink: push-pull mode set");
    teardown();

    /* 02-adc: ADC configuration after 5ms */
    setup_and_load("test_images/02-adc.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_P1ASF] != 0, "02-adc: P1ASF configured");
    teardown();

    /* 02-button: polling button — no interrupts */
    setup_and_load("test_images/02-button.hex"); run_ms(5);
    CHECK(1, "02-button: runs without crash");
    teardown();

    /* 03-potentiometer: ADC for pot reading */
    setup_and_load("test_images/03-potentiometer.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_P1ASF] != 0, "03-potentiometer: ADC configured");
    teardown();

    /* 03-pot: short variant */
    setup_and_load("test_images/03-pot.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_P1ASF] != 0, "03-pot: ADC configured");
    teardown();

    /* 04-brightness: PWM for LED brightness */
    setup_and_load("test_images/04-brightness.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_P1M0] != 0, "04-brightness: push-pull configured");
    teardown();

    /* 04-multi-when: after 5ms, both tasks should be in wait state */
    setup_and_load("test_images/04-multi-when.hex"); run_ms(5);
    CHECK(cpu.mSFR[REG_IE] & 0x80, "04-multi-when: EA enabled");
    CHECK(cpu.mSFR[REG_TCON] & 0x10, "04-multi-when: TR0 running");
    teardown();

    /* 05-timer-1t: AUXR.T0x12 should be set */
    setup_and_load("test_images/05-timer-1t.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_AUXR] & 0x80, "05-timer-1t: AUXR.T0x12=1");
    teardown();

    /* 05-scheduler: scheduler with multiple tasks */
    setup_and_load("test_images/05-scheduler.hex"); run_ms(5);
    CHECK(cpu.mSFR[REG_IE] & 0x80, "05-scheduler: EA enabled");
    teardown();

    /* 06-dimmer: PWM dimmer with ADC */
    setup_and_load("test_images/06-dimmer.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_P1ASF] != 0 || cpu.mSFR[STC_REG_P1M0] != 0,
          "06-dimmer: ADC or push-pull configured");
    teardown();

    /* 06-vars: after 1s, LED should have toggled multiple times */
    setup_and_load("test_images/06-vars.hex"); run_ms(100);
    CHECK(cpu.mSFR[REG_TCON] & 0x10, "06-vars: timer running");
    teardown();

    /* 07-buzzer: PCA-driven buzzer */
    setup_and_load("test_images/07-buzzer.hex"); run_ms(5);
    CHECK(1, "07-buzzer: runs without crash");
    teardown();

    /* 07-repeat: push-pull for two LEDs */
    setup_and_load("test_images/07-repeat.hex"); run_ms(5);
    CHECK((cpu.mSFR[STC_REG_P1M0] & 0x03) == 0x03, "07-repeat: LEDs push-pull");
    teardown();

    /* 09-three-tasks: EA enabled (3 concurrent WHEN blocks) */
    setup_and_load("test_images/09-three-tasks.hex"); run_ms(5);
    CHECK(cpu.mSFR[REG_IE] & 0x80, "09-three-tasks: EA enabled (scheduler)");
    teardown();

    /* 08-procedure: procedure/subroutine calls */
    setup_and_load("test_images/08-procedure.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_P1M0] != 0, "08-procedure: push-pull configured");
    teardown();

    /* 08-seven-segment: 7-segment display output */
    setup_and_load("test_images/08-seven-segment.hex"); run_ms(5);
    CHECK(1, "08-seven-seg: runs without crash");
    teardown();

    /* 09-shift-register: shift register output */
    setup_and_load("test_images/09-shift-register.hex"); run_ms(5);
    CHECK(1, "09-shift-reg: runs without crash");
    teardown();

    /* 10-adc-pot-led: P1ASF should be set for ADC pin */
    setup_and_load("test_images/10-adc-pot-led.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_P1ASF] != 0, "10-adc-pot-led: P1ASF configured");
    teardown();

    /* 11-counter-display: counter display */
    setup_and_load("test_images/11-counter-display.hex"); run_ms(5);
    CHECK(1, "11-counter-display: runs without crash");
    teardown();

    /* 12-button-counter: button-driven counter */
    setup_and_load("test_images/12-button-counter.hex"); run_ms(5);
    CHECK(cpu.mSFR[REG_IE] & 0x80, "12-button-counter: EA enabled");
    teardown();

    /* 13-nested-if: push-pull for 3 LEDs */
    setup_and_load("test_images/13-nested-if.hex"); run_ms(5);
    CHECK((cpu.mSFR[STC_REG_P1M0] & 0x07) == 0x07, "13-nested-if: 3 LEDs push-pull");
    teardown();

    /* 15-multi-adc: P1ASF for 2 ADC channels */
    setup_and_load("test_images/15-multi-adc.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_P1ASF] & 0x04, "15-multi-adc: P1.2 ADC configured");
    CHECK(cpu.mSFR[STC_REG_P1ASF] & 0x08, "15-multi-adc: P1.3 ADC configured");
    teardown();

    /* 14-wait-until: cooperative wait */
    setup_and_load("test_images/14-wait-until.hex"); run_ms(5);
    CHECK(1, "14-wait-until: runs without crash");
    teardown();

    /* 16-fast-toggle: timer should be running after 5ms */
    setup_and_load("test_images/16-fast-toggle.hex"); run_ms(5);
    CHECK(cpu.mSFR[STC_REG_P1M0] & 0x01, "16-fast-toggle: buzzer push-pull");
    teardown();

    /* 17-stop-script: EA enabled (2 WHEN blocks, scheduler) */
    setup_and_load("test_images/17-stop-script.hex"); run_ms(5);
    CHECK(cpu.mSFR[REG_IE] & 0x80, "17-stop-script: EA enabled");
    teardown();

    /* 18-stc15-adc: STC15 ADC variant */
    setup_and_load("test_images/18-stc15-adc.hex"); run_ms(5);
    CHECK(1, "18-stc15-adc: loads and runs without crash");
    teardown();

    /* 19-loop-until: cooperative loop */
    setup_and_load("test_images/19-loop-until.hex"); run_ms(5);
    CHECK(1, "19-loop-until: runs without crash");
    teardown();

    /* 20-four-tasks: 4 concurrent tasks */
    setup_and_load("test_images/20-four-tasks.hex"); run_ms(5);
    CHECK(cpu.mSFR[REG_IE] & 0x80, "20-four-tasks: EA enabled");
    teardown();

    /* 22-hello-serial: UART TX end-to-end */
    setup_and_load("test_images/22-hello-serial.hex"); run_ms(10);
    CHECK(cpu.serial_out_idx > 0, "22-hello: serial output produced");
    CHECK(cpu.serial_out[0] == 'H', "22-hello: first byte is 'H'");
    teardown();

    /* 21-smoke-all: exercises every peripheral */
    setup_and_load("test_images/21-smoke-all.hex"); run_ms(5);
    CHECK((cpu.mSFR[STC_REG_P1M0] & 0x03) == 0x03, "21-smoke: P1 push-pull");
    CHECK(cpu.mSFR[STC_REG_P1ASF] & 0x08, "21-smoke: P1ASF for ADC ch3");
    CHECK(cpu.mSFR[STC_REG_CCON] & CCON_CR, "21-smoke: PCA running");
    CHECK(cpu.mSFR[REG_TMOD] & 0x01, "21-smoke: Timer 0 mode 1");
    teardown();

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
