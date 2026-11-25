/** Exact shipped-bundle qualification for STC15 mode0, W408AS, and rewind.
 * Native tests cover register tables; this test also executes real MOV/SETB
 * opcodes so a debugger-only SFR write cannot hide missing firmware hooks.
 */
import assert from 'node:assert/strict';
import createEmu8051 from './build/emu8051.js';

const module = await createEmu8051();
const wrap = (name, returns = 'number', args = []) => module.cwrap(name, returns, args);
const init = wrap('emu_init', null, ['number']);
const setPart = wrap('emu_set_part', null, ['number']);
const getSfr = wrap('emu_get_sfr', 'number', ['number']);
const setSfr = wrap('emu_set_sfr', null, ['number', 'number']);
const loadHex = wrap('emu_load_hex', 'number', ['string', 'number']);
const tick = wrap('emu_tick');
const run = wrap('emu_run', 'number', ['number']);
const getPc = wrap('emu_get_pc');
const setPc = wrap('emu_set_pc', null, ['number']);
const capabilities = wrap('emu_capabilities', 'string');
const save = wrap('emu_checkpoint_save', 'number', ['number', 'number']);
const restore = wrap('emu_checkpoint_restore', 'number', ['number', 'number']);
const size = wrap('emu_checkpoint_size')();
let checks = 0;
function equal(actual, expected, label) {
    assert.deepEqual(actual, expected, label);
    checks++;
}
function record(address, bytes) {
    const data = [bytes.length, address >> 8, address & 255, 0, ...bytes];
    data.push((-data.reduce((sum, byte) => sum + byte, 0)) & 255);
    return ':' + data.map(byte => byte.toString(16).padStart(2, '0')).join('') + '\n';
}
function untilPc(address) {
    let clocks = 0;
    while (getPc() !== address && clocks < 100) { tick(); clocks++; }
    equal(getPc(), address, 'firmware reached its next instruction');
    return clocks;
}
const counter = () => (getSfr(0x8c) << 8) | getSfr(0x8a);
function afterClocks(current, reload, clocks) {
    const toOverflow = 65536 - current;
    return clocks < toOverflow ? current + clocks
        : reload + ((clocks - toOverflow) % (65536 - reload));
}

equal(wrap('emu_checkpoint_version')(), 1, 'checkpoint major version');
equal(wrap('emu_checkpoint_build_id')() >>> 0, 0x80510103, 'checkpoint layout fingerprint');
equal(size, 443561, 'exact checkpoint size includes the two hidden timer fields');

for (const part of [1, 3]) {
    init(1);
    setPart(part);
    // MOV TMOD,#0; MOV AUXR,#80h; MOV TH0,#FFh; MOV TL0,#F0h;
    // SETB TR0; SJMP $. At 0100h, a running write changes the next reload.
    const hex = record(0, [0x75, 0x89, 0, 0x75, 0x8e, 0x80,
        0x75, 0x8c, 0xff, 0x75, 0x8a, 0xf0, 0xd2, 0x8c, 0x80, 0xfe]) +
        record(0x100, [0x75, 0x8a, 0xe0, 0x80, 0xfe]) + ':00000001FF\n';
    equal(loadHex(hex, hex.length), 0, 'valid timer firmware loads');
    untilPc(14);
    const initial = counter();
    run(48);
    equal(counter(), afterClocks(initial, 0xfff0, 48),
        'three full 16-bit auto-reload periods retain the preload');
    equal(getSfr(0x88) & 0x20, 0x20, 'mode0 overflow sets TF0');

    const beforeWrite = counter();
    setPc(0x100);
    const writeClocks = untilPc(0x103);
    equal(counter(), afterClocks(beforeWrite, 0xfff0, writeClocks),
        'running firmware write preserves the readable counter');
    const savedCounter = counter();
    const ptr = module._malloc(size);
    try {
        equal(save(ptr, size), 0, 'save with distinct counter and reload');
        const saved = module.HEAPU8.slice(ptr, ptr + size);
        run(80);
        const expected = afterClocks(savedCounter, 0xffe0, 80);
        equal(counter(), expected, 'new reload takes effect at the next overflow');
        equal(restore(ptr, size), 0, 'restore timer checkpoint');
        equal(counter(), savedCounter, 'restore visible counter');
        run(80);
        equal(counter(), expected, 'rewind preserves the next reload and execution phase');
        equal(restore(ptr, size - 4), -3, 'reject the previous checkpoint length');
        module.HEAPU8[ptr + 8] = 2; // previous layout 0x80510102
        equal(restore(ptr, size), -6, 'reject the previous checkpoint fingerprint');
        equal(counter(), expected, 'failed restore is atomic');
        module.HEAPU8.set(saved, ptr);
    } finally { module._free(ptr); }

    const caps = JSON.parse(capabilities());
    equal(caps.part, part === 3 ? 'stc15w408as' : 'stc15f2k60s2', 'native part identity');
    for (const name of ['timer0', 'timer2', 'pca', 'pwm', 'uart1'])
        equal(caps.peripherals.includes(name), true, `${name} is present`);
    if (part === 3) {
        for (const name of ['timer1', 'uart2', 'dualDptr'])
            equal(caps.peripherals.includes(name), false, `${name} is absent on W408AS`);
        equal(wrap('emu_get_flash_size')(), 8192, 'W408AS flash');
        equal(wrap('emu_get_xram_size')(), 256, 'W408AS auxiliary RAM');
    }

    // The real bundle must expose all three default PCA outputs on the
    // STC15 pin group, including W408AS's previously suppressed modules.
    init(1);
    setPart(part);
    setSfr(0xa2, 0); // default group P1.1, P1.0, P3.7
    setSfr(0xd9, 8); // PCA clock = SYSclk
    for (let mod = 0; mod < 3; mod++) {
        setSfr(0xda + mod, 0x42); // ECOM | PWM
        setSfr(0xea + mod, 128);
        setSfr(0xfa + mod, 128);
    }
    setSfr(0xd8, 0x40); // CR
    run(1);
    equal([getSfr(0x90) & 3, getSfr(0xb0) & 128], [0, 0], 'three PCA outputs low');
    run(127);
    equal([getSfr(0x90) & 3, getSfr(0xb0) & 128], [3, 128], 'three PCA compare edges high');
}
console.log(`${checks} passed, 0 failed (exact WASM STC15 fidelity)`);
