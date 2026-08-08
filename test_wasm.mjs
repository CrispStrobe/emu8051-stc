/**
 * test_wasm.mjs — smoke test for the WASM build.
 * Run: node test_wasm.mjs  (uses the emsdk node if system node is old)
 *
 * Tests:
 * 1. Module loads and exports are present
 * 2. Init in STC12 mode
 * 3. Load a tiny Intel HEX program (MOV A,#42h; SJMP $)
 * 4. Run and verify A == 0x42
 * 5. SFR read/write
 * 6. ADC input and port input APIs
 * 7. Disassembly
 */

import { createRequire } from 'module';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Load the Emscripten module
const createEmu8051 = (await import(join(__dirname, 'build', 'emu8051.js'))).default;

const Module = await createEmu8051();

// Wrap the C functions
const emu_init = Module.cwrap('emu_init', null, ['number']);
const emu_reset = Module.cwrap('emu_reset', null, ['number']);
const emu_tick = Module.cwrap('emu_tick', 'number', []);
const emu_run = Module.cwrap('emu_run', 'number', ['number']);
const emu_get_sfr = Module.cwrap('emu_get_sfr', 'number', ['number']);
const emu_set_sfr = Module.cwrap('emu_set_sfr', null, ['number', 'number']);
const emu_get_pc = Module.cwrap('emu_get_pc', 'number', []);
const emu_set_pc = Module.cwrap('emu_set_pc', null, ['number']);
const emu_get_iram = Module.cwrap('emu_get_iram', 'number', ['number']);
const emu_set_adc_input = Module.cwrap('emu_set_adc_input', null, ['number', 'number']);
const emu_set_port_input = Module.cwrap('emu_set_port_input', null, ['number', 'number']);
const emu_set_fosc = Module.cwrap('emu_set_fosc', null, ['number']);
const emu_disasm = Module.cwrap('emu_disasm', 'string', ['number']);
const emu_load_hex = Module.cwrap('emu_load_hex', 'number', ['string', 'number']);

let passed = 0;
let failed = 0;

function assert(cond, msg) {
    if (cond) {
        console.log(`PASS: ${msg}`);
        passed++;
    } else {
        console.log(`FAIL: ${msg}`);
        failed++;
    }
}

console.log('=== WASM smoke tests ===\n');

// Test 1: Init
emu_init(1); // STC12 mode
assert(emu_get_pc() === 0, 'Init — PC == 0');

// Test 2: Load Intel HEX
// Program: MOV A,#42h (74 42) then SJMP $ (80 FE) at address 0000
const hex = ':04000000744280FE44\n:00000001FF\n';
const result = emu_load_hex(hex, hex.length);
assert(result === 0, 'Load HEX — success');

// Test 3: Run until MOV A,#42h executes
emu_reset(0);
emu_run(10); // plenty of cycles for a 2-byte instruction
const acc = emu_get_sfr(0xE0); // ACC
assert(acc === 0x42, `Run MOV A,#42h — ACC == 0x${acc.toString(16)} (expected 0x42)`);

// Test 4: PC should be at the SJMP loop
const pc = emu_get_pc();
assert(pc === 2, `PC == ${pc} (expected 2, the SJMP target)`);

// Test 5: SFR read/write
emu_set_sfr(0x90, 0xAA); // P1
assert(emu_get_sfr(0x90) === 0xAA, 'SFR write/read P1 == 0xAA');

// Test 6: AUXR is accessible (STC12 register at 0x8E)
emu_set_sfr(0x8E, 0x80); // Set T0x12
assert(emu_get_sfr(0x8E) === 0x80, 'SFR write/read AUXR == 0x80 (T0x12 set)');

// Test 7: ADC and port input APIs don't crash
emu_set_adc_input(3, 512);
emu_set_port_input(1, 0x55);
emu_set_fosc(11059200);
assert(true, 'ADC/port/fosc APIs — no crash');

// Test 8: Disassembly
emu_reset(0);
const dis = emu_disasm(0);
assert(dis.includes('MOV') && dis.includes('42'), `Disasm at 0000: "${dis}"`);

// --- Boundary A tests ---

const getPinMode  = Module.cwrap('emu_get_pin_mode',  'number', ['number','number']);
const getPinDrive = Module.cwrap('emu_get_pin_drive',  'number', ['number','number']);
const setPinInput = Module.cwrap('emu_set_pin_input',  null,     ['number','number','number']);
const setAdcVolt  = Module.cwrap('emu_set_adc_voltage', null,    ['number','number']);
const advanceTo   = Module.cwrap('emu_advance_to_ns',  'number', ['number','number']);
const getTimeNsLo = Module.cwrap('emu_get_time_ns_lo', 'number', []);
const getTimeNsHi = Module.cwrap('emu_get_time_ns_hi', 'number', []);
const setVcc      = Module.cwrap('emu_set_vcc',         null,    ['number']);

// Test 9: Pin mode/drive after reset
emu_reset(0);
const mode = getPinMode(1, 0); // P1.0
assert(mode === 0, `Pin mode P1.0 = ${mode} (expected 0 = quasi-bidi)`);
const drive = getPinDrive(1, 0);
assert(drive === 1, `Pin drive P1.0 = ${drive} (expected 1 = high at reset)`);

// Test 10: Set P1M0 for push-pull, check mode changes
emu_set_sfr(0x92, 0x01); // P1M0 bit 0 = push-pull
const mode2 = getPinMode(1, 0);
assert(mode2 === 1, `Pin mode P1.0 after push-pull = ${mode2} (expected 1)`);

// Test 11: Per-pin input
setPinInput(3, 2, 0); // P3.2 low
setPinInput(3, 3, 1); // P3.3 high
// (These set the external state for port reads)
assert(true, 'Per-pin input API — no crash');

// Test 12: ADC voltage API
setVcc(5.0);
setAdcVolt(3, 2.5); // 2.5V on ch3 -> ~512 counts at 5V VCC
assert(true, 'ADC voltage API — no crash');

// Test 13: advanceTo / time tracking
emu_reset(0);
emu_set_fosc(11059200);
const t0 = getTimeNsLo();
advanceTo(1000000, 0); // advance to 1 ms
const t1 = getTimeNsLo();
assert(t1 >= 1000000, `Time after advanceTo(1ms) = ${t1}ns (expected >= 1000000)`);

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
