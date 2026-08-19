/* test_stc12.c — headless tests for the STC12 peripheral model.
 * Build: gcc -O2 -o test_stc12 test_stc12.c core.c opcodes.c disasm.c stc12.c
 * (no curses needed)
 *
 * Tests:
 * 1. Timer 0 in 12T mode (AUXR.7=0): timer ticks once per 12 osc clocks.
 * 2. Timer 0 in 1T mode (AUXR.7=1): timer ticks every osc clock.
 * 3. Port mode read logic.
 * 4. ADC conversion completes and sets ADC_FLAG.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;

/* Minimal exception handler (no-op) */
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

    /* Put a NOP loop at address 0 so tick() has something to execute */
    cpu.mCodeMem[0] = 0x00; /* NOP */

    reset(&cpu, 1);
    stc12_init(&cpu, &stc);
    cpu.skip_timers = true;
}

static void teardown(void) {
    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
}

/* ------------------------------------------------------------------ */
/* Test 1: Timer 0 in 12T mode (AUXR.7 = 0)                           */
/* Timer should increment once per 12 stc12_tick() calls.              */
/* ------------------------------------------------------------------ */
static void test_timer0_12t(void) {
    setup();

    /* Configure Timer 0: mode 1 (16-bit), start it */
    cpu.mSFR[REG_TMOD] = TMODMASK_M0_0; /* mode 1 */
    cpu.mSFR[REG_TCON] |= TCONMASK_TR0; /* TR0 = 1, start */
    cpu.mSFR[STC_REG_AUXR] &= ~AUXR_T0x12; /* 12T mode */
    cpu.mSFR[REG_TL0] = 0;
    cpu.mSFR[REG_TH0] = 0;

    /* Tick 12 times — timer should increment by 1 */
    for (int i = 0; i < 12; i++)
        stc12_tick(&cpu, &stc);

    assert(cpu.mSFR[REG_TL0] == 1);
    printf("PASS: Timer 0 12T mode — 12 osc ticks = TL0 increments by 1\n");

    /* Tick another 12 — should be 2 */
    for (int i = 0; i < 12; i++)
        stc12_tick(&cpu, &stc);

    assert(cpu.mSFR[REG_TL0] == 2);
    printf("PASS: Timer 0 12T mode — 24 osc ticks = TL0 == 2\n");

    teardown();
}

/* ------------------------------------------------------------------ */
/* Test 2: Timer 0 in 1T mode (AUXR.7 = 1)                            */
/* Timer should increment every stc12_tick() call.                     */
/* ------------------------------------------------------------------ */
static void test_timer0_1t(void) {
    setup();

    cpu.mSFR[REG_TMOD] = TMODMASK_M0_0; /* mode 1 (16-bit) */
    cpu.mSFR[REG_TCON] |= TCONMASK_TR0;
    cpu.mSFR[STC_REG_AUXR] |= AUXR_T0x12; /* 1T mode */
    cpu.mSFR[REG_TL0] = 0;
    cpu.mSFR[REG_TH0] = 0;

    /* Tick 12 times — timer should increment by 12 (not 1!) */
    for (int i = 0; i < 12; i++)
        stc12_tick(&cpu, &stc);

    assert(cpu.mSFR[REG_TL0] == 12);
    printf("PASS: Timer 0 1T mode — 12 osc ticks = TL0 == 12\n");

    teardown();
}

/* ------------------------------------------------------------------ */
/* Test 3: Timer 0 1T mode overflow sets TF0                           */
/* ------------------------------------------------------------------ */
static void test_timer0_overflow(void) {
    setup();

    cpu.mSFR[REG_TMOD] = TMODMASK_M1_0; /* mode 2: 8-bit auto-reload */
    cpu.mSFR[REG_TCON] |= TCONMASK_TR0;
    cpu.mSFR[STC_REG_AUXR] |= AUXR_T0x12; /* 1T mode */
    cpu.mSFR[REG_TL0] = 0xFE;
    cpu.mSFR[REG_TH0] = 0x80; /* reload value */

    /* Two ticks to overflow: FE -> FF -> overflow (reload to 0x80) */
    stc12_tick(&cpu, &stc); /* TL0 = 0xFF */
    assert(!(cpu.mSFR[REG_TCON] & TCONMASK_TF0));
    stc12_tick(&cpu, &stc); /* TL0 overflows, reloads from TH0 */
    assert(cpu.mSFR[REG_TCON] & TCONMASK_TF0);
    assert(cpu.mSFR[REG_TL0] == 0x80);
    printf("PASS: Timer 0 1T auto-reload overflow — TF0 set, TL0 reloaded to 0x80\n");

    teardown();
}

