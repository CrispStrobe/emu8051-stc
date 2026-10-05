/* test_stc89_timers.c — the STC89C52RC is a 12T classic 8052: Timer 0 and
 * Timer 1 count once per MACHINE cycle, i.e. once per 12 oscillator clocks.
 *
 * Configured the way wasm_api.c's emu_set_part(PART_STC89) configures it and
 * driven the way every embedder drives the core (tick() then stc12_tick(),
 * once per oscillator clock), Timer 0 used to advance on EVERY clock: the
 * 12-clock prescale applied only to instruction timing. A generated blink
 * with a 1 ms Timer 0 tick ran its 500 ms half-period in 59 ms on the
 * emulated STC89 (measured through the wasm build) while the same C was right
 * for silicon.
 *
 * Build: gcc -O2 -o test_stc89_timers test_stc89_timers.c core.c opcodes.c disasm.c stc12.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int fails;
#define CHECK(c, ...) do { if (c) { printf("PASS: "); } else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static void noexc(struct em8051 *a, int c) { (void)a; (void)c; }

/* Mirrors wasm_api.c emu_init(1) + emu_set_part(PART_STC89). */
static void setup_stc89(void) {
    memset(&cpu, 0, sizeof(cpu));
    cpu.mCodeMemMaxIdx = 65535; cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535; cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    cpu.except = noexc;
    /* SJMP $ at 0: the CPU spins, timers run. */
    cpu.mCodeMem[0] = 0x80; cpu.mCodeMem[1] = 0xFE;
    reset(&cpu, 1);
    stc12_init(&cpu, &stc);
    stc12_configure_part(&cpu, &stc, PART_STC89);
}

static void run_clocks(long n) {
    for (long i = 0; i < n; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }
}

int main(void) {
    setup_stc89();
    cpu.mSFR[REG_TMOD] = TMODMASK_M0_0 | TMODMASK_M0_1; /* T0 mode 1, T1 mode 1 */
    cpu.mSFR[REG_TL0] = 0; cpu.mSFR[REG_TH0] = 0;
    cpu.mSFR[REG_TL1] = 0; cpu.mSFR[REG_TH1] = 0;
    cpu.mSFR[REG_TCON] |= TCONMASK_TR0 | TCONMASK_TR1;
    run_clocks(12000);
    unsigned t0 = cpu.mSFR[REG_TL0] | (cpu.mSFR[REG_TH0] << 8);
    unsigned t1 = cpu.mSFR[REG_TL1] | (cpu.mSFR[REG_TH1] << 8);
    CHECK(t0 >= 999 && t0 <= 1001, "STC89 Timer 0: 12000 osc clocks = %u counts (expected 1000, one per machine cycle)", t0);
    CHECK(t1 >= 999 && t1 <= 1001, "STC89 Timer 1: 12000 osc clocks = %u counts (expected 1000)", t1);

    /* The generated scheduler's millisecond: reload 65536 - FOSC/12/1000 at
     * 11.0592 MHz overflows after 921.6 machine cycles = 11059 clocks. */
    setup_stc89();
    cpu.mSFR[REG_TMOD] = TMODMASK_M0_0;
    unsigned reload = 65536u - 11059200u / 12u / 1000u;
    cpu.mSFR[REG_TL0] = reload & 0xFF; cpu.mSFR[REG_TH0] = reload >> 8;
    cpu.mSFR[REG_TCON] |= TCONMASK_TR0;
    long clocks = 0;
    while (!(cpu.mSFR[REG_TCON] & TCONMASK_TF0) && clocks < 200000) { tick(&cpu); stc12_tick(&cpu, &stc); clocks++; }
    CHECK(clocks >= 11040 && clocks <= 11080, "STC89 1 ms reload overflows after %ld clocks (expected ~11059)", clocks);

    /* The 1T parts are untouched: STC12 in 1T mode still counts every clock. */
    memset(&cpu, 0, sizeof(cpu));
    cpu.mCodeMemMaxIdx = 65535; cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535; cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1); cpu.except = noexc;
    cpu.mCodeMem[0] = 0x80; cpu.mCodeMem[1] = 0xFE;
    reset(&cpu, 1); stc12_init(&cpu, &stc); stc12_configure_part(&cpu, &stc, PART_STC12);
    cpu.mSFR[REG_TMOD] = TMODMASK_M0_0; cpu.mSFR[STC_REG_AUXR] |= AUXR_T0x12;
    cpu.mSFR[REG_TL0] = 0; cpu.mSFR[REG_TH0] = 0; cpu.mSFR[REG_TCON] |= TCONMASK_TR0;
    run_clocks(1200);
    unsigned s0 = cpu.mSFR[REG_TL0] | (cpu.mSFR[REG_TH0] << 8);
    CHECK(s0 >= 1199 && s0 <= 1201, "STC12 1T Timer 0: 1200 clocks = %u counts", s0);

    printf("%s (%d failed)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
