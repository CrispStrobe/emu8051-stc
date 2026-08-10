# UART entry points — how to drive the serial port from JavaScript

For **bw-board** (serial DebugTarget) and any other consumer that needs
to send and receive bytes through the emulated UART.

## 1. How a byte leaves the MCU (TX)

The firmware writes to `SBUF` (SFR 0x99). The emulator intercepts this
via an SFR write callback (`sfr_write_sbuf` in `stc12.c:1103`).

**What happens on a SBUF write:**
1. The byte value is read from `mSFR[REG_SBUF]`.
2. The TX callback fires immediately (no baud-rate delay — the transfer
   is modelled as instant).
3. `TI` (SCON bit 1, address 0x99 bit 1) is set, signalling "transmit
   complete" to the firmware.
4. The byte is also appended to `cpu.serial_out[]` (an 18-byte ring
   buffer, legacy — use the callback instead).

**The baud rate is not modelled.** The emulator does not simulate
bit-level timing. A SBUF write produces the byte immediately regardless
of what BRT/Timer 1/Timer 2 is configured for. This is correct for
protocol-level testing (the bytes arrive in the right order) but means
the emulator cannot detect baud mismatch.

## 2. How a byte enters the MCU (RX)

The host calls `stc12_serial_rx(&cpu, &stc, byte)` (C) or
`emu_serial_write(byte)` (WASM).

**What happens on inject:**
1. The byte is written to `mSFR[REG_SBUF]` (SBUF, 0x99).
2. `RI` (SCON bit 0) is set, signalling "receive complete" to the
   firmware.
3. If the firmware has UART interrupts enabled (`ES` in IE), the ISR
   will fire on the next tick.

**Timing:** The byte is available immediately. The firmware's polling
loop (`while (!RI)`) or ISR will pick it up on the next instruction
boundary. There is no inter-byte timing — inject bytes as fast as the
protocol requires. For the monitor protocol, inject one frame's bytes
in sequence with a few `run_clocks(1000)` between each.

## 3. JavaScript entry points (WASM API)

### TX: register a callback to receive transmitted bytes

```js
const setSerialCb = Module.cwrap('emu_set_serial_callback', null, ['number']);

const txBytes = [];
const cbPtr = Module.addFunction((byte, _ud) => {
    txBytes.push(byte);
}, 'vii');
setSerialCb(cbPtr);
```

The callback fires synchronously during `emu_run()` / `emu_tick()` /
`emu_advance_to_ns()` whenever the firmware writes SBUF. Signature:
`void fn(uint8_t byte, void *user_data)`.

### RX: inject a byte into the firmware's receive buffer

```js
const serialWrite = Module.cwrap('emu_serial_write', null, ['number']);
serialWrite(0x7E);  // inject SOF byte
```

This sets SBUF and RI immediately. Call `emu_run()` or `emu_tick()`
afterward to let the firmware process it.

### Legacy TX ring buffer (not recommended)

```js
const readBuf = Module.cwrap('emu_serial_read_buf', 'number', []);
const readIdx = Module.cwrap('emu_serial_read_idx', 'number', []);
// Returns a pointer to an 18-byte ring buffer; read via HEAPU8.
```

Use the callback instead — the ring buffer is 18 bytes and wraps.

## 4. UART2 (S2CON/S2BUF)

Same interface, different entry points:

```js
const setSerial2Cb = Module.cwrap('emu_set_serial2_callback', null, ['number']);
const serial2Write = Module.cwrap('emu_serial2_write', null, ['number']);
```

UART2 uses S2CON (0x9A) and S2BUF (0x9B). S2TI and S2RI are bits 1
and 0 of S2CON.

## 5. Baud rate model

| Part | Baud source | Modelled? |
|---|---|---|
| STC12 | BRT (0x9C) via AUXR.S1BRS | Counter runs, does NOT gate TX/RX timing |
| STC15 | Timer 2 (T2H/T2L) via AUXR.S1ST2 | Counter runs, does NOT gate TX/RX timing |
| STC89 | Timer 1 mode 2 (auto-reload) | Upstream timer runs, does NOT gate TX/RX timing |

**What this means for a host:** The emulator cannot tell you the baud
rate the firmware configured. If you need it (e.g. to set a real serial
port), read the BRT reload or Timer 2 reload from the SFR space and
compute it yourself:

```
baud = FOSC / (32 * (256 - BRT))     // STC12 with AUXR.BRTx12=1
baud = FOSC / (32 * (65536 - T2HL))  // STC15 with AUXR.T2x12=1
```

**What happens if the guest misconfigures baud:** Nothing. The emulator
does not enforce timing. Bytes are delivered instantly regardless of
baud. A baud mismatch would only appear on real silicon.

## 6. The monitor protocol over UART

`10-live-firmware` uses UART1 at 115200 baud (BRT on STC12, Timer 2 on
STC15). The host side sends framed commands (`SOF LEN CMD payload SUM`)
and receives framed replies. See `spec-updates/004-serial-bridge-api.md`
for the boundary A bridge, and `test_monitor.c` / `test_monitor_py.py`
for working examples.

