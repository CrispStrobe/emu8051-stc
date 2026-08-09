/**
 * test-avr-adapter.mjs — verify the AVR adapter behind boundary A.
 *
 * Loads a hand-assembled ATmega328P blink program, runs it, and
 * checks that pin events match the boundary A contract.
 */
import { createAvrAdapter } from './avr8js-adapter.mjs';

let passed = 0, failed = 0;
function check(cond, msg) {
    if (cond) { console.log(`PASS: ${msg}`); passed++; }
    else      { console.log(`FAIL: ${msg}`); failed++; }
}

console.log('=== AVR adapter boundary A tests ===\n');

// --- Test 1: Create adapter ---
const flash = new Uint16Array(16384); // 32KB flash
const adapter = createAvrAdapter({ flash, clockHz: 16_000_000 });
check(adapter !== null, 'Adapter created');
check(adapter.clockHz === 16_000_000, `Clock: ${adapter.clockHz} Hz`);

// --- Test 2: Capabilities ---
const caps = adapter.capabilities();
check(caps.part === 'atmega328p', `Part: ${caps.part}`);
check(caps.timeFreezes === true, 'timeFreezes: true');
check(caps.debugTarget === false, 'debugTarget: false (honest)');
check(caps.peripherals.includes('timer0'), 'Has timer0');

// --- Test 3: Hand-assemble a blink program ---
// ATmega328P: PB5 = Arduino D13 (built-in LED)
// Program: set DDRB bit 5 (output), toggle PORTB bit 5
//
// AVR instructions (16-bit words, little-endian):
//   SBI DDRB, 5       = 0x9A25  (set bit 5 of DDRB, I/O addr 0x04)
//   SBI PORTB, 5      = 0x9A2D  (set bit 5 of PORTB, I/O addr 0x05)
//   CBI PORTB, 5      = 0x982D  (clear bit 5 of PORTB)
//   RJMP -2            = 0xCFFD  (jump back to SBI PORTB)
flash[0] = 0x9A25;  // SBI DDRB, 5
flash[1] = 0x9A2D;  // SBI PORTB, 5
flash[2] = 0x982D;  // CBI PORTB, 5
flash[3] = 0xCFFE;  // RJMP -1 (back to CBI — tight loop with LED off)

// --- Test 4: Collect pin events ---
const pinEvents = [];
adapter.setBoardCallbacks({
    setPin: (pin, mode, driveHigh) => {
        pinEvents.push({ pin, mode, driveHigh });
    },
    readPin: () => 0,
    readAnalog: () => 0,
    advanceTo: () => {},
});

// Run enough cycles for the program to execute
adapter.run(20);

check(pinEvents.length > 0, `Pin events: ${pinEvents.length}`);

// Find PB5 events
const pb5Events = pinEvents.filter(e => e.pin === 'PB5');
check(pb5Events.length >= 2, `PB5 events: ${pb5Events.length} (DDR + PORTB writes)`);

if (pb5Events.length >= 2) {
    // First PB5 event should be DDR change (input → pushpull)
    check(pb5Events[0].mode === 'pushpull', `PB5[0]: mode=${pb5Events[0].mode} (pushpull from DDR)`);
    // Second should be PORTB=1 (LED on)
    check(pb5Events[1].driveHigh === true, `PB5[1]: drive=${pb5Events[1].driveHigh} (LED on)`);
}

// Find LED-off event
const pb5Off = pb5Events.find(e => e.driveHigh === false && e.mode === 'pushpull');
check(pb5Off !== undefined, 'PB5: LED-off event (CBI PORTB,5)');

// --- Test 5: Time tracking ---
const t = adapter.getTimeNs();
check(t > 0, `Time: ${t} ns (> 0)`);

// --- Test 6: setPin (board → MCU) ---
adapter.setPin('PD2', 1); // set input high
adapter.setPin('PD2', 0); // set input low
check(true, 'setPin: no crash');

// --- Test 7: Boundary A contract compliance ---
// Every pin event has (pin, mode, driveHigh) — the contract shape
for (const evt of pinEvents) {
    if (typeof evt.pin !== 'string' || typeof evt.mode !== 'string' ||
        typeof evt.driveHigh !== 'boolean') {
        check(false, 'Contract: event shape mismatch');
        break;
    }
}
check(true, 'Contract: all events have (pin:string, mode:string, driveHigh:boolean)');

// Modes are from the boundary A vocabulary
const validModes = new Set(['quasi', 'pushpull', 'input', 'opendrain', 'input-pullup']);
const allValid = pinEvents.every(e => validModes.has(e.mode));
check(allValid, `Contract: all modes in {${[...validModes].join(',')}}`);

// --- Test 8: Determinism ---
// Run the same program twice and assert identical pin event sequences
{
    const flash2 = new Uint16Array(16384);
    flash2[0] = 0x9A25; flash2[1] = 0x9A2D;
    flash2[2] = 0x982D; flash2[3] = 0xCFFE;
    const evts2 = [];
    const a2 = createAvrAdapter({ flash: flash2, clockHz: 16_000_000 });
    a2.setBoardCallbacks({
        setPin: (pin, mode, driveHigh) => evts2.push(`${pin}:${mode}:${driveHigh}`),
        readPin: () => 0, readAnalog: () => 0, advanceTo: () => {},
    });
    a2.run(20);

    const evts1 = pinEvents.map(e => `${e.pin}:${e.mode}:${e.driveHigh}`);
    const identical = evts1.length === evts2.length &&
                      evts1.every((v, i) => v === evts2[i]);
    check(identical, `Determinism: ${evts1.length} events identical on second run`);
}

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
