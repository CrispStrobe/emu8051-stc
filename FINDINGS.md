# Findings — bugs and gotchas surfaced during implementation

This file records bugs found in emu8051-stc that the ucsim-stc fork (or any
other STC12 model built from the same datasheet) should check for. A bug
found in one independent implementation is only valuable if the other one
can test for it.

---

## 1. PCA Timer 0 overflow clock source: repeated overflows not counted

**What:** When the PCA counter uses Timer 0 overflow as its clock source
(CMOD CPS=10), only the *first* T0 overflow incremented the PCA counter.
Subsequent overflows were silently dropped.

**Why it was easy to make:** The obvious implementation watches TF0 (the
Timer 0 overflow flag in TCON) for a rising edge: sample TF0 before the
timer tick, check if it transitioned from 0 to 1 after. This works exactly
once — then TF0 stays set (nothing clears it unless an ISR runs or the
program writes TCON), so the edge condition is never true again.

The same bug would appear in any mode where TF0 is not cleared by an
interrupt handler — which is every program that polls TF0 manually, or
simply doesn't enable the Timer 0 interrupt.

**Fix:** Track overflow directly inside the timer increment logic. The
`stc12_timer0_tick()` function now returns `true` when the counter wrapped,
regardless of whether TF0 was already set. The PCA uses this return value
instead of watching the SFR.

**Test that catches it:**

```c
/* Timer 0 mode 2 (auto-reload), 1T, reload=0xFE → overflow every 2 ticks.
 * PCA clock = T0 overflow.
 * After 4 osc clocks → 2 T0 overflows → PCA counter should be 2. */
cpu.mSFR[STC_REG_CMOD] = CMOD_CPS1_BIT;  /* CPS=10 */
cpu.mSFR[REG_TH0] = 0xFE;
cpu.mSFR[REG_TL0] = 0xFE;
/* ... run 4 clocks ... */
assert(pca_counter == 2);  /* fails with edge-detection, passes with direct flag */
```

The bug was invisible in single-overflow tests and only appeared when Timer 0
overflowed repeatedly without TF0 being cleared between overflows.

**Where in the code:** `stc12.c`, `stc12_timer0_tick()` return value and
`stc12_tick()` usage of `t0_overflowed`. Commit `8255030`.

---

## 2. ADC conversion times: single-source, not independently confirmed

The conversion times used (420 / 280 / 140 / 70 oscillator clocks for
SPEED 00 / 01 / 10 / 11) are from the STC12C5A60S2 datasheet §10.5. No
independent source has been found to corroborate these exact numbers.
SDCC's `stc12.h` confirms the SPEED bit layout but does not specify timing.

The emulator implements these values and the tests verify the implementation
is self-consistent (flag appears at exactly the right tick count), but this
proves the code matches itself, not that it matches the chip. If a different
STC12 datasheet revision or STC example code gives different numbers, the
constants in `stc12.h` should be updated.

**Status:** single-source. Marked accordingly in the shared spec.
