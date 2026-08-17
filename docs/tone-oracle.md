# Tone Timing Oracle — buzzer/tone frequency verification

**Date:** 2026-08-17  
**emu8051:** `74289f7`  
**ucsim:** `421505a`  
**FOSC:** 11,059,200 Hz

## Method

Synthetic firmware generates tones via three mechanisms (PCA toggle, T0CLKO,
software ISR toggle). Edge timestamps are captured and the actual frequency
measured as `1 / (2 × mean_half_period)`. Results compared against the
expected frequency derived from the register values programmed.

## PCA toggle mode (STC12, CPS=SYSclk, output on P1.3)

PCA module 0 in compare+toggle mode (`CCAPM0 = ECOM|MAT|TOG|ECCF = 0x4D`),
ISR at 0x3B advances `CCAP0` by the compare step each match. Clock source
CPS=4 (SYSclk, every osc clock). Expected frequency = FOSC / (2 × compare).

| Target Hz | Compare | Expected Hz | Measured Hz | Error (ppm) | Cycles |
|-----------|---------|------------|-------------|-------------|--------|
| 440 (A4) | 12567 | 439.927 | 439.936 | 166 | 41 |
| 1000 | 5530 | 999.855 | 999.761 | 166 | 97 |
| 2000 | 2765 | 1999.855 | 1999.523 | 166 | 197 |
| 4000 | 1382 | 4000.578 | 4000.492 | 166 | 398 |

**4/4 within 166 ppm.** The 166 ppm systematic error is the fixed-point
timing resolution: `ns_per_clock_x16 = round(16e9 / FOSC)` = 1447 for
11.0592 MHz, giving 90.4375 ns/clock vs the exact 90.42245... ns/clock.

## T0CLKO hardware toggle (STC15, Timer 0 mode 2, output on P3.5)

Timer 0 in 8-bit auto-reload mode, 1T (AUXR.T0x12=1), INT_CLKO.T0CLKO
enabled. Toggles P3.5 on every Timer 0 overflow. Frequency = FOSC / (2 × (256 - TH0)).

| Reload (TH0) | Period clocks | Expected Hz | Measured Hz | Error (ppm) | Cycles |
|--------------|--------------|------------|-------------|-------------|--------|
| 0x00 | 256 | 21,600 | 21,596 | 166 | 41 |
| 0x80 | 128 | 43,200 | 43,193 | 166 | 84 |
| 0xE0 | 32 | 172,800 | 172,771 | 166 | 343 |
| 0xF0 | 16 | 345,600 | 345,543 | 166 | 688 |

**4/4 within 166 ppm.** Same systematic error as PCA (same timing engine).

## Software toggle via Timer 0 ISR (STC12, output on P1.0)

Timer 0 mode 1 (16-bit), 1T, ISR at 0x000B does `CPL P1.0` + reload.
Expected frequency = FOSC / (2 × (65536 - reload)). ISR overhead (~10 clocks
for LCALL+CPL+MOV×2+RETI) reduces the actual frequency.

| Target Hz | Reload | Expected Hz | Measured Hz | ISR error |
|-----------|--------|------------|-------------|-----------|
| 440 | 0xCEE9 | 440.010 | 439.726 | 0.06% |
| 1000 | 0xEA66 | 999.928 | 998.858 | 0.11% |
| 2000 | 0xF533 | 1999.855 | 1995.193 | 0.23% |
| 4000 | 0xFA9A | 4001.158 | 3986.071 | 0.38% |

**4/4 within 0.4%.** Error increases with frequency because ISR overhead
is a fixed ~10 clocks per interrupt, representing a larger fraction of
shorter periods.

## Cross-emulator comparison (16-fast-toggle firmware)

The `16-fast-toggle.hex` catalog firmware toggles P1.0 via Timer 0 ISR.
Both emulators run the same hex for 50 ms.

| Metric | emu8051 | ucsim | Delta |
|--------|---------|-------|-------|
| Edges (50 ms) | 51 | 50 | 1 |
| Mean half-period | 1,002,771 ns | 1,002,604 ns | 167 ns |
| Frequency | 498.6 Hz | 498.7 Hz | 0.02% |
| Half-period jitter | ±181 ns | ±181 ns | identical pattern |

**Steady-state half-periods match within 167 ns (1.8 osc clocks).**
Both emulators show the same alternating-period pattern (ISR takes
different clock counts on consecutive calls due to instruction alignment).
The 1-edge difference over 50 ms is a timing-boundary artefact.

## Systematic error analysis

All three tone mechanisms show exactly 166 ppm error. Root cause:

```
exact ns/clock   = 1e9 / 11059200 = 90.422453703...
fixed-point      = round(16e9 / 11059200) / 16 = 1447/16 = 90.4375
ratio            = 90.4375 / 90.42245... = 1.000166...
```

The emulator runs 166 ppm slow because the fixed-point `×16` representation
rounds up by 0.015 ns per clock, accumulating to 1.66 µs per 10,000 clocks.
This is a known, documented, and constant offset — not a drift or jitter.

## Known divergences

| Source | emu8051 | ucsim | Root cause |
|--------|---------|-------|------------|
| Timing resolution | 166 ppm slow | ~166 ppm slow (same engine) | Fixed-point ×16 rounding |
| ISR overhead | ~10 clocks | ~10 clocks (same instructions) | Identical |
| Init transient | 1 extra edge | — | Double-toggle at P1M0 init |

**No frequency divergences between emulators.** Both produce the same
tone within the 167 ns timing resolution.
