# 007 — STC15W408AS peripheral model delta

**Date:** 2026-08-09
**Status:** Proposed for `stc/docs/STC15W408AS-PERIPHERAL-MODEL.md`
**Against:** STC15-PERIPHERAL-MODEL.md (which is itself a delta against STC12)

## Summary

The STC15W408AS is a **smaller STC15** — same 1T core, same SFR address
map for the registers it has, but fewer peripherals and less memory.
It is a delta against the STC15F2K60S2, not against the STC12.

## Comparison to STC15F2K60S2

| Feature | STC15F2K60S2 | STC15W408AS |
|---|---|---|
| Flash | 60 KB | **8 KB** |
| SRAM | 2048 B (256+1792) | **512 B** (256+256) |
| ADC | 8 ch, 10-bit | 8 ch, 10-bit (same) |
| PCA/CCP/PWM | 3 modules | **none** |
| UARTs | 2 | 2 (same) |
| SPI | yes | yes (same) |
| Timers | T0, T1, T2 | T0, T1, T2 (same) |
| Watchdog | yes | yes (same) |
| Port modes | yes | yes (same) |
| Packages | PDIP-40, LQFP-44 | **SOP-16 to LQFP-44** |
| Max I/O pins | 38 | **varies by package (as few as 14)** |

## What is ABSENT relative to STC15F2K60S2

| Feature | STC15F2K60S2 registers | STC15W408AS |
|---|---|---|
| **PCA/CCP/PWM** | CCON, CMOD, CCAPM0-2, CCAPnL/H, CL, CH, PCA_PWMn | **absent** |
| **IAP/EEPROM** ⚠ | IAP_DATA..IAP_CONTR | **may differ — check datasheet** |

## What is IDENTICAL

Everything else — same 1T core, same AUXR layout (Timer 2 at
T2H/T2L for baud), same port mode registers, same ADC registers
(ADRJ at CLK_DIV.5), same SPI, same UART1/2, same watchdog, same
INT_CLKO. The SFR addresses that both parts share are identical.

## For the emulator

- Same as `PART_STC15` except:
  - `n_modules = 0` in PCA tick (no PCA processing)
  - `capabilities()` omits `pca`, `pwm`
  - XRAM size 256 (vs 1792)
  - Code memory 8K (vs 60K)
- Firmware compiled for this part should refuse `ANALOG` on pins
  that don't exist on the selected package, but the emulator does
  not model package variants — it provides all 8 ADC channels.

## ⚠ Unverified

- Exact list of absent SFRs (only PCA confirmed absent)
- IAP/EEPROM presence and address compatibility
- Whether all SPI addresses match F2K60S2 (0xCD/0xCE/0xCF)
- Package-specific pin count gating

These should be checked against the STC15W408AS datasheet section
before relying on them. The delta structure above is from the STC15
series overview; per-part confirmation is pending.
