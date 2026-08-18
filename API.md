# WASM API Reference — emu8051-stc

All functions are exported via Emscripten and callable from JavaScript
using `Module.cwrap()` or `Module.ccall()`.

## Lifecycle

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_init(stc12)` | `(i32) → void` | Initialize. `1` = STC12 mode. |
| `emu_reset(wipe)` | `(i32) → void` | Reset CPU. `1` = wipe memory. |
| `emu_tick()` | `() → i32` | One osc clock. Returns 1 if instruction completed. |
| `emu_run(cycles)` | `(i32) → i32` | Run N osc clocks. Returns instructions executed. |
| `emu_advance_to_ns(lo,hi)` | `(i32,i32) → i32` | Run until nanosecond target. Returns instructions. |
| `emu_set_fosc(hz)` | `(i32) → void` | Set oscillator frequency. |
| `emu_set_part(id)` | `(i32) → void` | Set part: 0=STC12, 1=STC15. Call after init. |
| `emu_set_vcc(v)` | `(f64) → void` | Set supply voltage (default 5.0). |
| `emu_capabilities()` | `() → string` | JSON capabilities per DEBUG-CONTROL-MODEL.md §7. |
| `emu_version()` | `() → string` | Returns `"emu8051-stc 1.0.0"`. |

## Memory

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_load_hex(str,len)` | `(string,i32) → i32` | Load Intel HEX. Returns 0 on success. |
| `emu_get_sfr(addr)` | `(i32) → i32` | Read SFR (0x80-0xFF). |
| `emu_set_sfr(addr,val)` | `(i32,i32) → void` | Write SFR. |
| `emu_get_iram(addr)` | `(i32) → i32` | Read internal RAM (0x00-0xFF). |
| `emu_set_iram(addr,val)` | `(i32,i32) → void` | Write internal RAM. |
| `emu_get_code(addr)` | `(i32) → i32` | Read code memory. |
| `emu_get_xdata(addr)` | `(i32) → i32` | Read external data memory. |
| `emu_set_xdata(addr,val)` | `(i32,i32) → void` | Write external data memory. |
| `emu_get_pc()` | `() → i32` | Read program counter. |
| `emu_set_pc(pc)` | `(i32) → void` | Set program counter. |
| `emu_disasm(addr)` | `(i32) → string` | Disassemble instruction at address. |

