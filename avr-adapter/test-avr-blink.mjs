/**
 * test-avr-blink.mjs — Arduino-style blink through the AVR adapter
 * and the board layer.
 *
 * Hand-assembled ATmega328P program that:
 *   1. Sets PB5 as output (DDRB bit 5)
 *   2. Sets PB5 high (LED on)
 *   3. Busy-waits ~100k cycles
 *   4. Sets PB5 low (LED off)
 *   5. Busy-waits ~100k cycles
 *   6. Loops to 2
 *
 * Verifies: pin events fire, LED toggles, timing is measurable.
 */
import { createAvrAdapter } from './avr8js-adapter.mjs';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);

let passed = 0, failed = 0;
function check(cond, msg) {
    if (cond) { console.log(`PASS: ${msg}`); passed++; }
    else      { console.log(`FAIL: ${msg}`); failed++; }
}

console.log('=== AVR blink through board layer ===\n');

const flash = new Uint16Array(16384);

// Hand-assembled ATmega328P blink with delay loop:
//
// 0000: SBI  DDRB, 5      ; 9A25 — PB5 output
// 0001: SBI  PORTB, 5     ; 9A2D — LED on
// 0002: LDI  R24, 0       ; E080 — delay counter low
// 0003: LDI  R25, 0x10    ; E190 — delay counter high (4096 iterations)
// 0004: SBIW R24, 1       ; 9701 — decrement 16-bit counter
// 0005: BRNE -2 (0004)    ; F7F1 — loop if not zero
// 0006: CBI  PORTB, 5     ; 982D — LED off
// 0007: LDI  R24, 0       ; E080
// 0008: LDI  R25, 0x10    ; E190
// 0009: SBIW R24, 1       ; 9701
// 000A: BRNE -2 (0009)    ; F7F1
// 000B: RJMP -11 (0001)   ; CFF5 — back to LED on

flash[0x0000] = 0x9A25; // SBI DDRB, 5
flash[0x0001] = 0x9A2D; // SBI PORTB, 5
flash[0x0002] = 0xE080; // LDI R24, 0
flash[0x0003] = 0xE190; // LDI R25, 0x10
flash[0x0004] = 0x9701; // SBIW R24, 1
flash[0x0005] = 0xF7F1; // BRNE -2
flash[0x0006] = 0x982D; // CBI PORTB, 5
flash[0x0007] = 0xE080; // LDI R24, 0
flash[0x0008] = 0xE190; // LDI R25, 0x10
flash[0x0009] = 0x9701; // SBIW R24, 1
flash[0x000A] = 0xF7F1; // BRNE -2
flash[0x000B] = 0xCFF5; // RJMP -11

const adapter = createAvrAdapter({ flash, clockHz: 16_000_000 });

// Collect pin events
const pinEvents = [];
adapter.setBoardCallbacks({
    setPin: (pin, mode, driveHigh) => {
        pinEvents.push({ pin, mode, driveHigh, tNs: adapter.getTimeNs() });
    },
    readPin: () => 0,
    readAnalog: () => 0,
    advanceTo: () => {},
});

// Run enough cycles for several blink periods
// 4096 iterations * ~4 cycles/iter * 2 phases = ~32k cycles per blink
// Run 200k cycles for multiple blinks
adapter.run(200_000);

check(pinEvents.length > 0, `Pin events: ${pinEvents.length}`);

// Filter PB5 events
const pb5 = pinEvents.filter(e => e.pin === 'PB5');
check(pb5.length >= 4, `PB5 events: ${pb5.length} (expect >=4 for DDR+on+off+on)`);

if (pb5.length >= 3) {
    // First: DDR change (input → pushpull)
    check(pb5[0].mode === 'pushpull', `PB5[0] DDR: mode=${pb5[0].mode}`);

    // Second: LED on
    check(pb5[1].driveHigh === true, `PB5[1] LED on: drive=${pb5[1].driveHigh}`);

    // Third: LED off
    check(pb5[2].driveHigh === false, `PB5[2] LED off: drive=${pb5[2].driveHigh}`);

    // Check toggle count
    const toggles = pb5.filter(e => e.mode === 'pushpull').length - 1; // minus DDR
    check(toggles >= 2, `PB5 toggles: ${toggles}`);

    // Measure on-time (between LED-on and LED-off)
    if (pb5.length >= 3) {
        const onTime = pb5[2].tNs - pb5[1].tNs;
        console.log(`  LED on-time: ${onTime} ns (${(onTime/1e6).toFixed(2)} ms)`);
        check(onTime > 0, `LED on-time > 0`);

        // At 16 MHz, 4096 iterations * ~4 cycles = ~16384 cycles = ~1.024 ms
        const expectedMs = (4096 * 4) / 16_000_000 * 1000;
        const actualMs = onTime / 1e6;
        console.log(`  Expected ~${expectedMs.toFixed(2)} ms, got ${actualMs.toFixed(2)} ms`);
    }
}

// Verify time advanced
const totalTime = adapter.getTimeNs();
check(totalTime > 0, `Total time: ${totalTime} ns`);

// Capability report
const caps = adapter.capabilities();
console.log('\n=== AVR capability matrix ===');
console.log(`  Part: ${caps.part}`);
console.log(`  Steps: ${JSON.stringify(caps.steps)} (no debugger yet)`);
console.log(`  Breakpoints: ${JSON.stringify(caps.breakpoints)}`);
console.log(`  Peripherals: ${JSON.stringify(caps.peripherals)}`);
console.log(`  debugTarget: ${caps.debugTarget}`);
console.log(`  timeFreezes: ${caps.timeFreezes}`);

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
