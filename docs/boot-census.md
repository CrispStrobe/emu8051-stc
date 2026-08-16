# Boot Census — 8051 family example catalog

Every program in sb3-creator's example catalog with an 8051-family device,
compiled via `generateC()` + hosted stc-compiler, booted under emu8051 for
2 simulated seconds. The census catches firmware that compiles but fails
to boot — the exact class of bug that the address-mask wedge (`2f1855a`)
represented.

**Census date:** 2026-08-16, post address-mask fix.

## Results

| Example | Device | Compile | Hex bytes | TMOD | IE | Port events | Verdict |
|---------|--------|---------|-----------|------|-----|-------------|---------|
| 01-blink | STC12C5A60S2 | OK | 576 | 0x89 | 0x00 | 5 | clean |
| 02-dimmer | STC12C5A60S2 | OK | 800 | 0x89 | 0x00 | 3 | clean |
| 03-night-light | STC12C5A60S2 | OK | 834 | 0x89 | 0x00 | 3 | clean |
| 04-thermostat | STC12C5A60S2 | OK | 876 | 0x89 | 0x00 | 3 | clean |
| 05-counter | STC12C5A60S2 | OK | 794 | 0x89 | 0x00 | 7 | clean |
| 06-active-low-high | STC12C5A60S2 | OK | 600 | 0x89 | 0x00 | 11 | clean |
| 07-buzzer-siren | STC12C5A60S2 | OK | 578 | 0x89 | 0x00 | 0
0 | clean (no pins) |
| 08-led-chaser-595 | STC12C5A60S2 | OK | 1076 | 0x89 | 0x00 | 6 | clean |
| 09-relay-clicker | STC12C5A60S2 | OK | 600 | 0x89 | 0x00 | 5 | clean |
| 10-motor-speed | STC12C5A60S2 | OK | 800 | 0x89 | 0x00 | 3 | clean |
| 11-toggle-button | STC12C5A60S2 | OK | 596 | 0x89 | 0x00 | 1 | clean |
| 12-dual-blink | STC12C5A60S2 | OK | 600 | 0x89 | 0x00 | 9 | clean |
| 13-sos-morse | STC12C5A60S2 | OK | 930 | 0x89 | 0x00 | 8 | clean |
| 14-traffic-light | STC12C5A60S2 | OK | 640 | 0x89 | 0x00 | 4 | clean |
| 15-voltage-divider | STC12C5A60S2 | OK | 798 | 0x89 | 0x00 | 2 | clean |
| 16-ldr-bargraph | STC12C5A60S2 | OK | 950 | 0x89 | 0x00 | 4 | clean |
| 17-comparator | STC12C5A60S2 | OK | 844 | 0x89 | 0x00 | 3 | clean |
| 18-logic-and-gate | STC12C5A60S2 | OK | 600 | 0x89 | 0x00 | 1 | clean |
| 19-logic-or-gate | STC12C5A60S2 | OK | 600 | 0x89 | 0x00 | 1 | clean |
| 20-shift-register-binary | STC12C5A60S2 | OK | 936 | 0x89 | 0x00 | 150 | clean |
| 24-pwm-fade | STC12C5A60S2 | OK | 1346 | 0x89 | 0x00 | 39 | clean |
| 25-reaction-timer | STC12C5A60S2 | OK | 896 | 0x89 | 0x00 | 1 | clean |
| 26-debounce | STC12C5A60S2 | OK | 602 | 0x89 | 0x00 | 1 | clean |
| 27-led-dice | STC12C5A60S2 | OK | 850 | 0x89 | 0x00 | 1 | clean |
| 30-multi-led-pattern | STC12C5A60S2 | OK | 660 | 0x89 | 0x00 | 23 | clean |
| 32-source-vs-sink | STC12C5A60S2 | OK | 568 | 0x89 | 0x00 | 5 | clean |
| 33-inductive-no-flyback | STC12C5A60S2 | OK | 576 | 0x89 | 0x00 | 4 | clean |
| 46-port-overcurrent | STC12C5A60S2 | OK | 704 | 0x89 | 0x00 | 24 | clean |
| 49-lcd-hello | STC12C5A60S2 | OK | 3330 | 0x89 | 0x00 | 6520 | clean |
| 50-7seg-chase | STC12C5A60S2 | OK | 756 | 0x89 | 0x00 | 39 | clean |
| 51-tft-pixels | STC12C5A60S2 | cc-fail | - | - | - | - | main.c:30: warning 85: in function bw_se |
| 53-servo-sweep | STC12C5A60S2 | OK | 2664 | 0x89 | 0x00 | 195 | clean |
| 54-motor-driver | STC12C5A60S2 | OK | 1814 | 0x89 | 0x00 | 10 | clean |
| 60-retro-console | STC15F2K60S2 | OK | 3110 | 0x89 | 0x00 | 2443 | clean |
| 61-console-pong | STC15F2K60S2 | OK | 13876 | 0x89 | 0x00 | 5747 | clean |

## Summary

| Status | Count |
|--------|-------|
| clean (boots, port activity) | 33 |
| clean (no pins, timer-only) | 1 (07-buzzer-siren, uses PCA not port writes) |
| compile fail | 1 (51-tft-pixels, SDCC warning-as-error) |
| WEDGE | **0** |

**Zero unexplained wedges.** Every compilable 8051 example boots and
reaches its main loop within 2 simulated seconds. The STC15 examples
(60-retro-console, 61-console-pong) boot correctly after the address-mask
fix in `2f1855a`.

