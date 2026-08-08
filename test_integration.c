/* test_integration.c — integration tests using all available firmware images.
 *
 * Tests:
 * 1. 02-button: port input on P3.2, LED toggle via bit ops, debounce delay
 * 2. 03-potentiometer: ADC channel 2 at fast speed (0xE8), variable blink
 * 3. Timer 0 in 1T mode: synthetic test verifying AUXR.7=1 counts 12x faster
 * 4. PCA counter: verify counter increments and overflow flag
 * 5. Port mode edge cases: open-drain, mixed per-pin modes
 * 6. ADC edge cases: channel 0, min/max values, speed variations
 *
 * Build: gcc -O2 -o test_integration test_integration.c core.c opcodes.c disasm.c stc12.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int pass_count = 0;
static int fail_count = 0;

static void test_exception(struct em8051 *aCPU, int aCode) {
    (void)aCPU; (void)aCode;
}

#define PASS(msg) do { printf("PASS: %s\n", msg); pass_count++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); fail_count++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(msg); else FAIL(msg); } while(0)

static void setup(void) {
    memset(&cpu, 0, sizeof(cpu));
    memset(&stc, 0, sizeof(stc)); /* clear ALL state including preserved callbacks */
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

static void teardown(void) {
    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
}

static void run_clocks(int n) {
    for (int i = 0; i < n; i++) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);
    }
}

/* ================================================================== *
 * Test 1: 02-button — port input reading and LED toggle              *
 * ================================================================== */
static void test_button(const char *hexfile) {
    printf("\n--- test_button (%s) ---\n", hexfile);
    setup();

    int rc = load_obj(&cpu, (char*)hexfile);
    if (rc != 0) {
        printf("SKIP: could not load %s (error %d)\n", hexfile, rc);
        teardown();
        return;
    }

    /* Button is on P3.2 (active low: pressed = 0).
     * Start with button NOT pressed (P3.2 = 1). */
    stc12_set_port_input(&stc, 3, 0xFF); /* all high = nothing pressed */

    /* Run init + enter main loop */
    run_clocks(10000);

    /* After init: P1M0 should have bits 0,1 set (push-pull for LEDs) */
    CHECK((cpu.mSFR[STC_REG_P1M0] & 0x03) == 0x03,
          "02-button: P1M0 push-pull for LEDs");

    /* LED1 should be ON (P1.0 = 0), LED2 OFF (P1.1 = 1) after init */
    /* The code does: P1_0 = 0 (turn on led1) at line 44 */
    int p1_0 = (cpu.mSFR[REG_P1] >> 0) & 1;
    int p1_1 = (cpu.mSFR[REG_P1] >> 1) & 1;
    CHECK(p1_0 == 0, "02-button: LED1 ON (P1.0=0) after init");
    CHECK(p1_1 == 1, "02-button: LED2 OFF (P1.1=1) after init");

    /* The program is now in: while (!(!P3_2)) ;
     * This waits for P3.2 == 0 (button pressed).
     * Run some clocks with button not pressed — should stay in loop. */
    run_clocks(50000);
    p1_0 = (cpu.mSFR[REG_P1] >> 0) & 1;
    CHECK(p1_0 == 0, "02-button: LED1 still ON while button not pressed");

    /* Now press the button: P3.2 = 0 */
    stc12_set_port_input(&stc, 3, 0xFF & ~(1 << 2)); /* P3.2 low */

    /* Run through: detect press + debounce (20ms) + toggle */
    /* 20 ms = ~221k osc clocks. Add margin. */
    run_clocks(300000);

    /* After toggle: P1_0 = !P1_0 -> was 0, now 1. P1_1 = !P1_1 -> was 1, now 0. */
    p1_0 = (cpu.mSFR[REG_P1] >> 0) & 1;
    p1_1 = (cpu.mSFR[REG_P1] >> 1) & 1;
    CHECK(p1_0 == 1, "02-button: LED1 toggled OFF (P1.0=1) after press");
    CHECK(p1_1 == 0, "02-button: LED2 toggled ON (P1.1=0) after press");

    /* Release button: P3.2 = 1 */
    stc12_set_port_input(&stc, 3, 0xFF);

    /* Run through release debounce + wait-for-next-press loop */
    run_clocks(300000);

    /* Press again */
    stc12_set_port_input(&stc, 3, 0xFF & ~(1 << 2));
    run_clocks(300000);

    /* Should toggle back */
    p1_0 = (cpu.mSFR[REG_P1] >> 0) & 1;
    p1_1 = (cpu.mSFR[REG_P1] >> 1) & 1;
    CHECK(p1_0 == 0, "02-button: LED1 toggled back ON after 2nd press");
    CHECK(p1_1 == 1, "02-button: LED2 toggled back OFF after 2nd press");

    teardown();
}

/* ================================================================== *
 * Test 2: 03-potentiometer — ADC channel 2 at fast speed             *
 * ================================================================== */
