# 008 — STC12C5A60S2 interrupt vector table (settled by measurement)

**Date:** 2026-08-10
**Status:** Confirmed. PCA vector is 0x3B, not 0x33.

## Evidence

Two emulators measured the same servo driver with different PCA vectors:

| Emulator | PCA vector | 90° pulse (expected 1500 µs) |
|---|---|---|
| emu8051-stc | **0x3B** | **1498.9 µs** (0.07% error) |
| ucsim-stc | 0x33 | 1221.8 µs (18.5% error) |

The 278 µs shortfall at 0x33 is consistent with the ISR landing at the
LVD vector instead of the PCA vector, executing different code.

## Vector table

8051 vectors are `0x03 + 8 × N`. SDCC's `__interrupt(N)` uses this.

| N | Address | Source | IE bit | IP bit |
|---|---------|--------|--------|--------|
| 0 | 0x03 | INT0 | IE.0 (EX0) | IP.0 (PX0) |
| 1 | 0x0B | Timer 0 | IE.1 (ET0) | IP.1 (PT0) |
| 2 | 0x13 | INT1 | IE.2 (EX1) | IP.2 (PX1) |
| 3 | 0x1B | Timer 1 | IE.3 (ET1) | IP.3 (PT1) |
| 4 | 0x23 | UART1 | IE.4 (ES) | IP.4 (PS) |
| 5 | 0x2B | ADC | IE.5 (EADC) | IP.5 (PADC) |
| 6 | 0x33 | **LVD** | IE.6 (ELVD) | IP.6 (PLVD) |
| 7 | 0x3B | **PCA** | IE.6 (shared) | IP.7 (PPCA) |

## IE.6 is shared between LVD and PCA

SDCC's `stc12.h` names IE.6 as `ELVD` and provides separate IP bits
for LVD (IP.6 = PLVD) and PCA (IP.7 = PPCA). This confirms:
- LVD and PCA share one ENABLE bit (IE.6)
- They have separate PRIORITY bits (IP.6 and IP.7)
- They have separate VECTORS (0x33 and 0x3B)

The ISR must check CCON flags (for PCA) or the LVD flag to determine
which source triggered the interrupt.

## Source

- SDCC 4.5.0 `mcs51/stc12.h`: `SBIT(ELVD, 0xA8, 6)`, `SBIT(PPCA, 0xB8, 7)`,
  `SBIT(PLVD, 0xB8, 6)`
- Measurement: emu8051-stc servo pulse at 0x3B = 1498.9 µs (correct)
- Datasheet: STC12C5A60S2 §7 Interrupt System (vector table)

## Correction

`stc12.h` had `ISR_PCA = 0x33` — corrected to note in `b89118d`.
Canonical definition is `ISR_PCA = 0x3B` in `emu8051.h`.