## Boundary A — Pin Bus

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_get_pin_mode(port,bit)` | `(i32,i32) → i32` | 0=quasi, 1=pushpull, 2=input, 3=opendrain |
| `emu_get_pin_drive(port,bit)` | `(i32,i32) → i32` | Latch value (0 or 1). |
| `emu_set_pin_input(port,bit,level)` | `(i32,i32,i32) → void` | Set external pin input. |
| `emu_set_adc_input(ch,counts)` | `(i32,i32) → void` | ADC input in counts (0-1023). Legacy. |
| `emu_set_adc_voltage(ch,volts)` | `(i32,f64) → void` | ADC input in volts (0-VCC). |
| `emu_set_port_input(port,val)` | `(i32,i32) → void` | Set all 8 pins of a port. Legacy. |
| `emu_set_board_callbacks(pin,read,analog,advance,ud)` | `(ptr×5) → void` | Register push-mode callbacks via `addFunction`. `advance` signature: `(lo32, hi32, ud)` — split i64 for WASM compatibility. |
| `emu_get_time_ns_lo()` | `() → i32` | Nanoseconds since reset (low 32 bits). **See timing contract below.** |
| `emu_get_time_ns_hi()` | `() → i32` | Nanoseconds since reset (high 32 bits). **See timing contract below.** |

### Timing contract (u32 split representation)

The emulator's internal time is a `uint64_t` nanosecond counter, monotonically
increasing and overflow-free for ~36 years of simulated time. The WASM API
splits it into two `uint32_t` words because Emscripten cannot pass `i64`
across the JS/C boundary.

**Callers MUST reconstruct the full 64-bit value:**
```js
const lo = Module._emu_get_time_ns_lo() >>> 0;  // unsigned!
const hi = Module._emu_get_time_ns_hi() >>> 0;
const t  = BigInt(hi) * 0x100000000n + BigInt(lo);
```

| Boundary | ns | Wall time | What breaks |
|----------|----|-----------|-------------|
| i32 sign flip | 2,147,483,648 | ~2.15 s | `lo \| 0` treats bit 31 as sign → time appears negative |
| u32 wrap | 4,294,967,296 | ~4.29 s | Ignoring `hi` → time appears to restart from 0 |

The `>>> 0` (unsigned right-shift by zero) is the JS idiom that coerces
a signed i32 to an unsigned u32. Without it, `lo` goes negative at 2.15 s.

The `on_advance` callback signature is `(lo32, hi32, user_data)` for the
same reason. Both `emu_advance_to_ns` and `emu_dbg_run_until_ns` accept
`(lo, hi)` pairs.

**Tested:** `test_soak.c` runs 5.5 seconds of simulated time, crossing both
boundaries, verifying zero time-monotonicity violations and zero edge-period
jitter across the u32 wrap.

### Pin mode encoding

`emu_get_pin_mode(port, bit)` returns a 2-bit value `(M1 << 1) | M0`
matching the STC12/STC15 PxM1/PxM0 register encoding:

| Value | M1 | M0 | Mode | Drive behavior |
|-------|----|----|------|----------------|
| 0 | 0 | 0 | Quasi-bidirectional | Weak pull-up (~230 µA), strong pull-down (20 mA). Reset default. |
| 1 | 0 | 1 | Push-pull | Strong drive both directions (20 mA). |
| 2 | 1 | 0 | Input-only | High impedance. Latch value ignored for output. |
| 3 | 1 | 1 | Open-drain | No internal pull-up. Needs external pull-up. Used for I2C. |

The same encoding appears in the `on_pin_change` callback's `mode` parameter
and in the pin history event struct.

**Tested:** `test_integration.c` → `test_pin_mode_transitions` asserts all four
modes on P2 with mixed per-pin configurations.

### Mode-change events are pin events (design ruling)

A write to PxM0 or PxM1 that changes a pin's mode fires `on_pin_change`
with the **new mode** and the **current latch value**, even if the latch
did not change. This is correct and intentional:

- **The board's circuit model depends on mode.** A quasi-bidirectional pin
  driving high sources ~230 µA; a push-pull pin driving high sources 20 mA.
  The 87× current difference directly affects LED brightness, I2C pull-up
  behavior, and every Thévenin-based component model.
- **The adapter calls `board.setPin(pin, mode, driveHigh)` on every
  callback**, passing both mode and drive. Suppressing mode-only events
  would leave the board with a stale Thévenin model until the next latch
  write.
- **ucsim does not emit these events.** This is a documented convention
  difference: ucsim's trace format omits mode transitions. When
  cross-checking, mode events are stripped before comparison. The
  remaining data-level events match exactly.

Reference: STC12-PERIPHERAL-MODEL.md §3, §7.2; bw-board
`emu8051-adapter.js` line 142.

### Push-callback vs read-callback (pollPins contract)

The `on_pin_change` callback fires when firmware writes to a port data
register **or** a port-mode register (PxM0/PxM1). It does NOT fire at
registration time and does NOT fire for ports that firmware only reads.

For ports that are **input-only** (e.g., P3.2 = INT0 as a button), the
host receives pin state requests through the `on_read_pin` callback. This
is the "pollPins" contract: the adapter seats input pins by registering a
`read_pin` callback, not by waiting for a `pin_change` push notification.

At reset, all port latches and shadows are 0xFF (quasi-bidirectional,
all high). The first firmware write that deviates from 0xFF fires the
first `on_pin_change`. A port that is never written never fires.

**Tested:** `test_integration.c` → `test_push_callback_read_only` boots
firmware that reads P3 without writing it, verifying `readPin` is called
1200+ times while `pin_change` fires zero times for P3.

## Serial (UART)

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_set_serial_callback(fn)` | `(ptr) → void` | Register TX callback: `fn(byte, ud)`. |
| `emu_serial_write(byte)` | `(i32) → void` | Inject RX byte (simulates keyboard input). |
| `emu_serial_read_buf()` | `() → ptr` | Pointer to 18-byte TX ring buffer. |
| `emu_serial_read_idx()` | `() → i32` | Current write position in TX buffer. |
| `emu_set_serial2_callback(fn)` | `(ptr) → void` | UART2 TX callback. |
| `emu_serial2_write(byte)` | `(i32) → void` | UART2 RX inject. |