static void test_potentiometer(const char *hexfile) {
    printf("\n--- test_potentiometer (%s) ---\n", hexfile);
    setup();

    int rc = load_obj(&cpu, (char*)hexfile);
    if (rc != 0) {
        printf("SKIP: could not load %s (error %d)\n", hexfile, rc);
        teardown();
        return;
    }

    /* Set pot on channel 2 to ~50% (512) */
    stc12_set_adc_input(&stc, 2, 512);

    /* Run init */
    run_clocks(10000);

    /* Check ADC setup: P1ASF should have bit 2 set */
    CHECK(cpu.mSFR[STC_REG_P1ASF] & 0x04,
          "03-pot: P1ASF bit 2 set (P1.2 routed to ADC)");

    /* P1M1 bit 2 set (input mode), P1M0 bit 2 clear */
    CHECK((cpu.mSFR[STC_REG_P1M1] & 0x04) != 0,
          "03-pot: P1M1 bit 2 set (high-impedance input)");
    CHECK((cpu.mSFR[STC_REG_P1M0] & 0x04) == 0,
          "03-pot: P1M0 bit 2 clear");

    /* P1M0 bit 0 set for LED push-pull */
    CHECK((cpu.mSFR[STC_REG_P1M0] & 0x01) != 0,
          "03-pot: P1M0 bit 0 set (LED push-pull)");

    /* ADC_CONTR should have been initialized to 0xE0 (power+fast, no start) */
    /* Then the first adc_read sets ADC_CONTR = 0xE8 | 2 = 0xEA */
    /* Run enough for the ADC conversion (70 clocks at fast speed) + some code */
    run_clocks(50000);

    /* The program should have read the ADC and be in the blink loop.
     * With input=512, period = 512, delay = 512 ms per half.
     * Check LED state changes. */
    int led = (cpu.mSFR[REG_P1] >> 0) & 1;
    printf("  LED (P1.0) = %d at first sample\n", led);

    /* Run ~512 ms = ~5.66M osc clocks */
    run_clocks(5700000);
    int led2 = (cpu.mSFR[REG_P1] >> 0) & 1;
    printf("  LED (P1.0) = %d after ~512 ms\n", led2);
    CHECK(led != led2, "03-pot: LED toggled after ~512 ms (input=512)");

    /* Now change the pot to minimum (10, clamped to 20 by the program) */
    stc12_set_adc_input(&stc, 2, 10);

    /* The program must finish its current blink cycle (up to 512 ms
     * remaining) then read the new ADC value on the next iteration.
     * Run 600 ms worth of clocks to get past the old blink cycle,
     * then sample twice within a 50 ms window to catch a 20 ms toggle. */
    run_clocks(6700000); /* ~600 ms flush */
    int led3 = (cpu.mSFR[REG_P1] >> 0) & 1;
    run_clocks(330000);  /* ~30 ms — should see at least one toggle at 20 ms period */
    int led4 = (cpu.mSFR[REG_P1] >> 0) & 1;
    CHECK(led3 != led4, "03-pot: LED toggles faster with low ADC input");

    teardown();
}

/* ================================================================== *
 * Test 3: Timer 0 in 1T mode — synthetic test                        *
 * ================================================================== */
static void test_timer0_1t_synthetic(void) {
    printf("\n--- test_timer0_1t_synthetic ---\n");
    setup();

    /* Program: set AUXR.7=1, start Timer 0 mode 2 (auto-reload),
     * reload value = 0xF0 (overflows every 16 ticks in 1T mode).
     * MOV AUXR,#80h  (75 8E 80)
     * MOV TMOD,#02h  (75 89 02)   -- mode 2
     * MOV TH0,#F0h   (75 8C F0)   -- reload value
     * MOV TL0,#F0h   (75 8A F0)   -- start value
     * SETB TR0       (D2 8C)      -- start timer
     * loop: SJMP loop (80 FE)     -- spin
     */
    uint8_t prog[] = {
        0x75, 0x8E, 0x80,  /* MOV AUXR,#80h */
        0x75, 0x89, 0x02,  /* MOV TMOD,#02h */
        0x75, 0x8C, 0xF0,  /* MOV TH0,#F0h */
        0x75, 0x8A, 0xF0,  /* MOV TL0,#F0h */
        0xD2, 0x8C,        /* SETB TR0 */
        0x80, 0xFE         /* SJMP $ */
    };
    memcpy(cpu.mCodeMem, prog, sizeof(prog));

    /* Run init instructions (5 MOVs + SETB = ~10 cycles each, ~60 cycles) */
    run_clocks(100);

    /* Verify AUXR.7 = 1 */
    CHECK((cpu.mSFR[STC_REG_AUXR] & 0x80) != 0,
          "1T synthetic: AUXR.T0x12 set");
    CHECK((cpu.mSFR[REG_TMOD] & 0x0F) == 0x02,
          "1T synthetic: TMOD mode 2");
    CHECK((cpu.mSFR[REG_TCON] & TCONMASK_TR0) != 0,
          "1T synthetic: TR0 set");

    /* Clear TF0, reset TL0 */
    cpu.mSFR[REG_TCON] &= ~TCONMASK_TF0;
    cpu.mSFR[REG_TL0] = 0xF0;
    stc.timer0_prescaler = 0;

    /* In 1T mode, timer increments every osc clock.
     * From 0xF0, overflow after 16 ticks (0xF0..0xFF -> overflow).
     * Run exactly 16 ticks. */
    run_clocks(16);
    CHECK((cpu.mSFR[REG_TCON] & TCONMASK_TF0) != 0,
          "1T synthetic: TF0 set after 16 ticks (1T mode)");
    /* After auto-reload, TL0 should be back to 0xF0 */
    CHECK(cpu.mSFR[REG_TL0] == 0xF0,
          "1T synthetic: TL0 reloaded to 0xF0");

    /* Now test 12T mode: set AUXR.7 = 0 */
    cpu.mSFR[STC_REG_AUXR] &= ~0x80;
    cpu.mSFR[REG_TCON] &= ~TCONMASK_TF0;
    cpu.mSFR[REG_TL0] = 0xF0;
    stc.timer0_prescaler = 0;

    /* In 12T, 16 timer increments need 16*12 = 192 osc clocks */
    run_clocks(191);
    CHECK((cpu.mSFR[REG_TCON] & TCONMASK_TF0) == 0,
          "12T synthetic: TF0 NOT set after 191 ticks");
    run_clocks(1);
    CHECK((cpu.mSFR[REG_TCON] & TCONMASK_TF0) != 0,
          "12T synthetic: TF0 set after 192 ticks (16 * 12)");

    teardown();
}

