/* PCON.IDL fast-forward — a DIFFERENTIAL oracle.
 *
 * The claim this file has to support is not "idling got faster". It is
 * "idling got faster and NOTHING ELSE CHANGED". So every case runs the same
 * firmware twice — once with stc12_set_idle_fastforward(true), once with it
 * off — and requires the two runs to agree on the full observable state:
 * sim time, PC, every SFR, the interrupt count, and the timer registers.
 * The only permitted difference is the skipped-clocks counter.
 *
 * There are no wall-clock budgets anywhere in here. bw-board's
 * avr-sleep-fastforward test records why: three machines have shown timing
 * budgets fire falsely under load, and a gate that goes red for the wrong
 * reason gets ignored for the right ones.
 */
#include <stdio.h>
#include <string.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static uint8_t code[65536], ext[65536], upper[128];
static int failures = 0;

#define CHECK(cond, ...) do { if (!(cond)) { \
    printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

/* The ISR counts ITSELF, into internal RAM at 0x30. Sampling the PC between
 * hops does not work: the ISR is one instruction long and is almost never
 * the instruction in flight when a hop ends, so a PC-watcher reports zero
 * and would call two different runs equal because it saw nothing in either. */
#define ISR_COUNTER 0x30

static void build(int idle_in_loop, int arm_t0)
{
    memset(&cpu, 0, sizeof cpu);
    memset(&stc, 0, sizeof stc);
    memset(code, 0, sizeof code);
    cpu.mCodeMem = code; cpu.mCodeMemMaxIdx = 0xffff;
    cpu.mExtData = ext;  cpu.mExtDataMaxIdx = 0xffff;
    cpu.mUpperData = upper;
    stc12_init(&cpu, &stc);
    reset(&cpu, 1);
    /* Every real embedder does this — emu.c: "STC12 handles timers itself".
     * Without it core.c's timer_tick double-counts T0 against
     * stc12_timer0_tick, and the first run of this oracle measured exactly
     * that: 34 ISR entries where the arithmetic says 17. */
    cpu.skip_timers = true;

    code[0x0000] = 0x80; code[0x0001] = 0x2E;   /* SJMP 0x30 */
    code[0x000B] = 0x05; code[0x000C] = ISR_COUNTER;  /* INC 30h */
    code[0x000D] = 0x32;                             /* RETI */
    if (idle_in_loop) {
        code[0x0030] = 0x43; code[0x0031] = 0x87; code[0x0032] = 0x01; /* ORL PCON,#01 */
        code[0x0033] = 0x80; code[0x0034] = 0xFB;                      /* SJMP 0x30 */
    } else {
        code[0x0030] = 0x80; code[0x0031] = 0xFE;                      /* SJMP $ */
    }
    if (arm_t0) {
        cpu.mSFR[STC_REG_AUXR] |= AUXR_T0x12;                 /* 1T */
        cpu.mSFR[REG_TMOD] = (cpu.mSFR[REG_TMOD] & 0xF0) | TMODMASK_M0_0;
        uint16_t reload = (uint16_t)(65536 - 11059);          /* 1 ms @ 11.0592 MHz */
        cpu.mSFR[REG_TL0] = reload & 0xff;
        cpu.mSFR[REG_TH0] = reload >> 8;
        cpu.mSFR[REG_TCON] |= TCONMASK_TR0;
        cpu.mSFR[REG_IE] |= IEMASK_EA | IEMASK_ET0;
    }
}

/* Advance in hops, the way an embedder slices time. The hops matter: a skip
 * that is correct within one slice but wrong across a slice boundary is
 * exactly the bug the avr8js fast-forward shipped and had to fix. */
static void run_ns(uint64_t ns)
{
    uint64_t end = stc12_get_time_ns(&stc) + ns;
    while (stc12_get_time_ns(&stc) < end) {
        uint64_t next = stc12_get_time_ns(&stc) + 50000; /* 50 us hops */
        if (next > end) next = end;
        stc12_advance_to(&cpu, &stc, next);
    }
}

struct snapshot {
    uint64_t time_ns; uint16_t pc; int isr; uint8_t sfr[128]; uint8_t iram[128];
};

static struct snapshot capture(void)
{
    struct snapshot s;
    s.time_ns = stc12_get_time_ns(&stc);
    s.pc = cpu.mPC;
    s.isr = cpu.mLowerData[ISR_COUNTER];
    memcpy(s.sfr, cpu.mSFR, 128);
    memcpy(s.iram, cpu.mLowerData, 128);
    return s;
}

