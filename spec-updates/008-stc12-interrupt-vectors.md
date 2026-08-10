# 008 — STC12C5A60S2 interrupt vector table (settled by measurement + datasheet)

**Date:** 2026-08-10
**Status:** Confirmed. PCA vector is 0x3B, not 0x33. IE.6 is LVD only, not shared.

## Evidence

Two emulators measured the same servo driver with different PCA vectors:

| Emulator | PCA vector | 90° pulse (expected 1500 µs) |
|---|---|---|
| emu8051-stc | **0x3B** | **1498.9 µs** (0.07% error) |
| ucsim-stc | 0x33 | 1221.8 µs (18.5% error) |

The 278 µs shortfall at 0x33 is consistent with the ISR landing at the
LVD vector instead of the PCA vector, executing different code.

## Vector table (datasheet Ch. 6 "Interrupt System", p. 138)

8051 vectors are `0x03 + 8 × N`. SDCC's `__interrupt(N)` uses this.

| N | Address | Source | Enable | IP bit |
|---|---------|--------|--------|--------|
| 0 | 0x03 | INT0 | IE.0 (EX0) | IP.0 (PX0) |
| 1 | 0x0B | Timer 0 | IE.1 (ET0) | IP.1 (PT0) |
| 2 | 0x13 | INT1 | IE.2 (EX1) | IP.2 (PX1) |
| 3 | 0x1B | Timer 1 | IE.3 (ET1) | IP.3 (PT1) |
| 4 | 0x23 | UART1 | IE.4 (ES) | IP.4 (PS) |
| 5 | 0x2B | ADC | IE.5 (EADC) | IP.5 (PADC) |
| 6 | 0x33 | **LVD** | IE.6 (ELVD) | IP.6 (PLVD) |
| 7 | 0x3B | **PCA** | ECCF0/ECCF1/ECF in CCAPMn/CCON | IP.7 (PPCA) |
| 8 | 0x43 | UART2 | IE2.0 (ES2) | IP2.0 (PS2) |
| 9 | 0x4B | SPI | IE2.1 (ESPI) | IP2.1 (PSPI) |

## IE.6 is NOT shared — correction of prior claim

The earlier version of this document claimed IE.6 was shared between LVD
and PCA. **This is wrong.** The datasheet (Ch. 6, p. 138 vector table and
p. 143 IE register layout) is unambiguous:

- **IE.6 = ELVD** — enables the LVD interrupt only (p. 143)
- **PCA has no enable bit in IE.** PCA interrupt enable comes from the PCA's
  own registers: ECCFn bits in CCAPMn (per-module capture/compare interrupt
  enable) and ECF in CMOD (counter overflow interrupt enable). The PCA
  fires when any of CF, CCF0, or CCF1 in CCON is set AND the corresponding
  enable bit in CCAPMn/CMOD is set. (p. 138, enable column: "ECF+ECCF0+ECCF1")
- **LVD and PCA have separate vectors** (0x33 and 0x3B)
- **LVD and PCA have separate priority bits** (IP.6 = PLVD, IP.7 = PPCA)

bw-board's `IE |= 0x40` sets ELVD, which enables the LVD interrupt, not
the PCA interrupt. To enable PCA interrupts, set ECCFn in CCAPMn registers.

## IE register layout (Ch. 6, p. 142–143)

| bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| IE (0xA8) | EA | ELVD | EADC | ES | ET1 | EX1 | ET0 | EX0 |

## IP register layout (Ch. 6, p. 144)

| bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
|-----|---|---|---|---|---|---|---|---|
| IP (0xB8) | PPCA | PLVD | PADC | PS | PT1 | PX1 | PT0 | PX0 |
| IPH (0xB7) | PPCAH | PLVDH | PADCH | PSH | PT1H | PX1H | PT0H | PX0H |

## Source

- **Datasheet:** STC12C5A60S2-en.pdf, Chapter 6 "Interrupt System", pp. 138–145
  - Vector table: p. 138
  - IE register: p. 142–143
  - IP/IPH registers: p. 144
- Measurement: emu8051-stc servo pulse at 0x3B = 1498.9 µs (correct)
- SDCC 4.5.0 `mcs51/stc12.h`: `SBIT(ELVD, 0xA8, 6)`, `SBIT(PPCA, 0xB8, 7)`,
  `SBIT(PLVD, 0xB8, 6)` — note SDCC does not define an "EPCA" bit in IE

## Correction

`stc12.h` had `ISR_PCA = 0x33` — corrected to note in `b89118d`.
Canonical definition is `ISR_PCA = 0x3B` in `emu8051.h`.

Prior version of this spec-update incorrectly stated IE.6 was shared
between LVD and PCA. Corrected: IE.6 is ELVD only.
