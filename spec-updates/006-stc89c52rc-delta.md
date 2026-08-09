# 006 — STC89C52RC peripheral model delta

**Date:** 2026-08-09
**Status:** Proposed for `stc/docs/STC89-PERIPHERAL-MODEL.md`
**Against:** STC12-PERIPHERAL-MODEL.md (the parent)

## Summary

The STC89C52RC is a classic Intel 8052 clone with STC's ISP bootloader.
It is **12T** (12 clocks per machine cycle), has **no STC-specific
peripherals**, and is the part the drop-in-socket story in README §8.1
is about.

## What is identical to the 8052

Everything. The STC89C52RC is a standard 8052 with 8K flash, 512 bytes
IRAM (256 scratch-pad + 256 upper), and 64K XRAM. The ISP bootloader is
the only STC-specific addition and it does not affect the register model.

## What is ABSENT relative to the STC12

This is a subtractive delta. The STC89 does NOT have:

| Feature | STC12 register | STC89 |
|---|---|---|
| **AUXR (1T/12T select)** | 0x8E | absent — always 12T |
| **Port modes (PxM0/PxM1)** | 0x91-0x96, 0xB1-0xB4, 0xC9-0xCA | absent — all pins quasi-bidirectional |
| **ADC** | P1ASF, ADC_CONTR, ADC_RES, ADC_RESL | absent |
| **PCA/PWM** | CCON, CMOD, CCAPMn, CCAPnL/H, CL, CH | absent |
| **BRT** | 0x9C, AUXR bits | absent — baud from Timer 1 or Timer 2 |
| **Watchdog** | WDT_CONTR 0xC1 | absent (some STC89 variants have one at a different address ⚠) |
| **SPI** | SPCTL, SPDAT, SPSTAT | absent |
| **UART2** | S2CON, S2BUF | absent |
| **Dual DPTR** | AUXR1.DPS | absent |
| **P4, P5** | 0xC0, 0xC8 | absent (these addresses are T2CON/T2MOD on 8052) |
| **CLK_DIV** | 0x97 | absent |

## What the STC89 HAS that the STC12 does NOT

**8052 Timer 2** at the standard addresses:

| Register | Address | Function |
|---|---|---|
| T2CON | 0xC8 | Timer 2 control (bit-addressable) |
| T2MOD | 0xC9 | Timer 2 mode |
| RCAP2L | 0xCA | Timer 2 capture/reload low |
| RCAP2H | 0xCB | Timer 2 capture/reload high |
| TL2 | 0xCC | Timer 2 counter low |
| TH2 | 0xCD | Timer 2 counter high |

⚠ The STC12 remaps 0xC8 to P5 and 0xC9 to P5M1 — these are different
registers at the same addresses on the two parts.

## Timing

**12T.** Every instruction takes 12× as many oscillator clocks as on the
STC12's 1T core. Timer 0 at FOSC/12 counts identically on both — this is
the design constraint that makes one binary correct on both parts.

**Software delay loops run at the intended speed.** On the STC12 they run
12× too fast (README §8.1). This is the drop-in-socket trap.

## For the emulator

- `cpu.skip_timers = false` — upstream `core.c` handles all timers in 12T
- Do NOT install STC-specific SFR callbacks (port modes, ADC, PCA, etc.)
- `capabilities()` reports only `timer0, timer1, uart1`
- `stc12_tick()` returns immediately (no 1T prescaling needed)
- Port reads: all pins quasi-bidirectional, no mode registers
