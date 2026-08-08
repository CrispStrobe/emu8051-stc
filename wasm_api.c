/* WASM API for emu8051-stc
 * Copyright 2024 CrispStrobe
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * (i.e. the MIT License)
 *
 * wasm_api.c
 * Thin C API layer for the WASM build. Provides a flat, JS-friendly
 * interface to the emulator core and STC12 peripherals.
 */

#include <stdlib.h>
#include <string.h>
#include <emscripten/emscripten.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int initialized = 0;

/* No-op exception handler for WASM */
static void wasm_exception(struct em8051 *aCPU, int aCode) {
    (void)aCPU; (void)aCode;
}

/* ------------------------------------------------------------------ *
 * Lifecycle                                                           *
 * ------------------------------------------------------------------ */

EMSCRIPTEN_KEEPALIVE
void emu_init(int stc12_mode) {
    memset(&cpu, 0, sizeof(cpu));
    cpu.mCodeMemMaxIdx = 65535;
    cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535;
    cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    cpu.except = wasm_exception;

    reset(&cpu, 1);

    if (stc12_mode) {
        stc12_init(&cpu, &stc);
        cpu.skip_timers = true;
    }

    initialized = 1;
}

EMSCRIPTEN_KEEPALIVE
void emu_reset(int wipe) {
    if (!initialized) return;
    int stc_mode = stc.stc12_mode;
    reset(&cpu, wipe ? true : false);
    if (stc_mode) {
        stc12_init(&cpu, &stc);
        cpu.skip_timers = true;
    }
}

/* ------------------------------------------------------------------ *
 * Execution                                                           *
 * ------------------------------------------------------------------ */

/* Run one machine cycle. Returns 1 if an instruction completed. */
EMSCRIPTEN_KEEPALIVE
int emu_tick(void) {
    if (!initialized) return 0;
    bool ticked = tick(&cpu);
    if (stc.stc12_mode)
        stc12_tick(&cpu, &stc);
    return ticked ? 1 : 0;
}

/* Run N machine cycles. Returns number of instructions completed. */
EMSCRIPTEN_KEEPALIVE
int emu_run(int cycles) {
    if (!initialized) return 0;
    int count = 0;
    for (int i = 0; i < cycles; i++) {
        bool ticked = tick(&cpu);
        if (stc.stc12_mode)
            stc12_tick(&cpu, &stc);
        if (ticked) count++;
    }
    return count;
}

/* ------------------------------------------------------------------ *
 * Memory access                                                       *
 * ------------------------------------------------------------------ */