/* ================================================================== *
 * Test 4: PCA counter                                                 *
 * ================================================================== */
static void test_pca_counter(void) {
    printf("\n--- test_pca_counter ---\n");
    setup();

    /* Enable PCA: set CR in CCON, clock source = FOSC/12 */
    cpu.mSFR[STC_REG_CCON] = CCON_CR;
    cpu.mSFR[STC_REG_CMOD] = 0x00; /* CPS=00 (FOSC/12), CIDL=0, ECF=0 */
    cpu.mSFR[STC_REG_CL] = 0x00;
    cpu.mSFR[STC_REG_CH] = 0x00;

    /* Run 12 osc clocks — PCA counter should increment by 1 */
    run_clocks(12);
    uint16_t pca = cpu.mSFR[STC_REG_CL] | (cpu.mSFR[STC_REG_CH] << 8);
    CHECK(pca == 1, "PCA: counter=1 after 12 osc clocks (FOSC/12)");

    /* Run to near-overflow: set counter to 0xFFFE */
    cpu.mSFR[STC_REG_CL] = 0xFE;
    cpu.mSFR[STC_REG_CH] = 0xFF;
    cpu.mSFR[STC_REG_CCON] &= ~CCON_CF; /* clear overflow flag */
    stc.pca_prescaler = 0;

    /* 2 more PCA ticks = 24 osc clocks -> overflow */
    run_clocks(24);
    CHECK((cpu.mSFR[STC_REG_CCON] & CCON_CF) != 0,
          "PCA: CF set on counter overflow");
    pca = cpu.mSFR[STC_REG_CL] | (cpu.mSFR[STC_REG_CH] << 8);
    CHECK(pca == 0, "PCA: counter wrapped to 0 after overflow");

    /* Test FOSC/2 clock source */
    cpu.mSFR[STC_REG_CMOD] = CMOD_CPS0_BIT; /* CPS=01 (FOSC/2) */
    cpu.mSFR[STC_REG_CL] = 0x00;
    cpu.mSFR[STC_REG_CH] = 0x00;
    stc.pca_prescaler = 0;

    run_clocks(10); /* 10 osc clocks / 2 = 5 PCA ticks */
    pca = cpu.mSFR[STC_REG_CL] | (cpu.mSFR[STC_REG_CH] << 8);
    CHECK(pca == 5, "PCA: counter=5 after 10 osc clocks (FOSC/2)");

    teardown();
}

/* ================================================================== *
 * Test 5: PCA compare/match                                           *
 * ================================================================== */
static void test_pca_compare(void) {
    printf("\n--- test_pca_compare ---\n");
    setup();

    /* Set up PCA module 0 in compare mode: ECOM + MAT */
    cpu.mSFR[STC_REG_CCON] = CCON_CR;
    cpu.mSFR[STC_REG_CMOD] = CMOD_CPS0_BIT; /* FOSC/2 */
    cpu.mSFR[STC_REG_CCAPM0] = CCAPM_ECOM | CCAPM_MAT;
    cpu.mSFR[STC_REG_CCAP0L] = 0x05;
    cpu.mSFR[STC_REG_CCAP0H] = 0x00; /* compare at counter = 0x0005 */
    cpu.mSFR[STC_REG_CL] = 0x00;
    cpu.mSFR[STC_REG_CH] = 0x00;
    stc.pca_prescaler = 0;

    /* Run 8 osc clocks = 4 PCA ticks -> counter = 4, no match yet */
    run_clocks(8);
    CHECK((cpu.mSFR[STC_REG_CCON] & CCON_CCF0) == 0,
          "PCA compare: no CCF0 at counter=4");

    /* 2 more osc clocks = 1 more PCA tick -> counter = 5, match! */
    run_clocks(2);
    CHECK((cpu.mSFR[STC_REG_CCON] & CCON_CCF0) != 0,
          "PCA compare: CCF0 set at counter=5 (match)");

    teardown();
}

/* ================================================================== *
 * Test 5b: PCA 8-bit PWM mode                                        *
 * ================================================================== */