The emulator is a transparent byte pipe. It does not interpret frames.
The host builds and parses frames; the emulator delivers bytes.

## 7. SFR story — what is honoured, what is accepted-and-ignored

Cited against `stc/docs/STC12-PERIPHERAL-MODEL.md` §8 (out of scope) and
the datasheet (Ch. 8 UART, Ch. 9 BRT).

| SFR | Address | Honoured | Notes |
|-----|---------|----------|-------|
| SBUF | 0x99 | **Yes** — TX write triggers callback + TI; RX inject writes here + sets RI | Shared address for TX/RX per 8051 convention |
| SCON | 0x98 | **Partially** — SM0/SM1 stored but mode is always "8-bit UART". REN stored but not enforced (RX inject works regardless). TI/RI set by model, cleared by firmware. TB8/RB8 stored, not used. | Mode 1 (SM0=0, SM1=1) is the only mode that matters for generated code |
| PCON | 0x87 | **SMOD bit (bit 7) stored, not used.** Baud doubling has no effect because baud timing is not modelled. IDL/PD bits handled by core (idle/power-down). | SMOD would double baud rate on real silicon |
| BRT | 0x9C | **Counter runs, does NOT gate timing.** The reload register is writable and readable; the counter increments on schedule. But TX/RX are instant regardless. | Read BRT to compute baud: `FOSC / (32 * (256 - BRT))` with BRTx12 |
| AUXR | 0x8E | **S1BRS (bit 0) stored.** Selects BRT vs Timer 1 as baud source — honoured for the counter but irrelevant since timing is not gated. BRTx12 (bit 2) stored. | |
| S2CON | 0x9A | **Same model as SCON** for UART2. S2TI/S2RI set/cleared. | |
| S2BUF | 0x9B | **Yes** — same instant model as SBUF. | |

**Divergence from the peripheral model:** `STC12-PERIPHERAL-MODEL.md` §8
lists UART as "out of scope — add when something needs it." This
implementation exists because the monitor protocol and serial DebugTarget
need it. The timing gap (instant vs baud-gated) is the divergence:
the peripheral model would require bit-level timing for full conformance.

## 8. Buffer ownership and concurrency

**TX callback:** The `byte` argument is a value, not a reference. The
callback owns the byte from the moment it receives it. The callback fires
synchronously during `emu_run()`/`emu_tick()`/`emu_advance_to_ns()`, so
it must not re-enter the emulator.

**RX inject:** `emu_serial_write(byte)` writes to SBUF and sets RI
immediately. If RI is already set (firmware has not read the previous
byte), the new byte **overwrites** the previous one — there is no
hardware FIFO. The host must wait for RI to clear before injecting the
next byte, or accept that bytes will be lost. For the monitor protocol,
inject one byte, run enough ticks for the ISR to clear RI, then inject
the next.

**Ring buffer (`serial_out[]`):** 18 bytes, wraps. Legacy; use the TX
callback. The emulator owns this buffer and may overwrite it at any time.

## 9. What is NOT modelled — and the trap this creates

- **Baud-rate timing.** TX and RX are instant.
- **Framing errors.** No start/stop bit checking.
- **Parity.** Not checked.
- **Multi-processor communication.** SM2/TB8/RB8 bits are stored but
  not used for address filtering.
- **UART mode 0 (synchronous).** Not implemented.
- **P3.0/P3.1 ISP contention.** P3.0 (RxD) and P3.1 (TxD) are also the
  ISP programming pins. On real silicon, serial traffic during ISP
  entry can trigger unintended resets. The emulator does not model this.
- **Real UART timing** has never been tested on silicon.

**The trap, stated plainly:** A serial DebugTarget that passes all its
tests against this emulator has demonstrated that the protocol logic is
correct — bytes arrive in the right order, frames parse, commands
execute. It has NOT demonstrated that the wire works. A passing test
against an untimed UART model says nothing about baud mismatch, framing
errors, or timing-sensitive recovery on real hardware. Say so when
reporting results: this is evidence category 2b (model agreement), not
category 1 (independent measurement).

**Idle-timeout resync:** The monitor protocol's idle timeout
(`LIVE_IDLE_MS = 5 ms`) reads Timer 1 wall time, NOT the UART — so it
is reachable in emulation in principle (ucsim-stc 2193511). The blocker
is not a modelling limit but **RX input plumbing**: `emu_serial_write`
injects a byte immediately with no way to schedule delivery at a future
nanosecond. To exercise the resync path, the trace harness needs a
`-inject TIME_NS,BYTE` flag that schedules a byte delivery at a
specific wall-clock time. Once that exists: send a truncated frame, let
5 ms pass, then send a well-formed frame and verify the monitor accepts
it. That is the recovery path a real wire tests hardest and that nothing
has ever exercised.
