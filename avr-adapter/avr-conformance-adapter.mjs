/**
 * avr-conformance-adapter.mjs — wraps avr8js-adapter to match
 * bw-board's McuAdapter conformance interface.
 *
 * This bridges the gap between our boundary-A adapter and the
 * test harness bw-board uses to verify MCU implementations.
 */
import { createAvrAdapter } from './avr8js-adapter.mjs';
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const { PinState, AVRIOPort, portBConfig, portCConfig, portDConfig } = require('avr8js');

/**
 * Create a conformance-compatible AVR adapter.
 * @param {object} [opts]
 * @param {number} [opts.clockHz=16000000]
 */
export function createAvrConformanceAdapter(opts = {}) {
    let clockHz = opts.clockHz || 16_000_000;
    let flash = new Uint16Array(16384);
    let adapter = createAvrAdapter({ flash, clockHz });

    const pinHistory = [];
    const timeHistory = [];

    // Board reference (set by attachBoard)
    let board = null;

    return {
        reset() {
            flash = new Uint16Array(16384);
            adapter = createAvrAdapter({ flash, clockHz });
            pinHistory.length = 0;
            timeHistory.length = 0;
            if (board) this.attachBoard(board);
        },

        setFosc(hz) {
            clockHz = hz;
            // Rebuild adapter with new clock
            adapter = createAvrAdapter({ flash, clockHz });
            if (board) this.attachBoard(board);
        },

        attachBoard(b) {
            board = b;
            adapter.setBoardCallbacks({
                setPin: (pin, mode, driveHigh) => {
                    const tNs = BigInt(adapter.getTimeNs());
                    pinHistory.push({ pin, mode, driveHigh, tNs });
                    if (b.setPin) b.setPin(pin, mode, driveHigh);
                },
                readPin: (pin) => b.readPin ? b.readPin(pin) : 0,
                readAnalog: (pin) => b.readAnalog ? b.readAnalog(pin) : 0,
                advanceTo: (tNs) => {
                    timeHistory.push(tNs);
                    if (b.advanceTo) b.advanceTo(tNs);
                },
            });
        },

        writePort(port, value) {
            // AVR ports: 0=B, 1=C, 2=D
            if (port >= 0 && port < 3) {
                const portRegAddr = [0x25, 0x28, 0x2B][port];
                adapter.cpu.writeData(portRegAddr, value);
                // Trigger port listener (writeData alone doesn't fire it)
                adapter.ports[port].updatePinRegister(adapter.cpu.data);
            }
        },

        setPortMode(port, m1, m0) {
            // For AVR: m0 is DDR (direction). m1 is unused (no STC-style modes).
            if (port >= 0 && port < 3) {
                const ddrAddr = [0x24, 0x27, 0x2A][port];
                adapter.cpu.writeData(ddrAddr, m0);
                adapter.ports[port].updatePinRegister(adapter.cpu.data);
            }
        },

        readPort(port) {
            if (port >= 0 && port < 3) {
                const pinAddr = [0x23, 0x26, 0x29][port]; // PINB, PINC, PIND
                return adapter.cpu.readData(pinAddr);
            }
            return 0xFF;
        },

        runNs(ns) {
            adapter.advanceTo(adapter.getTimeNs() + ns);
        },

        // ADC — ATmega328P ADC on PC0-PC5
        startAdc(channel) {
            // Write ADMUX and ADCSRA to start a conversion
            const admux = (channel & 0x0F); // AVcc ref, right-adjusted
            adapter.cpu.writeData(0x7C, admux);  // ADMUX
            adapter.cpu.writeData(0x7A, 0xC7);   // ADCSRA: ADEN+ADSC+prescaler
        },

        adcReady() {
            // Check ADIF (bit 4 of ADCSRA at 0x7A)
            return (adapter.cpu.readData(0x7A) & 0x10) !== 0;
        },

        readAdc() {
            const lo = adapter.cpu.readData(0x78); // ADCL
            const hi = adapter.cpu.readData(0x79); // ADCH
            return (hi << 8) | lo;
        },

        getPinHistory() { return pinHistory; },
        getTimeHistory() { return timeHistory; },

        // Extra: access to raw adapter for advanced tests
        get raw() { return adapter; },
    };
}