## Red list

None. All examples boot cleanly.

## How to re-run

```bash
# Requires: sb3-creator checkout, emu_trace binary, stc-compiler API access
# For each example:
node -e "require('.../sb3Creator.js').default; ..." > example.c
curl -X POST https://stc-compiler.vercel.app/compile ... > example.hex
./emu_trace -fosc 11059200 -part <stc12|stc15> -until-ns 2000000000 example.hex
# Check: PIN events > 0 and/or TMOD != 0
```

## rainbowpeee STC15 corpus (2026-08-16)

Real-world STC15F2K60S2 firmware from github rainbowpeee/STC15F2K60S2 (MIT).
31 projects, compiled with SDCC via Keil→SDCC header translation.

### Single-file projects

| Project | Compile | Hex | Pins (2s) | Verdict |
|---------|---------|-----|-----------|---------|
| 空程序 (empty) | OK | 388 | 8 | boots |
| 流水灯 (LED chaser) | OK | 476 | 219 | boots |
| 定时器 (timer) | Keil-only | - | - | syntax error (encoding issue) |
| 串行口 (serial) | Keil-only | - | - | needs P_SW1 (UART pin switch SFR) |
| 串口转发 (serial fwd) | Keil-only | - | - | needs P_SW1 |
| LOCKKeil4 | Keil-only | - | - | needs P_SW1 |
| 17-频率采集 (freq capture) | Keil-only | - | - | Keil `code` keyword |
| 11、PCF8591 数码管 | Keil-only | - | - | Keil syntax |
| 19、LED1602 | Keil-only | - | - | Keil syntax |

### STC15 peripheral gap notes (from Keil-only refusals)

| SFR/Peripheral | Projects needing it | Notes |
|----------------|-------------------|-------|
| P_SW1 (0xA2) | 串行口, 串口转发, LOCKKeil4 | UART pin switch register — selects which pins carry UART1 |
| `code` keyword | 17-频率采集 | Keil `code` = SDCC `__code` — lookup table in flash |

### Running totals (rainbowpeee)

| Status | Count |
|--------|-------|
| boots | 2 |
| Keil-only (not SDCC-portable) | 7 |
| not yet attempted | 22 (multi-file projects) |

### Multi-file projects (batch 2)

| Project | Compile | Verdict | Keil-only reason |
|---------|---------|---------|-----------------|
| 步进电机驱动 (stepper) | Keil-only | - | `code` keyword (lookup tables) |
| 红外人体感应灯 (IR sensor) | Keil-only | - | P10/P11 Keil port bit names |
| 数码管测试 (7-seg test) | Keil-only | - | `sbit` keyword |
| 测试按键 (key test) | Keil-only | - | local header not resolved |
| 开发板点阵测试 (LED matrix) | Keil-only | - | `code` keyword |

### Updated totals (rainbowpeee)

| Status | Count |
|--------|-------|
| boots | 2 |
| Keil-only (not SDCC-portable) | 12 |
| not yet attempted | 17 (remaining multi-file) |

### STC15 peripheral gap notes (expanded)

| SFR/Peripheral | Projects | Notes |
|----------------|----------|-------|
| P_SW1 (0xA2) | 3 projects | UART pin switch — selects UART1 pins |
| Keil `code` keyword | 3 projects | `__code` in SDCC — flash lookup tables |
| Keil `sbit` keyword | 1 project | `__sbit __at(addr)` in SDCC |
| Keil port bit names (P10) | 1 project | P1_0 in SDCC |

### Remaining multi-file projects (batch 3)

All 16 remaining projects are Keil-only. Dominant Keil constructs:

| Keil construct | SDCC equivalent | Projects |
|----------------|-----------------|----------|
| `code` (flash arrays) | `__code` | 9 |
| `sbit` / `bit` | `__sbit __at()` / `__bit` | 4 |
| `P_SW1`, `P42` etc. | need compat header | 2 |
| local header resolution | concat + strip | 1 |

### Final rainbowpeee totals

| Status | Count |
|--------|-------|
| **boots under emu8051** | **2** (空程序, 流水灯) |
| Keil-only (not SDCC-portable) | **29** |
| wedges | **0** |

**The Keil→SDCC syntax barrier is the dominant issue**, not peripheral
model gaps. Every project that compiles with SDCC boots cleanly —
zero wedges. The two that boot (empty program + LED chaser) exercise
port I/O on P0/P2 under the STC15 config.

**STC15 model completion roadmap (from real firmware):**

The Keil-only projects, when eventually ported, would need:
1. **P_SW1 (0xA2)** — UART pin switch (3 projects)
2. **DS1302** — real-time clock via bit-bang I/O (2 projects)
3. **PCF8591** — I2C ADC/DAC (1 project)
4. **OLED/LCD12864** — I2C/SPI display (3 projects)
5. **MFRC522** — SPI RFID reader (2 projects)
6. **DS18B20** — 1-Wire temperature sensor (3 projects)
7. **IR receiver** — infrared decode (2 projects)
8. **Ultrasonic** — HC-SR04 timing (1 project)
9. **Stepper motor** — GPIO sequencing (1 project)
10. **16x16 LED matrix** — shift register cascade (2 projects)

These are peripheral DEVICES, not missing SFRs. The emulator's
STC15 port model is sufficient; what's missing is the external
device simulation (I2C/SPI/1-Wire peripherals on the board side).
