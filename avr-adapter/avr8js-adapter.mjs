/**
 * avr8js-adapter.mjs — boundary A adapter for avr8js (ATmega328P).
 *
 * Implements the same McuToBoard / BoardToMcu contract as the 8051 path,
 * proving the contract is chip-agnostic.
 *
 * McuToBoard: setPin(pin, mode, driveHigh), advanceTo(tNs)
 * BoardToMcu: readPin(pin) → 0|1, readAnalog(pin) → volts
 *
 * Pin naming: "PB0".."PB5" (Arduino D8-D13), "PD0".."PD7" (D0-D7),
 *             "PC0".."PC5" (A0-A5).
 *
 * @module
 */

import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const {
    CPU, avrInstruction, AVRIOPort, AVRTimer, AVRADC,
    portBConfig, portCConfig, portDConfig,
    timer0Config, timer1Config, timer2Config,
    adcConfig, PinState
} = require('avr8js');

/**
 * Map avr8js PinState to boundary A (mode, driveHigh).
 *
 * AVR pin states:
 *   Low (0)          → DDR=1, PORT=0 → pushpull, drive=false
 *   High (1)         → DDR=1, PORT=1 → pushpull, drive=true
 *   Input (2)        → DDR=0, PORT=0 → input, drive=false
 *   InputPullUp (3)  → DDR=0, PORT=1 → input with ~35k pull-up
 *
 * Boundary A has 4 modes: quasi, pushpull, input, opendrain.
 * AVR has no quasi or open-drain. InputPullUp is modelled as 'input'
 * with driveHigh=true (the pull-up is weak, ~35kΩ — same shape as
 * the STC12's quasi-bidirectional but even weaker).
 */
function pinStateToMode(state) {
    switch (state) {
        case PinState.Low:          return { mode: 'pushpull', driveHigh: false };
        case PinState.High:         return { mode: 'pushpull', driveHigh: true };
        case PinState.Input:        return { mode: 'input', driveHigh: false };
        case PinState.InputPullUp:  return { mode: 'input-pullup', driveHigh: true };
        default:                    return { mode: 'input', driveHigh: false };
    }
}

const PORT_NAMES = ['B', 'C', 'D'];
const PORT_CONFIGS = [portBConfig, portCConfig, portDConfig];

/**
 * Create an AVR MCU adapter behind boundary A.
 *
 * @param {object} opts
 * @param {Uint16Array} opts.flash - program memory (16-bit words)
 * @param {number} [opts.clockHz=16000000] - clock frequency
 * @returns {object} adapter with run/halt/setPin/readPin/readAnalog/advanceTo
 */
