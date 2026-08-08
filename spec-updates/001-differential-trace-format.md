# Differential execution trace format

**Proposed addition to STC12-PERIPHERAL-MODEL.md §7.6**

## Purpose

Two independent emulators (emu8051-stc, ucsim-stc) run the same firmware
image and emit the same trace format. `diff` on the two outputs catches
any behavioural divergence — wrong timer rates, different interrupt timing,
ADC result mismatches. This is the top rung of the acceptance ladder.

## Format

One event per line. Tab-separated fields. All times in nanoseconds since
reset (decimal). Monotonically non-decreasing.

```
<t_ns>\t<event_type>\t<fields...>
```

### Event types

| type | fields | meaning |
|------|--------|---------|
| `PIN` | `port.bit mode drive` | Pin state changed. mode = `Q`/`PP`/`IN`/`OD` (quasi/pushpull/input/opendrain). drive = `H`/`L`. |
| `SFR` | `addr value` | SFR write. addr and value in hex (`8E 80`). Only emitted for SFRs on the watch list (below). |
| `PC` | `addr` | Instruction fetch. addr in hex. Emitted once per instruction, not per cycle. |
| `TF` | `n` | Timer *n* overflow (TF0 or TF1 set). |
| `ADC` | `channel result` | ADC conversion complete. channel decimal, result decimal (0-1023). |
| `PCA` | `counter` | PCA counter value at each PCA tick. Decimal. |

### SFR watch list

Only these SFRs generate `SFR` events (to keep traces diffable — writing to
every SFR would overwhelm the output with stack pointer changes):

`AUXR` (8E), `TMOD` (89), `TCON` (88), `P0` (80), `P1` (90), `P2` (A0),
`P3` (B0), `P4` (C0), `P0M0` (94), `P0M1` (93), `P1M0` (92), `P1M1` (91),
`P2M0` (96), `P2M1` (95), `P3M0` (B2), `P3M1` (B1), `ADC_CONTR` (BC),
`CCON` (D8), `CMOD` (D9), `CCAPM0` (DA), `CCAPM1` (DB).

### Example

```
0       PC      0000
90      SFR     91 FC
90      SFR     92 03
90      PIN     1.0 PP H
90      PIN     1.1 PP H
180     SFR     8E 00
270     SFR     89 01
5500    PC      0010
11052   TF      0
```

### Clock basis

Both emulators must compute nanoseconds from the same formula:

```
t_ns = osc_clocks * 1_000_000_000 / fosc
```

Integer division, truncating. `fosc` is a parameter of the run, not
embedded in the trace. Both runs must use the same `fosc`.

### Producing a trace

```
emu8051-stc:  ./emu_trace -stc12 -fosc 11059200 firmware.hex > trace_emu.tsv
ucsim-stc:    s51_trace -t stc12c5a60s2 -X 11059200 firmware.hex > trace_ucsim.tsv
diff trace_emu.tsv trace_ucsim.tsv
```

The command-line interface doesn't exist yet on either side. This spec
defines the format so both can implement it independently.

### What a diff means

- **PC divergence** at the same time: an opcode is decoded differently
  (wrong instruction semantics or different memory contents).
- **PIN divergence**: port mode or latch logic disagrees.
- **TF divergence at different times**: timer prescaler or 1T/12T logic
  disagrees — the critical test.
- **ADC result divergence**: conversion time or count mapping differs.
- **Same events at different times**: clock accounting bug.

### Limitations

This format does not capture interrupt delivery timing, stack operations,
or accumulator state. It is intentionally minimal — enough to catch the
peripheral model bugs that matter (timers, ports, ADC) without making
traces too large to diff by eye.