static void test_pca_pwm(void) {
    printf("\n--- test_pca_pwm ---\n");
    setup();

    /* Set up PCA module 0 in 8-bit PWM mode:
     * CCAPM0 = ECOM | PWM = 0x42
     * Set CCAP0H = 0x80 (50% duty — output low when CL < 0x80, high when >= 0x80)
     * Actually per datasheet: output is low when CL < CCAPnL, high when >= CCAPnL.
     * On CL overflow (0xFF -> 0x00), CCAPnL is reloaded from CCAPnH. */
    cpu.mSFR[STC_REG_CCON] = CCON_CR;  /* PCA running */
    cpu.mSFR[STC_REG_CMOD] = CMOD_CPS0_BIT;  /* FOSC/2 */
    cpu.mSFR[STC_REG_CCAPM0] = CCAPM_ECOM | CCAPM_PWM;
    cpu.mSFR[STC_REG_CCAP0H] = 0x80;  /* reload value */
    cpu.mSFR[STC_REG_CCAP0L] = 0x80;  /* initial compare value */
    cpu.mSFR[STC_REG_CL] = 0x00;
    cpu.mSFR[STC_REG_CH] = 0x00;
    stc.pca_prescaler = 0;

    /* Run 256 PCA ticks = 512 osc clocks to complete one CL cycle */
    run_clocks(512);
    uint8_t cl = cpu.mSFR[STC_REG_CL];
    CHECK(cl == 0x00, "PCA PWM: CL wrapped to 0 after 256 PCA ticks");

    /* CCAPnL should have been reloaded from CCAPnH on overflow */
    CHECK(cpu.mSFR[STC_REG_CCAP0L] == 0x80,
          "PCA PWM: CCAP0L reloaded from CCAP0H on CL overflow");

    /* Run to mid-cycle: 128 PCA ticks = 256 osc clocks -> CL = 0x80 */
    run_clocks(256);
    cl = cpu.mSFR[STC_REG_CL];
    CHECK(cl == 0x80, "PCA PWM: CL = 0x80 at mid-cycle");

    /* Test with different duty: CCAP0H = 0x40 (25% duty) */
    cpu.mSFR[STC_REG_CCAP0H] = 0x40;
    /* Run another full cycle to trigger reload */
    /* CL is at 0x80, needs 128 more ticks to overflow, then reload */
    run_clocks(256); /* 128 PCA ticks -> CL overflows, reloads CCAP0L from 0x40 */
    CHECK(cpu.mSFR[STC_REG_CCAP0L] == 0x40,
          "PCA PWM: CCAP0L reloaded to 0x40 after duty change");

    teardown();
}

/* ================================================================== *
 * Test 5c: PCA with Timer 0 overflow clock source                     *
 * ================================================================== */
static void test_pca_t0_clock(void) {
    printf("\n--- test_pca_t0_clock ---\n");
    setup();

    /* PCA clock = Timer 0 overflow */
    cpu.mSFR[STC_REG_CCON] = CCON_CR;
    cpu.mSFR[STC_REG_CMOD] = CMOD_CPS1_BIT;  /* CPS=10 = T0 overflow */
    cpu.mSFR[STC_REG_CL] = 0x00;
    cpu.mSFR[STC_REG_CH] = 0x00;

    /* Set up Timer 0 in mode 2 (auto-reload), 1T mode, reload=0xFE
     * so it overflows every 2 ticks */
    cpu.mSFR[STC_REG_AUXR] |= AUXR_T0x12;
    cpu.mSFR[REG_TMOD] = TMODMASK_M1_0; /* mode 2 */
    cpu.mSFR[REG_TH0] = 0xFE;
    cpu.mSFR[REG_TL0] = 0xFE;
    cpu.mSFR[REG_TCON] |= TCONMASK_TR0;
    stc.timer0_prescaler = 0;

    /* Run 4 osc clocks -> 2 T0 overflows -> PCA counter should be 2 */
    run_clocks(4);
    uint16_t pca = cpu.mSFR[STC_REG_CL] | (cpu.mSFR[STC_REG_CH] << 8);
    CHECK(pca == 2, "PCA T0-clock: counter=2 after 2 Timer 0 overflows");

    teardown();
}

/* ================================================================== *
 * Test 6: Port open-drain mode                                        *
 * ================================================================== */
static void test_port_open_drain(void) {
    printf("\n--- test_port_open_drain ---\n");
    setup();

    /* Set P2 to open-drain (M1=1, M0=1) */
    cpu.mSFR[STC_REG_P2M1] = 0xFF;
    cpu.mSFR[STC_REG_P2M0] = 0xFF;

    /* Latch = 0xFF, external = 0x55 -> read = latch AND ext = 0x55 */
    cpu.mSFR[REG_P2] = 0xFF;
    stc12_set_port_input(&stc, 2, 0x55);
    uint8_t val = cpu.sfrread[REG_P2](&cpu, REG_P2 + 0x80);
    CHECK(val == 0x55, "Open-drain: latch=FF, ext=55 -> read=55");

    /* Latch = 0x0F, external = 0xFF -> read = 0x0F (latch drives low) */
    cpu.mSFR[REG_P2] = 0x0F;
    stc12_set_port_input(&stc, 2, 0xFF);
    val = cpu.sfrread[REG_P2](&cpu, REG_P2 + 0x80);
    CHECK(val == 0x0F, "Open-drain: latch=0F, ext=FF -> read=0F");

    /* Latch = 0x00, external = 0xFF -> read = 0x00 (all pins driven low) */
    cpu.mSFR[REG_P2] = 0x00;
    val = cpu.sfrread[REG_P2](&cpu, REG_P2 + 0x80);
    CHECK(val == 0x00, "Open-drain: latch=00, ext=FF -> read=00");

    teardown();
}

