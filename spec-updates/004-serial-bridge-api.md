# 004 — Serial bridge API for emulated UART targets

**Date:** 2026-08-09
**Status:** Implemented. Entry points exist in both native C and WASM.

## Problem

`10-live-firmware` is the on-chip debug monitor. Its protocol is verified
under emulation (4 independent codecs, time-freeze measured). To test
`bw-board`'s serial DebugTarget without silicon, it needs a way to push
bytes into the emulator's UART RX and read bytes from UART TX — from
JavaScript, with framing left entirely to the caller.

## Entry points

### WASM (for bw-board in the browser)

```js
// Inject a byte into the emulator's UART RX (as if received on P3.0)
const serialWrite = Module.cwrap('emu_serial_write', null, ['number']);

// Register a TX callback — fn(byte, userData) fires on each SBUF write
const setSerialCb = Module.cwrap('emu_set_serial_callback', null, ['number']);

// Usage:
const txBytes = [];
const cbPtr = Module.addFunction((byte, _ud) => { txBytes.push(byte); }, 'vii');
setSerialCb(cbPtr);

// Inject a framed command (caller builds the frame)
for (const b of frameBytes) serialWrite(b);

// After emu_run() or emu_advance_to_ns(), txBytes contains the response
```

The emulator does NOT interpret the bytes. It sets RI on inject and fires
the TX callback on SBUF write. Framing (SOF, LEN, CMD, SUM) is the
caller's responsibility — use `live-proto.h` constants.

### UART2

Same pattern, different entry points:

```js
const serial2Write = Module.cwrap('emu_serial2_write', null, ['number']);
const setSerial2Cb = Module.cwrap('emu_set_serial2_callback', null, ['number']);
```

### Native C (for testing)

```c
stc12_serial_rx(&cpu, &stc, byte);     // inject RX
stc12_set_serial_callback(&stc, cb, userdata);  // register TX callback
```

`emu_serial_bridge.c` is the reference: it reads stdin, injects into the
firmware, and writes TX to stdout. `test_monitor_py.py` and
`test_monitor_js.mjs` drive it from Python and JavaScript respectively.

### Memory access (HEAPU8)

`HEAPU8` is now exported. Pointer-returning functions like
`emu_dbg_read_mem` can be read directly:

```js
const ptr = emu_dbg_read_mem(space, addr, len);
const data = Module.HEAPU8.slice(ptr, ptr + len);
```

## What bw-board should NOT do

- Do not implement boundary D over serial — bw-board owns DebugTarget
  and there must be one implementation, not two.
- Do not interpret the frame format in the emulator — the emulator is a
  transparent byte pipe. The host builds and parses frames.
- Do not use the `on_advance` callback with `uint64_t` — it is split to
  `(uint32_t lo, uint32_t hi, void*)` to avoid WASM i64 signature mismatch.

## What is verified

| Path | Evidence |
|------|----------|
| HELLO, POS, REGS, READ, HALT, RUN | test_monitor.c (32 assertions) |
| live-monitor.py decoder + bridge | test_monitor_py.py (28 assertions) |
| stc12live.js decoder + bridge | test_monitor_js.mjs (23 assertions) |
| Time freeze (bw_ms stops, skew accumulates) | test_monitor.c §time-freeze |
| Torn-frame idle recovery | test_monitor.c + test_monitor_py.py |