/* Run one scenario both ways and compare. */
static void differential(const char *name, int idle_in_loop, int arm_t0,
                         uint64_t ns, int expect_skip)
{
    printf("%s\n", name);

    stc12_set_idle_fastforward(false);
    build(idle_in_loop, arm_t0);
    run_ns(ns);
    struct snapshot slow = capture();
    uint64_t slow_skipped = stc12_get_idle_skipped_clocks(&stc);

    stc12_set_idle_fastforward(true);
    build(idle_in_loop, arm_t0);
    run_ns(ns);
    struct snapshot fast = capture();
    uint64_t fast_skipped = stc12_get_idle_skipped_clocks(&stc);

    CHECK(slow_skipped == 0, "the reference run skipped %llu clocks — the off switch does not work",
          (unsigned long long)slow_skipped);
    if (expect_skip)
        CHECK(fast_skipped > 0, "expected the fast-forward to fire and it did not");
    else
        CHECK(fast_skipped == 0, "the fast-forward fired (%llu clocks) where it must stay inert",
              (unsigned long long)fast_skipped);

    CHECK(slow.time_ns == fast.time_ns, "sim time differs: %llu vs %llu",
          (unsigned long long)slow.time_ns, (unsigned long long)fast.time_ns);
    CHECK(slow.pc == fast.pc, "PC differs: %04x vs %04x", slow.pc, fast.pc);
    CHECK(slow.isr == fast.isr, "ISR entries differ: %d vs %d", slow.isr, fast.isr);
    for (int i = 0; i < 128; i++) {
        /* SBUF is the one exemption, and it is not an exemption from this
         * change: core.c's reset() seeds it with rand(), modelling an unknown
         * power-on value. It differs between two runs of IDENTICAL firmware
         * with zero clocks skipped, which is how this was caught rather than
         * assumed — the inert cases below flagged it too. */
        if (0x80 + i == 0x99) continue;   /* SBUF */
        CHECK(slow.sfr[i] == fast.sfr[i], "SFR 0x%02x differs: %02x vs %02x",
              0x80 + i, slow.sfr[i], fast.sfr[i]);
    }
    for (int i = 0; i < 128; i++)
        CHECK(slow.iram[i] == fast.iram[i], "IRAM 0x%02x differs: %02x vs %02x",
              i, slow.iram[i], fast.iram[i]);
    /* Differential agreement is not enough on its own: two runs can agree on
     * a wrong number. So the tick count is also checked against arithmetic.
     * T0 mode 1 is 16-bit with no auto-reload, so it overflows every 65536
     * counter ticks — 5.9257 ms at 11.0592 MHz in 1T. */
    if (arm_t0 && idle_in_loop) {
        /* 11.0592 MHz = 0.0110592 clocks per ns. 65536 clocks per overflow. */
        double period_ns = 65536.0 / 0.0110592;           /* 5,925,925 ns */
        int expect = (int)((double)fast.time_ns / period_ns);
        CHECK(fast.isr >= expect - 1 && fast.isr <= expect + 1,
              "ISR fired %d times in %llu ns; arithmetic says about %d",
              fast.isr, (unsigned long long)fast.time_ns, expect);
    }
    printf("   time %llu ns, %d ISR entries, %llu clocks slept (%.2f%%)\n",
           (unsigned long long)fast.time_ns, fast.isr,
           (unsigned long long)fast_skipped,
           100.0 * (double)fast_skipped / ((double)fast.time_ns * 11.0592 / 1000.0));
}

int main(void)
{
    /* The case the generated TASKS scheduler produces. */
    differential("sleep / wake on the T0 tick / sleep again, 100 ms",
                 1, 1, 100000000ULL, 1);

    /* Today's firmware: no idle at all. The fast-forward must never fire. */
    differential("busy-spin with no PCON.IDL — must stay inert",
                 0, 1, 10000000ULL, 0);

    /* Idle with NO wake source armed. Skipping here could park for ever,
     * so the guard must refuse. */
    differential("idle with T0 disarmed — must refuse to skip",
                 1, 0, 10000000ULL, 0);

    /* Every guard, checked one at a time: set the condition that must
     * disable the skip, and require zero skipped clocks. */
    struct { const char *why; uint8_t sfr; uint8_t bit; } guards[] = {
        { "Timer 1 running",        REG_TCON,            TCONMASK_TR1 },
        { "baud-rate timer on",     STC_REG_AUXR,        AUXR_BRTR    },
        { "PCA running",            STC_REG_CCON,        CCON_CR      },
        { "watchdog enabled",       STC_REG_WDT_CONTR,   0x20         },
        { "ADC started",            STC_REG_ADC_CONTR,   ADC_START    },
        { "T0 in counter mode",     REG_TMOD,            TMODMASK_CT_0 },
        { "T0 gated by INT0",       REG_TMOD,            TMODMASK_GATE_0 },
    };
    printf("guards — each must disable the skip on its own\n");
    for (unsigned g = 0; g < sizeof guards / sizeof guards[0]; g++) {
        stc12_set_idle_fastforward(true);
        build(1, 1);
        cpu.mSFR[guards[g].sfr] |= guards[g].bit;
        run_ns(5000000ULL);
        uint64_t skipped = stc12_get_idle_skipped_clocks(&stc);
        CHECK(skipped == 0, "%s: skipped %llu clocks, must be 0",
              guards[g].why, (unsigned long long)skipped);
        if (skipped == 0) printf("   ok: %s\n", guards[g].why);
    }

    /* EA and ET0 are the wake path itself. */
    const char *ie_names[2] = { "EA cleared", "ET0 cleared" };
    uint8_t ie_bits[2] = { IEMASK_EA, IEMASK_ET0 };
    for (int i = 0; i < 2; i++) {
        stc12_set_idle_fastforward(true);
        build(1, 1);
        cpu.mSFR[REG_IE] &= (uint8_t)~ie_bits[i];
        run_ns(5000000ULL);
        uint64_t skipped = stc12_get_idle_skipped_clocks(&stc);
        CHECK(skipped == 0, "%s: skipped %llu clocks, must be 0",
              ie_names[i], (unsigned long long)skipped);
        if (skipped == 0) printf("   ok: %s\n", ie_names[i]);
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
