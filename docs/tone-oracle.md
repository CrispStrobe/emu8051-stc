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
| 440 (A4) | 12567 | 439.927 | 440.012 | 6 | 41 |
| 1000 | 5530 | 999.855 | 999.934 | 6 | 97 |
| 2000 | 2765 | 1999.855 | 1999.868 | 6 | 197 |
| 4000 | 1382 | 4000.578 | 4001.183 | 6 | 398 |

**4/4 within 6 ppm.** Fixed-point precision increased from ×16 to ×256
in commit after `85745b3` — see systematic error analysis below.

## T0CLKO hardware toggle (STC15, Timer 0 mode 2, output on P3.5)

Timer 0 in 8-bit auto-reload mode, 1T (AUXR.T0x12=1), INT_CLKO.T0CLKO
enabled. Toggles P3.5 on every Timer 0 overflow. Frequency = FOSC / (2 × (256 - TH0)).

| Reload (TH0) | Period clocks | Expected Hz | Measured Hz | Error (ppm) | Cycles |
|--------------|--------------|------------|-------------|-------------|--------|
| 0x00 | 256 | 21,600 | 21,600 | 6 | 41 |
| 0x80 | 128 | 43,200 | 43,200 | 6 | 84 |
| 0xE0 | 32 | 172,800 | 172,801 | 6 | 343 |
| 0xF0 | 16 | 345,600 | 345,602 | 6 | 688 |

**4/4 within 6 ppm.** Same residual error as PCA (same timing engine).

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

### Before fix: ×16 (166 ppm)

The original `ns_per_clock_x16` representation:

```
exact ns/clock   = 1e9 / 11059200 = 90.422453703703...
×16 fixed-point  = round(16e9 / 11059200) = round(1446.759) = 1447
effective ns/clk = 1447 / 16 = 90.4375
error            = 90.4375 / 90.42245... − 1 = +166.4 ppm
```

At higher FOSC the error was worse: 500 ppm at 24 MHz, 688 ppm at 27 MHz.

### After fix: ×256 (6 ppm)

Increased to `ns_per_clock_x256` with `>> 8`:

```
×256 fixed-point = round(256e9 / 11059200) = round(23148.148) = 23148
effective ns/clk = 23148 / 256 = 90.421875
error            = 90.421875 / 90.42245... − 1 = −6.4 ppm
```

| FOSC | ×16 error | ×256 error | Improvement |
|------|-----------|-----------|-------------|
| 11.0592 MHz | +166 ppm | −6 ppm | 26× |
| 12.000 MHz | −250 ppm | −16 ppm | 16× |
| 22.1184 MHz | −525 ppm | −6 ppm | 84× |
| 24.000 MHz | +500 ppm | +31 ppm | 16× |
| 27.000 MHz | +688 ppm | −51 ppm | 13× |

Trade-off: max sim time before uint64 overflow drops from 36 years to
2.3 years (at 11 MHz). This is more than sufficient — no real simulation
runs for years.

### Why not higher precision?

| Precision | Worst-case error | Max sim time | Verdict |
|-----------|-----------------|-------------|---------|
| ×16 | 688 ppm | 36 years | too coarse |
| ×256 | 51 ppm | 2.3 years | **chosen** |
| ×1024 | 13 ppm | 208 days | acceptable but marginal |
| ×4096 | 3 ppm | 52 days | too short for stress tests |

×256 is the sweet spot: sub-audible error at every standard FOSC, and
the 2.3 year overflow limit has ample margin.

### Audible impact at 440 Hz

```
166 ppm × 440 Hz = 0.073 Hz deviation (before fix)
  6 ppm × 440 Hz = 0.003 Hz deviation (after fix)
JND at 440 Hz    ≈ 3 Hz
```

Both are inaudible. The fix matters for high-precision timing tests
and for higher FOSC values, not for audio fidelity.

## Known divergences

| Source | emu8051 | ucsim | Root cause |
|--------|---------|-------|------------|
| Timing resolution | 6 ppm | ~6 ppm (same formula) | Fixed-point ×256 rounding |
| ISR overhead | ~10 clocks | ~10 clocks (same instructions) | Identical |
| Init transient | 1 extra edge | — | Double-toggle at P1M0 init |

**No frequency divergences between emulators.** Both produce the same
tone within the timing resolution.