/* ------------------------------------------------------------------ */
/* Test 4: Port mode logic                                             */
/* ------------------------------------------------------------------ */
static void test_port_modes(void) {
    setup();

    /* P1 latch = 0xFF (reset default), external = 0x00 */
    cpu.mSFR[REG_P1] = 0xFF;
    stc12_set_port_input(&stc, 1, 0x00);

    /* Mode 00 (quasi-bidi): read = latch AND external = 0xFF & 0x00 = 0x00 */
    cpu.mSFR[STC_REG_P1M1] = 0x00;
    cpu.mSFR[STC_REG_P1M0] = 0x00;
    uint8_t val = cpu.sfrread[REG_P1](&cpu, REG_P1 + 0x80);
    assert(val == 0x00);
    printf("PASS: Port P1 quasi-bidi mode — latch=FF, ext=00, read=00\n");

    /* Mode 01 (push-pull): read = latch = 0xFF regardless of external */
    cpu.mSFR[STC_REG_P1M1] = 0x00;
    cpu.mSFR[STC_REG_P1M0] = 0xFF;
    val = cpu.sfrread[REG_P1](&cpu, REG_P1 + 0x80);
    assert(val == 0xFF);
    printf("PASS: Port P1 push-pull mode — latch=FF, ext=00, read=FF\n");

    /* Mode 10 (input-only): read = external = 0x00 */
    cpu.mSFR[STC_REG_P1M1] = 0xFF;
    cpu.mSFR[STC_REG_P1M0] = 0x00;
    val = cpu.sfrread[REG_P1](&cpu, REG_P1 + 0x80);
    assert(val == 0x00);
    printf("PASS: Port P1 input-only mode — latch=FF, ext=00, read=00\n");

    /* Mixed: bit 0 push-pull, bit 1 input, rest quasi-bidi */
    cpu.mSFR[STC_REG_P1M1] = 0x02; /* bit 1 */
    cpu.mSFR[STC_REG_P1M0] = 0x01; /* bit 0 */
    cpu.mSFR[REG_P1] = 0xFF;
    stc12_set_port_input(&stc, 1, 0x55); /* 01010101 */
    val = cpu.sfrread[REG_P1](&cpu, REG_P1 + 0x80);
    /* bit 0: push-pull, latch=1 -> 1
     * bit 1: input, ext=0 -> 0
     * bit 2: quasi-bidi, latch=1 & ext=1 -> 1
     * bit 3: quasi-bidi, latch=1 & ext=0 -> 0
     * bit 4: quasi-bidi, latch=1 & ext=1 -> 1
     * bit 5: quasi-bidi, latch=1 & ext=0 -> 0
     * bit 6: quasi-bidi, latch=1 & ext=1 -> 1
     * bit 7: quasi-bidi, latch=1 & ext=0 -> 0
     * = 0101_0101 & ~0x02 | 0x01 = wait, let me compute properly */
    /* Expected: 0b01010101 with bit0=1(pp), bit1=0(input) = 0b01010101 */
    /* bit 0: push-pull -> latch bit 0 = 1 -> 1 */
    /* bit 1: input -> ext bit 1 = 0 -> 0 */
    /* bit 2: quasi -> 1 & 1 = 1 */
    /* bit 3: quasi -> 1 & 0 = 0 */
    /* bit 4: quasi -> 1 & 1 = 1 */
    /* bit 5: quasi -> 1 & 0 = 0 */
    /* bit 6: quasi -> 1 & 1 = 1 */
    /* bit 7: quasi -> 1 & 0 = 0 */
    /* = 0101_0101 = 0x55 */
    assert(val == 0x55);
    printf("PASS: Port P1 mixed modes — bit0=pp, bit1=input, rest=quasi\n");

    teardown();
}

