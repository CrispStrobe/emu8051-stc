/** The actual shipped bundle must retain its board configuration on reset.
 * Three NOPs measure the native clock; an ADC conversion measures Vcc. Merely
 * asserting the adapter's advertised clock would miss the original regression.
 */
import assert from 'node:assert/strict';
import createEmu8051 from './build/emu8051.js';

let checks = 0;
function equal(actual, expected, label) {
    assert.deepEqual(actual, expected, label);
    checks++;
}
function ok(actual, label) {
    assert.ok(actual, label);
    checks++;
}
function record(bytes) {
    const data = [bytes.length, 0, 0, 0, ...bytes];
    data.push((-data.reduce((sum, byte) => sum + byte, 0)) & 255);
    return ':' + data.map(byte => byte.toString(16).padStart(2, '0')).join('') +
        '\n:00000001FF\n';
}

// NOP x3; MOV A,P2; MOV P1,#FEh; MOV SBUF,#41h; MOV S2BUF,#42h;
// MOV P1ASF,#1; MOV ADC_CONTR,#E8h; SJMP $.
const hex = record([0, 0, 0, 0xe5, 0xa0, 0x75, 0x90, 0xfe, 0x75, 0x99, 0x41,
    0x75, 0x9b, 0x42, 0x75, 0x9d, 1, 0x75, 0xbc, 0xe8, 0x80, 0xfe]);

for (const part of [0, 1, 3]) {
    const m = await createEmu8051();
    m._emu_init(1);
    m._emu_set_part(part);
    m._emu_set_fosc(12000000);
    m._emu_set_vcc(3.3);
    const events = [];
    const userdata = 42;
    const pin = m.addFunction((port, bit, mode, high, user) =>
        events.push(['pin', port, bit, mode, high, user]), 'viiiii');
    const read = m.addFunction((port, bit, user) => {
        events.push(['read', port, bit, user]);
        return 1;
    }, 'iiii');
    const analog = m.addFunction((port, bit, user) => {
        events.push(['analog', port, bit, user]);
        return 1.65;
    }, 'diii');
    const advance = m.addFunction((lo, hi, user) =>
        events.push(['advance', lo, hi, user]), 'viii');
    const serial = m.addFunction((byte, user) =>
        events.push(['uart1', byte, user]), 'vii');
    const serial2 = m.addFunction((byte, user) =>
        events.push(['uart2', byte, user]), 'vii');
    const load = m.cwrap('emu_load_hex', 'number', ['string', 'number']);
    const ns = () => (BigInt(m._emu_get_time_ns_hi() >>> 0) << 32n) |
        BigInt(m._emu_get_time_ns_lo() >>> 0);
    const adc = () => (m._emu_get_sfr(0xbd) << 2) | (m._emu_get_sfr(0xbe) & 3);
    const measureNops = () => {
        m._emu_dbg_run();
        for (let i = 0; i < 3; ++i) m._emu_dbg_tick();
        m._emu_dbg_halt();
        equal(m._emu_get_pc(), 3, 'three NOPs actually executed');
        return ns();
    };
    const finishFirmware = () => {
        m._emu_dbg_run();
        m._emu_dbg_run_until_ns(100000, 0);
        m._emu_dbg_halt();
        // Exercise the separate boundary-A advance callback after debugger
        // execution as well. Neither reset path may drop any binding.
        m._emu_advance_to_ns(101000, 0);
    };

    m._emu_set_board_callbacks(pin, read, analog, advance, userdata);
    m._emu_set_serial_callback(serial);
    m._emu_set_serial2_callback(serial2);
    try {
        equal(load(hex, hex.length), 0, 'reset-test firmware loads');
        for (const { reset, wipe } of [
            { reset: () => m._emu_reset(0) },
            { reset: () => m._emu_dbg_reset() },
            { reset: () => m._emu_reset(1), wipe: true },
            { reset: () => m._emu_dbg_reset() },
        ]) {
            events.length = 0;
            reset();
            equal(events.length, 0, 'reset itself emits no fabricated callback');
            equal(ns(), 0n, 'reset restarts native time');
            equal(m._emu_get_flash_size(), part === 3 ? 8192 : 61440,
                'reset retains native part identity');
            equal(m._emu_get_code(3), wipe ? 0 : 0xe5,
                'only a cold reset wipes code memory');
            // A cold reset wipes code. Reloading the same program does not
            // touch the oscillator, supply, or callback registrations.
            if (wipe) equal(load(hex, hex.length), 0, 'reload after wipe');
            const elapsed = measureNops();
            ok(elapsed >= 249n && elapsed <= 251n, '12MHz survives hardware/debug reset');
            finishFirmware();
            equal(adc(), 512, '1.65V converts against the retained 3.3V supply');
            equal(events.filter(e => e[0] === 'uart1'), [['uart1', 0x41, userdata]],
                'the original UART1 callback still delivers once');
            equal(events.filter(e => e[0] === 'uart2'),
                part === 3 ? [] : [['uart2', 0x42, userdata]],
                'UART2 delivery retains the part-specific contract');
            ok(events.some(e => e[0] === 'pin' && e[1] === 1 && e[2] === 0 && e[4] === 0),
                'the original GPIO callback still delivers');
            ok(events.some(e => e[0] === 'analog'), 'the original analog callback still delivers');
            ok(events.some(e => e[0] === 'read'), 'the original digital-input callback still delivers');
            ok(events.some(e => e[0] === 'advance'), 'the original advance callback still delivers');
            ok(events.every(e => e.at(-1) === userdata), 'all callback user data survives');
        }

        // A new target has the historical initialization defaults, unlike a
        // restart of the already-configured target above.
        m._emu_init(1);
        equal(load(hex, hex.length), 0, 'firmware loads after raw init');
        events.length = 0;
        const defaultsElapsed = measureNops();
        ok(defaultsElapsed >= 270n && defaultsElapsed <= 272n,
            'raw init restores the default 11.0592MHz oscillator');
        finishFirmware();
        equal(adc(), 338, 'raw init restores the default 5V supply');
        equal(events.filter(e => e[0].startsWith('uart')), [],
            'raw init clears serial callbacks');
    } finally {
        m._emu_set_board_callbacks(0, 0, 0, 0, 0);
        m._emu_set_serial_callback(0);
        m._emu_set_serial2_callback(0);
        for (const fn of [pin, read, analog, advance, serial, serial2]) m.removeFunction(fn);
    }
}
console.log(`${checks} passed, 0 failed (exact WASM reset configuration)`);