export function createAvrAdapter(opts) {
    const { flash, clockHz = 16_000_000 } = opts;

    const cpu = new CPU(flash);
    const ports = PORT_CONFIGS.map(cfg => new AVRIOPort(cpu, cfg));
    const timer0 = new AVRTimer(cpu, timer0Config);
    const timer1 = new AVRTimer(cpu, timer1Config);
    const timer2 = new AVRTimer(cpu, timer2Config);
    const adc = new AVRADC(cpu, adcConfig);

    // Boundary A callbacks (set by the board)
    let onSetPin = null;       // (pin, mode, driveHigh) => void
    let onAdvanceTo = null;    // (tNs) => void
    let readPinFn = null;      // (pin) => 0|1
    let readAnalogFn = null;   // (pin) => volts

    // ADC voltage inputs (set by readAnalog callback)
    const adcVolts = new Float64Array(8); // PC0-PC5 = ADC0-ADC5

    // Time tracking
    const nsPerCycle = 1e9 / clockHz;
    let totalCycles = 0;

    // Install port listeners for boundary A setPin
    ports.forEach((port, portIdx) => {
        const portName = PORT_NAMES[portIdx];
        const nPins = portName === 'C' ? 6 : 8; // PC only has 6 pins

        // Track previous state for change detection
        const prevState = new Uint8Array(nPins);
        for (let i = 0; i < nPins; i++) prevState[i] = PinState.Input;

        port.addListener(() => {
            for (let bit = 0; bit < nPins; bit++) {
                const state = port.pinState(bit);
                if (state !== prevState[bit]) {
                    prevState[bit] = state;
                    const { mode, driveHigh } = pinStateToMode(state);
                    const pin = `P${portName}${bit}`;
                    if (onSetPin) onSetPin(pin, mode, driveHigh);
                }
            }
        });
    });

    // Wire ADC to readAnalog
    adc.onADCRead = (channel) => {
        if (channel < 8 && readAnalogFn) {
            const pin = `PC${channel}`;
            const volts = readAnalogFn(pin);
            adcVolts[channel] = volts;
            // Convert volts to 10-bit counts (AREF = 5V default)
            return Math.round((volts / 5.0) * 1023);
        }
        return 0;
    };

    function tick() {
        avrInstruction(cpu);
        cpu.tick();  // drives timers, interrupts, and all peripherals
        totalCycles++;
    }

    function getTimeNs() {
        return Math.round(totalCycles * nsPerCycle);
    }

    return {
        /** The raw avr8js CPU, for debug access */
        cpu,
        ports,

        /** Load Intel HEX into flash */
        loadHex(hexString) {
            const lines = hexString.split('\n');
            for (const line of lines) {
                if (!line.startsWith(':')) continue;
                const len = parseInt(line.slice(1, 3), 16);
                const addr = parseInt(line.slice(3, 7), 16);
                const type = parseInt(line.slice(7, 9), 16);
                if (type !== 0) continue; // only data records
                for (let i = 0; i < len; i++) {
                    const byte = parseInt(line.slice(9 + i * 2, 11 + i * 2), 16);
                    const wordAddr = (addr + i) >> 1;
                    if ((addr + i) & 1) {
                        flash[wordAddr] = (flash[wordAddr] & 0xFF) | (byte << 8);
                    } else {
                        flash[wordAddr] = (flash[wordAddr] & 0xFF00) | byte;
                    }
                }
            }
        },

        /** Register boundary A callbacks */
        setBoardCallbacks({ setPin, readPin, readAnalog, advanceTo }) {
            onSetPin = setPin || null;
            readPinFn = readPin || null;
            readAnalogFn = readAnalog || null;
            onAdvanceTo = advanceTo || null;
        },

        /** Set external pin input (from board to MCU) */
        setPin(pin, level) {
            const match = pin.match(/^P([BCD])(\d)$/);
            if (!match) return;
            const portIdx = PORT_NAMES.indexOf(match[1]);
            const bit = parseInt(match[2]);
            if (portIdx >= 0) {
                ports[portIdx].setPin(bit, level ? true : false);
            }
        },

        /** Run N cycles */
        run(cycles) {
            for (let i = 0; i < cycles; i++) tick();
        },

        /** Advance to a nanosecond target */
        advanceTo(targetNs) {
            const targetCycles = Math.ceil(targetNs / nsPerCycle);
            while (totalCycles < targetCycles) tick();
            if (onAdvanceTo) onAdvanceTo(BigInt(getTimeNs()));
        },

        /** Current time in nanoseconds */
        getTimeNs,

        /** Capabilities (boundary D §7 shape) */
        capabilities() {
            return {
                steps: [],           // no debug target for AVR yet
                breakpoints: [],
                spaces: ['code', 'sram'],
                writable: ['sram'],
                sfrs: 'none',
                haltPolicy: 'freeze-timers',
                timeFreezes: true,
                consumes: [],
                part: 'atmega328p',
                peripherals: ['timer0', 'timer1', 'timer2', 'adc', 'uart0'],
                // Honest: no boundary-D debugger for AVR yet
                debugTarget: false,
            };
        },

        clockHz,
        totalCycles: () => totalCycles,
    };
}