## Interrupt State

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_get_interrupt_active()` | `() → i32` | Returns `mInterruptActive` (0 = no ISR running). |

## Boundary D — Debug Control

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_dbg_state()` | `() → i32` | 0=halted, 1=running. |
| `emu_dbg_run()` | `() → void` | Start running. |
| `emu_dbg_halt()` | `() → void` | Halt execution. |
| `emu_dbg_step(kind,count)` | `(i32,i32) → i32` | Step. kind: 0=insn, 1=line, 2=block, 3=over, 4=out. |
| `emu_dbg_reset()` | `() → void` | Reset CPU (keeps code memory). |
| `emu_dbg_tick()` | `() → i32` | One tick in debug mode. Returns 1 if halted. |
| `emu_dbg_run_until_ns(lo,hi)` | `(i32,i32) → i32` | Run until time or halt. Returns 1 if BP/step. |
| `emu_dbg_set_bp_code(addr)` | `(i32) → i32` | Set code breakpoint. Returns handle. |
| `emu_dbg_set_bp_yield(addr,task,state)` | `(i32,i32,i32) → i32` | Set yield breakpoint. |
| `emu_dbg_set_bp_write(space,addr)` | `(i32,i32) → i32` | Set write watchpoint. Halts when the byte changes. Space: 0=code,1=iram,2=sfr,3=xram. |
| `emu_dbg_clear_bp(handle)` | `(i32) → void` | Clear breakpoint. |
| `emu_dbg_read_mem(space,addr,len)` | `(i32,i32,i32) → ptr` | Read memory. space: 0-4. Returns pointer. |
| `emu_dbg_write_mem(space,addr,val)` | `(i32,i32,i32) → void` | Write one byte. |
| `emu_dbg_pc()` | `() → i32` | Read PC. |
| `emu_dbg_acc()` | `() → i32` | Read accumulator. |
| `emu_dbg_b()` | `() → i32` | Read B register. |
| `emu_dbg_dptr()` | `() → i32` | Read DPTR (16-bit). |
| `emu_dbg_sp()` | `() → i32` | Read stack pointer. |
| `emu_dbg_psw()` | `() → i32` | Read PSW. |
| `emu_dbg_rn(n)` | `(i32) → i32` | Read R0-R7 (bank-aware). |
| `emu_dbg_set_on_halt(fn)` | `(ptr) → void` | Register halt callback. |
| `emu_dbg_consumes_count()` | `() → i32` | Returns 0 (emulator consumes nothing). |

## Level 1 Position (Cooperative Scheduler)

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_dbg_bw_ms()` | `() → i32` | Read `bw_ms` (millisecond tick counter). |
| `emu_dbg_task_state(idx)` | `(i32) → i32` | Read task state (yield position). |
| `emu_dbg_task_until(idx)` | `(i32) → i32` | Read task wait deadline. |
| `emu_dbg_set_bw_ms_addr(addr)` | `(i32) → void` | Set IRAM address of `bw_ms`. |
| `emu_dbg_set_task(idx,state_addr,until_addr)` | `(i32,i32,i32) → void` | Set task IRAM addresses. |

## Profiling

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_dbg_profile_start()` | `() → void` | Start recording PC histogram. |
| `emu_dbg_profile_stop()` | `() → void` | Stop recording. |
| `emu_dbg_profile_get(addr)` | `(i32) → i32` | Hit count for code address. |
| `emu_dbg_profile_total()` | `() → i32` | Total instructions profiled. |

## Pin History

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_pin_history_enable()` | `() → void` | Allocate 4096-entry ring buffer. |
| `emu_pin_history_count()` | `() → i32` | Total events recorded. |
| `emu_pin_history_head()` | `() → i32` | Current write position. |
| `emu_pin_history_get(idx)` | `(i32) → ptr` | Pointer to event struct at index. |
| `emu_pin_event_size()` | `() → i32` | Size of one event (for JS offset calc). |

## Miscellaneous

| Function | Signature | Description |
|----------|-----------|-------------|
| `emu_get_interrupt_active()` | `() → i32` | Current interrupt nesting level. |