EMSCRIPTEN_KEEPALIVE
int emu_load_hex(const char *hex_data, int length) {
    if (!initialized) return -1;
    /* Parse Intel HEX from a string buffer */
    int pos = 0;
    while (pos < length) {
        if (hex_data[pos] != ':') { pos++; continue; }
        pos++; /* skip ':' */

        /* Parse hex byte helper */
        #define HEX2(p) (int)( \
            ((hex_data[p] >= '0' && hex_data[p] <= '9') ? (hex_data[p] - '0') : \
             (hex_data[p] >= 'A' && hex_data[p] <= 'F') ? (hex_data[p] - 'A' + 10) : \
             (hex_data[p] >= 'a' && hex_data[p] <= 'f') ? (hex_data[p] - 'a' + 10) : 0) * 16 + \
            ((hex_data[p+1] >= '0' && hex_data[p+1] <= '9') ? (hex_data[p+1] - '0') : \
             (hex_data[p+1] >= 'A' && hex_data[p+1] <= 'F') ? (hex_data[p+1] - 'A' + 10) : \
             (hex_data[p+1] >= 'a' && hex_data[p+1] <= 'f') ? (hex_data[p+1] - 'a' + 10) : 0))

        if (pos + 8 > length) return -2;

        int reclen = HEX2(pos); pos += 2;
        int addr_hi = HEX2(pos); pos += 2;
        int addr_lo = HEX2(pos); pos += 2;
        int addr = (addr_hi << 8) | addr_lo;
        int rectype = HEX2(pos); pos += 2;

        if (rectype == 1) return 0; /* EOF record */
        if (rectype != 0) return -3; /* unsupported record type */

        if (pos + reclen * 2 + 2 > length) return -2;

        for (int i = 0; i < reclen; i++) {
            int data = HEX2(pos); pos += 2;
            if (addr + i <= (int)cpu.mCodeMemMaxIdx)
                cpu.mCodeMem[addr + i] = data;
        }
        pos += 2; /* skip checksum */
        #undef HEX2
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t emu_get_sfr(int addr) {
    if (addr >= 0x80 && addr <= 0xFF)
        return cpu.mSFR[addr - 0x80];
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void emu_set_sfr(int addr, uint8_t value) {
    if (addr >= 0x80 && addr <= 0xFF)
        cpu.mSFR[addr - 0x80] = value;
}

EMSCRIPTEN_KEEPALIVE
uint8_t emu_get_iram(int addr) {
    if (addr < 128) return cpu.mLowerData[addr];
    if (cpu.mUpperData && addr < 256) return cpu.mUpperData[addr - 128];
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void emu_set_iram(int addr, uint8_t value) {
    if (addr < 128) cpu.mLowerData[addr] = value;
    else if (cpu.mUpperData && addr < 256) cpu.mUpperData[addr - 128] = value;
}

EMSCRIPTEN_KEEPALIVE
uint8_t emu_get_code(int addr) {
    if (addr >= 0 && addr <= (int)cpu.mCodeMemMaxIdx)
        return cpu.mCodeMem[addr];
    return 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t emu_get_xdata(int addr) {
    if (addr >= 0 && addr <= (int)cpu.mExtDataMaxIdx)
        return cpu.mExtData[addr];
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void emu_set_xdata(int addr, uint8_t value) {
    if (addr >= 0 && addr <= (int)cpu.mExtDataMaxIdx)
        cpu.mExtData[addr] = value;
}

EMSCRIPTEN_KEEPALIVE
uint16_t emu_get_pc(void) {
    return cpu.mPC;
}

EMSCRIPTEN_KEEPALIVE
void emu_set_pc(uint16_t pc) {
    cpu.mPC = pc;
}

/* ------------------------------------------------------------------ *
 * Disassembly                                                         *
 * ------------------------------------------------------------------ */

static char disasm_buf[64];

EMSCRIPTEN_KEEPALIVE
const char *emu_disasm(int addr) {
    decode(&cpu, (uint16_t)addr, disasm_buf);
    return disasm_buf;
}

/* ------------------------------------------------------------------ *
 * STC12 peripheral access                                             *
 * ------------------------------------------------------------------ */

EMSCRIPTEN_KEEPALIVE
void emu_set_adc_input(int channel, int value) {
    stc12_set_adc_input(&stc, channel, (uint16_t)value);
}

EMSCRIPTEN_KEEPALIVE
void emu_set_port_input(int port, uint8_t value) {
    stc12_set_port_input(&stc, port, value);
}

EMSCRIPTEN_KEEPALIVE
void emu_set_fosc(uint32_t hz) {
    stc.fosc = hz;
    if (hz > 0)
        stc.ns_per_clock_x16 = (uint64_t)(16.0e9 / hz + 0.5);
}

/* ------------------------------------------------------------------ *
 * Boundary A — pin bus API                                            *
 * These conform to simulation-contract.md boundary A.                 *
 * ------------------------------------------------------------------ */

/* Get the current pin state for boundary A consumers.
 * Returns: mode (0=quasi, 1=pushpull, 2=input, 3=opendrain) */
EMSCRIPTEN_KEEPALIVE
int emu_get_pin_mode(int port, int bit) {
    if (port < 0 || port > 5 || bit < 0 || bit > 7) return 0;
    static const uint8_t m1_regs[] = {
        STC_REG_P0M1, STC_REG_P1M1, STC_REG_P2M1,
        STC_REG_P3M1, STC_REG_P4M1, STC_REG_P5M1
    };
    static const uint8_t m0_regs[] = {
        STC_REG_P0M0, STC_REG_P1M0, STC_REG_P2M0,
        STC_REG_P3M0, STC_REG_P4M0, STC_REG_P5M0
    };
    int m1 = (cpu.mSFR[m1_regs[port]] >> bit) & 1;
    int m0 = (cpu.mSFR[m0_regs[port]] >> bit) & 1;
    return (m1 << 1) | m0;
}

/* Get the latch (drive) value for a pin */
EMSCRIPTEN_KEEPALIVE
int emu_get_pin_drive(int port, int bit) {
    if (port < 0 || port > 5 || bit < 0 || bit > 7) return 1;
    static const uint8_t port_regs[] = {
        REG_P0, REG_P1, REG_P2, REG_P3, STC_REG_P4, STC_REG_P5
    };
    return (cpu.mSFR[port_regs[port]] >> bit) & 1;
}

/* Set external pin input from the board (digital).
 * Use this instead of emu_set_port_input for per-pin control. */
EMSCRIPTEN_KEEPALIVE
void emu_set_pin_input(int port, int bit, int level) {
    if (port < 0 || port > 5 || bit < 0 || bit > 7) return;
    if (level)
        stc.port_ext[port] |= (1 << bit);
    else
        stc.port_ext[port] &= ~(1 << bit);
}

/* Set ADC input as voltage (0.0 .. VCC).
 * The MCU converts to 10-bit counts internally. */
EMSCRIPTEN_KEEPALIVE
void emu_set_adc_voltage(int channel, double volts) {
    if (channel < 0 || channel >= 8) return;
    double vcc = stc.vcc > 0.0 ? stc.vcc : 5.0;
    if (volts < 0.0) volts = 0.0;
    if (volts > vcc) volts = vcc;
    stc.adc_input[channel] = (uint16_t)(volts / vcc * 1023.0 + 0.5);
}

/* Run until the MCU clock reaches target_ns. Returns instructions executed. */
EMSCRIPTEN_KEEPALIVE
int emu_advance_to_ns(uint32_t target_ns_lo, uint32_t target_ns_hi) {
    if (!initialized) return 0;
    uint64_t target = ((uint64_t)target_ns_hi << 32) | target_ns_lo;
    return stc12_advance_to(&cpu, &stc, target);
}

/* Get current MCU time in nanoseconds (returns low 32 bits; use _hi for upper). */
EMSCRIPTEN_KEEPALIVE
uint32_t emu_get_time_ns_lo(void) {
    return (uint32_t)(stc12_get_time_ns(&stc) & 0xFFFFFFFF);
}

EMSCRIPTEN_KEEPALIVE
uint32_t emu_get_time_ns_hi(void) {
    return (uint32_t)(stc12_get_time_ns(&stc) >> 32);
}

/* Set VCC (supply voltage for ADC reference). Default 5.0. */
EMSCRIPTEN_KEEPALIVE
void emu_set_vcc(double vcc) {
    stc.vcc = vcc;
}

/* Register boundary A board callbacks from JS.
 * Function pointers are created via Emscripten's addFunction(). */
EMSCRIPTEN_KEEPALIVE
void emu_set_board_callbacks(
    stc12_pin_callback on_pin,
    stc12_read_pin_callback on_read,
    stc12_read_analog_callback on_analog,
    stc12_advance_callback on_advance,
    void *user_data)
{
    stc12_set_board_callbacks(&stc, on_pin, on_read, on_analog,
                              on_advance, user_data);
}