/* ------------------------------------------------------------------ */
/* Test 5: ADC conversion                                              */
/* ------------------------------------------------------------------ */
static void test_adc(void) {
    setup();

    /* Set channel 3 input to 512 */
    stc12_set_adc_input(&stc, 3, 512);
    cpu.mSFR[STC_REG_P1ASF] = 0xFF; /* enable all ADC channels */

    /* Start ADC: power on, channel 3, speed 11 (fastest = 70 clocks) */
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | ADC_SPEED1 | ADC_SPEED0 | 3;
    /* Trigger the write callback */
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);

    /* Flag should not be set yet */
    assert(!(cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG));

    /* Tick 69 times — should still not be done */
    for (int i = 0; i < 69; i++)
        stc12_tick(&cpu, &stc);
    assert(!(cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG));

    /* One more tick — should complete */
    stc12_tick(&cpu, &stc);
    assert(cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG);
    assert(!(cpu.mSFR[STC_REG_ADC_CONTR] & ADC_START));

    /* Check result (ADRJ=0 default: high 8 in RES, low 2 in RESL) */
    /* 512 = 0x200. High 8 = 0x80, low 2 = 0x00 */
    assert(cpu.mSFR[STC_REG_ADC_RES] == 0x80);
    assert(cpu.mSFR[STC_REG_ADC_RESL] == 0x00);
    printf("PASS: ADC channel 3, input=512, ADRJ=0 — RES=0x80, RESL=0x00\n");

    /* Test ADRJ=1 */
    cpu.mSFR[STC_REG_AUXR1] |= AUXR1_ADRJ;
    stc12_set_adc_input(&stc, 5, 1023);
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | ADC_SPEED1 | ADC_SPEED0 | 5;
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);

    for (int i = 0; i < 70; i++)
        stc12_tick(&cpu, &stc);

    assert(cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG);
    /* 1023 = 0x3FF. ADRJ=1: RESL = low 8 = 0xFF, RES = high 2 = 0x03 */
    assert(cpu.mSFR[STC_REG_ADC_RESL] == 0xFF);
    assert(cpu.mSFR[STC_REG_ADC_RES] == 0x03);
    printf("PASS: ADC channel 5, input=1023, ADRJ=1 — RES=0x03, RESL=0xFF\n");

    teardown();
}

/* ------------------------------------------------------------------ */
/* Test 6: Timer 1 in 1T vs 12T                                       */
/* ------------------------------------------------------------------ */
static void test_timer1_1t_12t(void) {
    setup();

    cpu.mSFR[REG_TMOD] = TMODMASK_M0_1; /* Timer 1 mode 1 (16-bit) */
    cpu.mSFR[REG_TCON] |= TCONMASK_TR1;
    cpu.mSFR[REG_TL1] = 0;
    cpu.mSFR[REG_TH1] = 0;

    /* 12T mode for Timer 1 */
    cpu.mSFR[STC_REG_AUXR] &= ~AUXR_T1x12;
    for (int i = 0; i < 12; i++)
        stc12_tick(&cpu, &stc);
    assert(cpu.mSFR[REG_TL1] == 1);
    printf("PASS: Timer 1 12T mode — 12 ticks = TL1 == 1\n");

    /* Switch to 1T mode */
    cpu.mSFR[REG_TL1] = 0;
    stc.timer1_prescaler = 0;
    cpu.mSFR[STC_REG_AUXR] |= AUXR_T1x12;
    for (int i = 0; i < 12; i++)
        stc12_tick(&cpu, &stc);
    assert(cpu.mSFR[REG_TL1] == 12);
    printf("PASS: Timer 1 1T mode — 12 ticks = TL1 == 12\n");

    teardown();
}

int main(void) {
    printf("=== STC12 peripheral model tests ===\n\n");

    test_timer0_12t();
    test_timer0_1t();
    test_timer0_overflow();
    test_port_modes();
    test_adc();
    test_timer1_1t_12t();

    printf("\nAll tests passed.\n");
    return 0;
}