/* ================================================================== *
 * Test 7: ADC edge cases                                              *
 * ================================================================== */
static void test_adc_edges(void) {
    printf("\n--- test_adc_edges ---\n");
    setup();

    /* Channel 0, input = 0 (minimum) */
    stc12_set_adc_input(&stc, 0, 0);
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | 0; /* ch 0, speed 00 */
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);
    for (int i = 0; i < 420; i++) stc12_tick(&cpu, &stc);
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) != 0,
          "ADC ch0: flag set after 420 clocks (speed 00)");
    CHECK(cpu.mSFR[STC_REG_ADC_RES] == 0x00,
          "ADC ch0: RES=0 for input=0");
    CHECK(cpu.mSFR[STC_REG_ADC_RESL] == 0x00,
          "ADC ch0: RESL=0 for input=0");

    /* Channel 7, input = 1023 (maximum), speed 01 (280 clocks) */
    stc12_set_adc_input(&stc, 7, 1023);
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | ADC_SPEED0 | 7;
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);
    for (int i = 0; i < 279; i++) stc12_tick(&cpu, &stc);
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) == 0,
          "ADC ch7: flag NOT set after 279 clocks (speed 01)");
    stc12_tick(&cpu, &stc);
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) != 0,
          "ADC ch7: flag set after 280 clocks");
    /* 1023 = 0x3FF, ADRJ=0: RES = 0xFF, RESL = 0x03 */
    CHECK(cpu.mSFR[STC_REG_ADC_RES] == 0xFF,
          "ADC ch7: RES=0xFF for input=1023");
    CHECK(cpu.mSFR[STC_REG_ADC_RESL] == 0x03,
          "ADC ch7: RESL=0x03 for input=1023");

    /* Speed 10 (140 clocks) */
    stc12_set_adc_input(&stc, 4, 256);
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | ADC_SPEED1 | 4;
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);
    for (int i = 0; i < 140; i++) stc12_tick(&cpu, &stc);
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) != 0,
          "ADC ch4: flag set after 140 clocks (speed 10)");
    /* 256 = 0x100, ADRJ=0: RES = 0x40, RESL = 0x00 */
    CHECK(cpu.mSFR[STC_REG_ADC_RES] == 0x40,
          "ADC ch4: RES=0x40 for input=256");

    /* No conversion without ADC_POWER */
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_START | 0; /* no power */
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);
    for (int i = 0; i < 500; i++) stc12_tick(&cpu, &stc);
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) == 0,
          "ADC: no conversion without ADC_POWER");

    teardown();
}

/* ================================================================== *
 * Test 8: Timer 0 mode 0 (13-bit) in both 1T and 12T                 *
 * ================================================================== */
static void test_timer0_mode0(void) {
    printf("\n--- test_timer0_mode0 (13-bit) ---\n");
    setup();

    /* Mode 0: 13-bit timer. TL0 uses lower 5 bits (0-31), overflows into TH0 */
    cpu.mSFR[REG_TMOD] = 0x00; /* mode 0 */
    cpu.mSFR[REG_TCON] |= TCONMASK_TR0;
    cpu.mSFR[STC_REG_AUXR] |= AUXR_T0x12; /* 1T mode */
    cpu.mSFR[REG_TL0] = 0x1E; /* 30, 2 ticks from overflow of lower 5 bits */
    cpu.mSFR[REG_TH0] = 0x00;

    stc12_tick(&cpu, &stc); /* TL0.lower5 = 31 */
    CHECK((cpu.mSFR[REG_TL0] & 0x1F) == 0x1F,
          "13-bit 1T: TL0 lower5 = 31 after 1 tick");
    stc12_tick(&cpu, &stc); /* TL0.lower5 overflows -> TH0 increments */
    CHECK(cpu.mSFR[REG_TH0] == 0x01,
          "13-bit 1T: TH0 incremented on TL0 lower-5 overflow");

    /* Now set TH0 to 0xFF, TL0 to max of lower 5 */
    cpu.mSFR[REG_TH0] = 0xFF;
    cpu.mSFR[REG_TL0] = 0x1F;
    cpu.mSFR[REG_TCON] &= ~TCONMASK_TF0;

    stc12_tick(&cpu, &stc); /* TL0 overflows -> TH0 overflows -> TF0 */
    CHECK((cpu.mSFR[REG_TCON] & TCONMASK_TF0) != 0,
          "13-bit 1T: TF0 set on full 13-bit overflow");

    teardown();
}

/* ================================================================== *
 * Test 9: WASM hex loader edge cases                                  *
 * ================================================================== */
