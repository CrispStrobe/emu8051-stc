# ADC Analog Path Oracle — emu8051 vs ucsim cross-check

**Date:** 2026-08-17  
**emu8051:** `f701459`  
**ucsim:** `421505a`  
**FOSC:** 11,059,200 Hz  
**VCC:** 5.0 V (default)

## Method

Each emulator is given the same firmware and the same injected ADC count
value via the `-adc CH,VAL` flag. The ADC completion event is captured
from the trace output. The test verifies that both emulators produce
identical 10-bit ADC result values (counts) for the same input.

## 02-adc firmware (STC12C5A60S2, channel 3, ADRJ=0)

Firmware reads ADC channel 3, polls ADC_FLAG, reads ADC_RES:ADC_RESL.
Register sequence: `ADC_CONTR = ADC_POWER | ADC_START | ch`.

| Counts | Voltage | emu8051 | ucsim | Match | RES:RESL |
|--------|---------|---------|-------|-------|----------|
| 0 | 0.000V | 0 | 0 | OK | 0x00:0x00 |
| 102 | 0.499V | 102 | 102 | OK | 0x19:0x02 |
| 205 | 1.002V | 205 | 205 | OK | 0x33:0x01 |
| 256 | 1.251V | 256 | 256 | OK | 0x40:0x00 |
| 512 | 2.502V | 512 | 512 | OK | 0x80:0x00 |
| 767 | 3.749V | 767 | 767 | OK | 0xBF:0x03 |
| 768 | 3.754V | 768 | 768 | OK | 0xC0:0x00 |
| 1000 | 4.888V | 1000 | 1000 | OK | 0xFA:0x00 |
| 1023 | 5.000V | 1023 | 1023 | OK | 0xFF:0x03 |

**9/9 exact match.**

## 76-multimeter firmware (STC15F2K60S2, channel 0, ADRJ=0)

Firmware reads ADC channel 0 (voltage divider input) in a timer-ISR-driven
loop. Same register sequence as above.

| Counts | Voltage | emu8051 | ucsim | Match | RES:RESL |
|--------|---------|---------|-------|-------|----------|
| 0 | 0.000V | 0 | 0 | OK | 0x00:0x00 |
| 102 | 0.499V | 102 | 102 | OK | 0x19:0x02 |
| 205 | 1.002V | 205 | 205 | OK | 0x33:0x01 |
| 256 | 1.251V | 256 | 256 | OK | 0x40:0x00 |
| 512 | 2.502V | 512 | 512 | OK | 0x80:0x00 |
| 767 | 3.749V | 767 | 767 | OK | 0xBF:0x03 |
| 768 | 3.754V | 768 | 768 | OK | 0xC0:0x00 |
| 1000 | 4.888V | 1000 | 1000 | OK | 0xFA:0x00 |
| 1023 | 5.000V | 1023 | 1023 | OK | 0xFF:0x03 |

**9/9 exact match.**

## Conversion timing

| Firmware | emu8051 (ns) | ucsim (ns) | Delta |
|----------|-------------|-----------|-------|
| 02-adc (STC12) | 2,117,774 | 2,117,422 | +352 ns (4 clocks) |
| 76-multimeter (STC15) | 105,359 | 104,437 | +922 ns (10 clocks) |

Timing deltas are within tick-granularity tolerance (ucsim decrements
the conversion counter by multi-cycle steps; emu8051 decrements by 1
per osc clock). **ADC count values are identical; only the completion
timestamp differs.**

## Voltage→count formula verification (emu8051 analog callback)

emu8051's boundary-A analog callback converts volts to counts with
`round(volts / vcc * 1023)`. Verified at 10 points across two VCC levels:

| Voltage | VCC | Expected | Actual | Match |
|---------|-----|----------|--------|-------|
| 0.000 | 5.0 | 0 | 0 | OK |
| 0.500 | 5.0 | 102 | 102 | OK |
| 1.000 | 5.0 | 205 | 205 | OK |
| 2.500 | 5.0 | 512 | 512 | OK |
| 3.750 | 5.0 | 767 | 767 | OK |
| 5.000 | 5.0 | 1023 | 1023 | OK |
| 0.000 | 3.3 | 0 | 0 | OK |
| 1.650 | 3.3 | 512 | 512 | OK |
| 3.300 | 3.3 | 1023 | 1023 | OK |
| 4.000 | 3.3 | 1023 | 1023 | OK (clamped) |

**10/10 match.**

## ADRJ register layout verification

| ADRJ | ADC_RES | ADC_RESL | Source |
|------|---------|----------|--------|
| 0 (right-justified) | `result >> 2` (high 8) | `result & 0x03` (low 2) | STC12 datasheet Ch. 10 |
| 1 (left-justified) | `(result >> 8) & 0x03` (high 2) | `result & 0xFF` (low 8) | STC12 datasheet Ch. 10 |

Verified at 7 count values (0, 128, 256, 512, 768, 1000, 1023) for
both ADRJ settings. **14/14 match.**

ADRJ location:
- STC12: AUXR1 (0xA2) bit 2
- STC15: CLK_DIV (0x97) bit 5

Both verified in `test_adc_oracle.c`.

## Known behavioral differences

| Aspect | emu8051 | ucsim | Impact |
|--------|---------|-------|--------|
| P1ASF check | Ignores P1ASF (always uses input) | Returns 0 if channel not in P1ASF | None for normal firmware (firmware sets P1ASF before reading) |
| Tick granularity | 1 osc clock per stc12_tick | Multi-cycle per tick | 4–10 clock timing delta on completion |
| Analog callback | Volts→counts via `round(v/vcc*1023)` | Raw counts only | emu8051 also supports raw count injection |

**No value divergences. All count-level results are identical.**

## Test coverage

- `test_adc_oracle.c`: 38 in-process assertions (raw counts, ADRJ, voltage
  conversion, STC15 ADRJ, FLAG/START, all 8 channels)
- Cross-emulator: 18 trace-level comparisons (9 per firmware × 2 firmware)
