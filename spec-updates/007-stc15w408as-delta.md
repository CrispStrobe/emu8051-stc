# 007 — STC15W408AS peripheral model delta

**Updated:** 2026-11-25, bw8051_1 qualification. This replaces the 2026-08-09
proposal, whose claims of no PCA, two UARTs, and a Timer 1 were incorrect.

## Hardware identity

The manufacturer's [English product page](https://stcmicro.com/stc/stc15w408as.html)
and [Chinese product page](https://stcmicro.com/cn/stc/stc15w408as.html) specify
the following W408AS resources. The STC15 family programming details come from
the [STC15 series manual](https://stcmicro.com/datasheet/STC15F2K60S2-en.pdf).

| Resource | STC15F2K60S2 | STC15W408AS |
|---|---|---|
| Flash | 60 KB | 8 KB |
| SRAM | 2048 B: 256 internal + 1792 auxiliary | 512 B: 256 internal + 256 auxiliary |
| Main timers | T0, T1, T2 | T0, T2 |
| UARTs | 2 | 1 |
| PCA / CCP / PWM channels | 3 | 3 |
| ADC | 8 channels, 10 bits | 8 channels, 10 bits |
| SPI | Present | Present |
| DPTRs | 2 | 1 |
| EEPROM / IAP | Present | 5 KB EEPROM advertised; not emulated |

Package pin availability is a separate constraint. The emulator currently
exposes all six port banks and eight ADC channels, without package-specific
bond-out validation.

## Native part 3

Use `PART_STC15W` / `emu_set_part(3)`, not the F2K60S2 identifier `1`.
The model reports 8192 bytes of flash and 256 bytes of auxiliary RAM.
Its capability list includes `timer0`, `timer2`, `adc`, `pca`, `pwm`, `uart1`,
`spi`, and `watchdog`; it omits `timer1`, `uart2`, and `dualDptr`.
These names describe available models, not complete peripheral qualification.

The following behaviors now use the STC15-family path for both parts:

- Timer 0 mode 0 is 16-bit auto-reload. Stopped writes update both the counter
  and hidden reload latch; running writes change the next reload only.
- All three PCA modules run. P_SW1.CCP_S selects the STC15 pin groups:
  P1.1/P1.0/P3.7, P3.5/P3.6/P3.7, or P2.5/P2.6/P2.7.
- ADC justification uses `CLK_DIV.5`, with the result packing in the manual's
  ADC_RES / ADC_RESL table.
- SPI uses SPSTAT/SPCTL/SPDAT at `0xCD` / `0xCE` / `0xCF`.
- The existing Timer 2 counter uses T2H/T2L, not the STC12 baud-rate timer.

Timer 1 does not tick on W408AS. TL1/TH1 and S2CON/S2BUF are excluded by
`stc12_is_valid_sfr`; UART2 transmit and receive hooks are inactive, and writing
the F2K60S2's DPS bit does not switch the single DPTR. Raw debugger SFR storage
remains writable, as on other parts.

## Qualification boundary

This revision fixes the profile and the exercised Timer 0 / PCA / ADC / SPI
paths. It does not implement the complete W408AS. In particular, STC15 Timer 2
auto-reload and interrupt delivery, ADC interrupt delivery, SPI bit shifting,
watchdog CPU reset, and IAP/EEPROM transactions remain unavailable. UART timing
is simplified. Reserved PCA pin group 3 has no qualified mapping; firmware
must use a documented group.

See [bw8051_1 qualification](../docs/bw8051-1-qualification.md) for the exact
test boundary, checkpoint change, and reproducible commands. This is
datasheet- and software-tested behavior, not new silicon evidence.