static void test_hex_loader(void) {
    printf("\n--- test_hex_loader ---\n");
    setup();

    /* Load a minimal hex file inline: NOP at 0000 + EOF */
    /* We already know load_obj works from files; test edge cases */

    /* Verify code memory is zeroed */
    CHECK(cpu.mCodeMem[0] == 0x00, "Hex loader: code mem zeroed initially");

    /* Load the blink hex and verify non-zero at expected locations */
    int rc = load_obj(&cpu, "test_images/01-blink.hex");
    CHECK(rc == 0, "Hex loader: loads 01-blink.hex successfully");
    CHECK(cpu.mCodeMem[0] == 0x02, "Hex loader: LJMP at 0000");
    CHECK(cpu.mCodeMem[1] != 0x00 || cpu.mCodeMem[2] != 0x00,
          "Hex loader: LJMP target is non-zero");

    teardown();
}

/* ================================================================== *
 * Test 10: Boundary A pin-change callback                             *
 * ================================================================== */
static int cb_count = 0;
static int cb_last_port, cb_last_bit;
static enum stc12_pin_mode cb_last_mode;
static bool cb_last_drive;

static void test_pin_callback(int port, int bit,
                              enum stc12_pin_mode mode,
                              bool drive_high, void *ud)
{
    (void)ud;
    cb_count++;
    cb_last_port = port;
    cb_last_bit = bit;
    cb_last_mode = mode;
    cb_last_drive = drive_high;
}

static void test_boundary_a_callbacks(void) {
    printf("\n--- test_boundary_a_callbacks ---\n");
    setup();

    /* Register pin-change callback */
    stc12_set_board_callbacks(&stc, test_pin_callback, NULL, NULL, NULL, NULL);
    cb_count = 0;

    /* Write P1 = 0xFE (bit 0 low, bits 1-7 unchanged at 0xFF).
     * Should fire callback for bit 0 only (the one that changed). */
    cpu.mSFR[REG_P1] = 0xFE;
    cpu.sfrwrite[REG_P1](&cpu, REG_P1);

    CHECK(cb_count == 1, "Boundary A: exactly 1 callback on 1-bit change");
    CHECK(cb_last_port == 1, "Boundary A: callback port == 1");
    CHECK(cb_last_bit == 0, "Boundary A: callback bit == 0 (changed pin)");
    CHECK(cb_last_mode == PIN_QUASI, "Boundary A: mode == quasi (default)");
    CHECK(cb_last_drive == false, "Boundary A: drive == false (bit 0 = 0)");

    /* Now change mode to push-pull for P1.0 */
    cb_count = 0;
    cpu.mSFR[STC_REG_P1M0] = 0x01;
    cpu.sfrwrite[STC_REG_P1M0](&cpu, STC_REG_P1M0);

    CHECK(cb_count == 1, "Boundary A: 1 callback on 1-bit mode change");
    CHECK(cb_last_bit == 0, "Boundary A: mode change on bit 0");
    CHECK(cb_last_mode == PIN_PUSHPULL,
          "Boundary A: mode == pushpull after P1M0 change");

    /* Write P1 = 0xFF (all high) — should fire for bit 0 only (changed) */
    cb_count = 0;
    cpu.mSFR[REG_P1] = 0xFF;
    cpu.sfrwrite[REG_P1](&cpu, REG_P1);

    CHECK(cb_count == 1, "Boundary A: only changed pin fires callback");
    CHECK(cb_last_drive == true, "Boundary A: drive == true after setting high");

    /* No-change write should not fire */
    cb_count = 0;
    cpu.sfrwrite[REG_P1](&cpu, REG_P1);
    CHECK(cb_count == 0, "Boundary A: no callback on no-change write");

    teardown();
}

/* ================================================================== *
 * Test 11: advanceTo nanosecond timing                                *
 * ================================================================== */
static void test_advance_to(void) {
    printf("\n--- test_advance_to ---\n");
    setup();
    stc.fosc = 11059200;
    stc.ns_per_clock_x16 = (uint64_t)(16.0e9 / stc.fosc + 0.5);

    /* Put a NOP loop at 0 */
    cpu.mCodeMem[0] = 0x00; /* NOP */
    cpu.mCodeMem[1] = 0x80; /* SJMP */
    cpu.mCodeMem[2] = 0xFD; /* back to 0 */

    /* Advance to 1 ms */
    uint64_t target = 1000000; /* 1 ms in ns */
    int executed = stc12_advance_to(&cpu, &stc, target);
    uint64_t actual = stc12_get_time_ns(&stc);

    CHECK(actual >= target, "advanceTo: reached target time");
    CHECK(actual < target + 1000, "advanceTo: within 1 us of target");
    CHECK(executed > 0, "advanceTo: executed instructions");
    printf("  target=%llu actual=%llu instructions=%d\n",
           (unsigned long long)target, (unsigned long long)actual, executed);

    /* Advance to 10 ms */
    target = 10000000;
    executed = stc12_advance_to(&cpu, &stc, target);
    actual = stc12_get_time_ns(&stc);
    CHECK(actual >= target, "advanceTo: reached 10ms target");
    CHECK(executed > 0, "advanceTo: more instructions executed");

    teardown();
}

/* ================================================================== *
 * Test 12: Timer 0 mode 3 (two 8-bit timers)                         *
 * ================================================================== */
