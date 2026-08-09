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
| `emu_set_board_callbacks(pin,read,analog,advance,ud)` | `(ptr×5) → void` | Register push-mode callbacks via `addFunction`. |
| `emu_get_time_ns_lo()` | `() → i32` | Nanoseconds since reset (low 32 bits). |
| `emu_get_time_ns_hi()` | `() → i32` | Nanoseconds since reset (high 32 bits). |

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