static void test_timer0_mode3(void) {
    printf("\n--- test_timer0_mode3 ---\n");
    setup();

    /* Mode 3: TL0 is an 8-bit timer controlled by TR0/TF0.
     *          TH0 is a separate 8-bit timer controlled by TR1/TF1.
     *          Timer 1 loses its control bits in this mode. */
    cpu.mSFR[STC_REG_AUXR] |= AUXR_T0x12; /* 1T for clear counting */
    cpu.mSFR[REG_TMOD] = TMODMASK_M0_0 | TMODMASK_M1_0; /* mode 3 */
    cpu.mSFR[REG_TCON] |= TCONMASK_TR0; /* start TL0 */
    cpu.mSFR[REG_TCON] |= TCONMASK_TR1; /* start TH0 (as second timer) */
    cpu.mSFR[REG_TL0] = 0xFD;
    cpu.mSFR[REG_TH0] = 0xFC;

    /* TL0: 3 ticks to overflow (FD -> FE -> FF -> overflow) */
    run_clocks(2);
    CHECK(cpu.mSFR[REG_TL0] == 0xFF, "Mode 3: TL0 = 0xFF after 2 ticks");
    CHECK(!(cpu.mSFR[REG_TCON] & TCONMASK_TF0), "Mode 3: TF0 not yet set");

    run_clocks(1);
    CHECK((cpu.mSFR[REG_TCON] & TCONMASK_TF0) != 0,
          "Mode 3: TF0 set on TL0 overflow");

    /* TH0: started at 0xFC, runs using TR1. After 3 TL0 ticks it should be at 0xFF.
     * TH0 increments at the same rate when TR1 is set. */
    CHECK(cpu.mSFR[REG_TH0] == 0xFF,
          "Mode 3: TH0 = 0xFF after 3 ticks (TR1-controlled)");

    /* One more tick -> TH0 overflows, sets TF1 */
    cpu.mSFR[REG_TCON] &= ~TCONMASK_TF1;
    run_clocks(1);
    CHECK((cpu.mSFR[REG_TCON] & TCONMASK_TF1) != 0,
          "Mode 3: TF1 set on TH0 overflow");

    teardown();
}

/* ================================================================== *
 * Test 13: Boundary A read callbacks (on_read_pin, on_read_analog)    *
 * ================================================================== */
static int read_pin_port, read_pin_bit;
static int read_pin_value;
static int read_pin_called;

static int test_read_pin(int port, int bit, void *ud) {
    (void)ud;
    read_pin_called++;
    read_pin_port = port;
    read_pin_bit = bit;
    return read_pin_value;
}

static int read_analog_called;
static double read_analog_volts;

static double test_read_analog(int port, int bit, void *ud) {
    (void)ud; (void)port; (void)bit;
    read_analog_called++;
    return read_analog_volts;
}

static void test_boundary_a_read_callbacks(void) {
    printf("\n--- test_boundary_a_read_callbacks ---\n");
    setup();

    /* Register read callbacks */
    stc12_set_board_callbacks(&stc, NULL, test_read_pin, test_read_analog,
                              NULL, NULL);

    /* Test readPin: set P1 latch to 0xFF, make callback return 0 for bit 3 */
    cpu.mSFR[REG_P1] = 0xFF;
    cpu.mSFR[STC_REG_P1M1] = 0x00;
    cpu.mSFR[STC_REG_P1M0] = 0x00; /* quasi-bidi: read = latch AND external */
    read_pin_value = 0; /* callback returns 0 for all pins */
    read_pin_called = 0;

    uint8_t val = cpu.sfrread[REG_P1](&cpu, REG_P1 + 0x80);
    CHECK(read_pin_called == 8, "readPin: callback called 8 times (once per bit)");
    CHECK(val == 0x00, "readPin: quasi-bidi latch=FF, ext=all-0 -> read=00");

    /* Now return 1 for all pins */
    read_pin_value = 1;
    val = cpu.sfrread[REG_P1](&cpu, REG_P1 + 0x80);
    CHECK(val == 0xFF, "readPin: quasi-bidi latch=FF, ext=all-1 -> read=FF");

    /* Push-pull mode: should return latch regardless of callback */
    cpu.mSFR[STC_REG_P1M0] = 0xFF;
    cpu.mSFR[STC_REG_P1M1] = 0x00;
    read_pin_value = 0;
    val = cpu.sfrread[REG_P1](&cpu, REG_P1 + 0x80);
    CHECK(val == 0xFF, "readPin: push-pull latch=FF -> read=FF (ignores external)");

    /* Test readAnalog: set up ADC, callback returns 2.5V on a 5V VCC */
    read_analog_called = 0;
    read_analog_volts = 2.5;
    stc.vcc = 5.0;

    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | ADC_SPEED1 | ADC_SPEED0 | 3;
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);

    /* Run conversion (70 clocks at fastest speed) */
    for (int i = 0; i < 70; i++) stc12_tick(&cpu, &stc);

    CHECK(read_analog_called > 0, "readAnalog: callback was called");
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) != 0,
          "readAnalog: ADC_FLAG set");

    /* 2.5V / 5.0V * 1023 = 511.5, rounds to 512 = 0x200
     * ADRJ=0: RES = 0x80, RESL = 0x00 */
    uint16_t result = ((uint16_t)cpu.mSFR[STC_REG_ADC_RES] << 2) |
                      (cpu.mSFR[STC_REG_ADC_RESL] & 0x03);
    printf("  readAnalog: 2.5V -> %d counts (expect ~512)\n", result);
    CHECK(result >= 511 && result <= 513,
          "readAnalog: 2.5V/5V -> ~512 counts");

    /* Test with 3.3V VCC, 1.65V input -> still ~512 */
    stc.vcc = 3.3;
    read_analog_volts = 1.65;
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | ADC_SPEED1 | ADC_SPEED0 | 3;
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);
    for (int i = 0; i < 70; i++) stc12_tick(&cpu, &stc);
    result = ((uint16_t)cpu.mSFR[STC_REG_ADC_RES] << 2) |
             (cpu.mSFR[STC_REG_ADC_RESL] & 0x03);
    printf("  readAnalog: 1.65V/3.3V -> %d counts (expect ~512)\n", result);
    CHECK(result >= 510 && result <= 514,
          "readAnalog: 1.65V/3.3V -> ~512 counts");

    teardown();
}

/* ================================================================== *
 * Test 14: PCA with ECF=1 (verify ECF doesn't corrupt CPS decode)    *
 * ================================================================== */
static void test_pca_ecf_isolation(void) {
    printf("\n--- test_pca_ecf_isolation ---\n");
    setup();

    /* CPS=00 (FOSC/12) with ECF=1. The concern: ECF is bit 0, CPS is bits 2:1.
     * If the mask or shift is wrong, ECF=1 looks like CPS=01 (FOSC/2). */
    cpu.mSFR[STC_REG_CCON] = CCON_CR;
    cpu.mSFR[STC_REG_CMOD] = CMOD_ECF; /* ECF=1, CPS=00 */
    cpu.mSFR[STC_REG_CL] = 0x00;
    cpu.mSFR[STC_REG_CH] = 0x00;
    stc.pca_prescaler = 0;

    /* At FOSC/12, 12 osc clocks = 1 PCA tick */
    run_clocks(12);
    uint16_t pca = cpu.mSFR[STC_REG_CL] | (cpu.mSFR[STC_REG_CH] << 8);
    CHECK(pca == 1, "PCA ECF: CPS=00+ECF=1 -> FOSC/12 (counter=1 after 12 clocks)");

    /* If ECF leaked into CPS, we'd get FOSC/2 and counter=6 */
    CHECK(pca != 6, "PCA ECF: ECF does not leak into CPS (counter != 6)");

    teardown();
}

/* ================================================================== *
 * Test 15: ADC mid-conversion restart                                 *
 * ================================================================== */
static void test_adc_restart(void) {
    printf("\n--- test_adc_restart ---\n");
    setup();

    /* Start a slow conversion (420 clocks) */
    stc12_set_adc_input(&stc, 0, 100);
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | 0; /* ch 0, speed 00 */
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);

    /* Run 200 clocks (mid-conversion) — use stc12_tick only, not run_clocks,
     * to avoid CPU execution side effects */
    for (int i = 0; i < 200; i++) stc12_tick(&cpu, &stc);
    printf("  After 200 ticks: countdown=%d, CONTR=%02X\n",
           stc.adc_countdown, cpu.mSFR[STC_REG_ADC_CONTR]);
    CHECK(!(cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG),
          "ADC restart: flag not set mid-conversion");

    /* Restart on a different channel with different input */
    stc12_set_adc_input(&stc, 5, 900);
    cpu.mSFR[STC_REG_ADC_CONTR] = ADC_POWER | ADC_START | ADC_SPEED1 | ADC_SPEED0 | 5;
    cpu.sfrwrite[STC_REG_ADC_CONTR](&cpu, STC_REG_ADC_CONTR + 0x80);

    /* Run 70 clocks (fastest speed) */
    for (int i = 0; i < 70; i++) stc12_tick(&cpu, &stc);
    CHECK((cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG) != 0,
          "ADC restart: flag set after restart completes");

    /* Result should be from channel 5 (900), not channel 0 (100) */
    uint16_t result = ((uint16_t)cpu.mSFR[STC_REG_ADC_RES] << 2) |
                      (cpu.mSFR[STC_REG_ADC_RESL] & 0x03);
    printf("  ADC result = %d, ch = %d, RES=%02X RESL=%02X\n",
           result, cpu.mSFR[STC_REG_ADC_CONTR] & 7,
           cpu.mSFR[STC_REG_ADC_RES], cpu.mSFR[STC_REG_ADC_RESL]);
    CHECK(result == 900, "ADC restart: result from restarted channel (900, not 100)");

    teardown();
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("=== Extended integration tests ===\n");

    test_button("test_images/02-button.hex");
    test_potentiometer("test_images/03-potentiometer.hex");
    test_timer0_1t_synthetic();
    test_pca_counter();
    test_pca_compare();
    test_pca_pwm();
    test_pca_t0_clock();
    test_port_open_drain();
    test_adc_edges();
    test_timer0_mode0();
    test_hex_loader();
    test_boundary_a_callbacks();
    test_advance_to();
    test_timer0_mode3();
    test_boundary_a_read_callbacks();
    test_pca_ecf_isolation();
    test_adc_restart();

    printf("\n=== Results: %d passed, %d failed ===\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
