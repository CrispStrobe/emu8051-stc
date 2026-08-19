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

## rainbowpeee STC15 corpus (2026-08-17, post Keil→SDCC translator)

Real-world STC15F2K60S2 firmware from github rainbowpeee/STC15F2K60S2 (MIT).
31 projects (33 buildable sub-projects), compiled with SDCC 4.2.0 via
automated Keil→SDCC translation (`tools/keil2sdcc.py`).

### Results

| Project | Compile | Hex bytes | Pins (2s) | Verdict |
|---------|---------|-----------|-----------|---------|
| 空程序 (empty) | OK | 388 | 8 | boots |
| 流水灯 (LED chaser) | OK | 476 | 219 | boots |
| 10、DS1302-SEG | OK | 1600 | 238043 | boots |
| 11、PCF8591 数码管 | OK | 1126 | 243890 | boots |
| 1302数码管显示 | OK | 2055 | 463695 | boots |
| 17-频率采集 | OK | 1228 | 106226 | boots |
| 19、LED1602 | OK | 467 | 149 | boots |
| GPSLCD自动授时 | OK | 1426 | 1 | boots |
| LED汉字显示/01- | OK | 321 | 1473983 | boots |
| LED汉字显示/01一 | OK | 349 | 1306900 | boots |
| LED汉字显示/02- | OK | 365 | 1454362 | boots |
| LED汉字显示/03- | OK | 558 | 1331872 | boots |
| LOCKKeil4 | OK | 492 | 0 | boots (no pins) |
| OLED12864 1.3寸遥控 | OK | 4252 | 1571984 | boots |
| 串口转发/01 | OK | 360 | 0 | boots (no pins) |
| 串行口/01 | OK | 360 | 0 | boots (no pins) |
| 偏振子按摩 | OK | 719 | 1094696 | boots |
| 定时器 | OK | 139 | 0 | boots (no pins) |
| 开发板点阵测试 | OK | 1348 | 1257210 | boots |
| 按键数码管测试 | OK | 259 | 0 | boots (no pins) |
| 数码管按键测试/按键 | OK | 259 | 0 | boots (no pins) |
| 数码管按键测试/函数 | OK | 266 | 169181 | boots |
| 数码管测试 | OK | 1968 | 44833 | boots |
| 步进电机驱动/正转 | OK | 239 | 8916 | boots |
| 步进电机驱动/反转 | OK | 239 | 7451 | boots |
| 测试按键 | OK | 852 | 2239383 | boots |
| 温度 | OK | 1853 | 35315 | boots |
| 温度计 | OK | 4167 | 114211 | boots |
| 点阵测试-1616 | OK | 1083 | 1269541 | boots |
| 红外人体感应灯 | OK | 543 | 30856 | boots |
| 红外灯+温度注释点阵 | OK | 2014 | 85139 | boots |
| 超声波_时间/4届 | OK | 4759 | 9206 | boots |
| LCD12864 | link-fail | - | - | missing serial_one_init (source incomplete) |
| MFRC522KEIL4 | cc-fail | - | - | idata→generic pointer type mismatch |
| MFRC522OLEDKEIL4 | cc-fail | - | - | same as MFRC522KEIL4 |

### Totals

| Status | Count |
|--------|-------|
| **boots under emu8051** | **30** |
| source incomplete (missing file) | **1** (LCD12864) |
| SDCC type error (idata ptr) | **2** (MFRC522×2) |
| wedges | **0** |

### Keil→SDCC translator (`tools/keil2sdcc.py`)

Automated translation of 6 Keil C51 constructs to SDCC equivalents:

| Keil construct | SDCC equivalent | Projects affected |
|----------------|-----------------|-------------------|
| `sfr NAME = 0xNN` | `__sfr __at(0xNN) NAME` | all |
| `sbit NAME = SFR^N` | `__sbit __at(base+N) NAME` | 16 |
| `bit` type | `__bit` | 11 |
| `code` qualifier | `__code` | 20+ |
| `interrupt N` | `__interrupt(N)` | 13 |
| `idata` qualifier | `__idata` | 2 |

Master SDCC STC15 header: `tools/stc15f2k60s2_sdcc.h` (drop-in for
all Keil-style vendor headers). Intrinsic stub: `tools/intrins_sdcc.h`.

### Source-level bugs patched during translation

| Bug | Projects | Fix |
|-----|----------|-----|
| Missing variable declarations | 温度, 按键数码管×2 | added declarations |
| Duplicate function names | 数码管测试 | renamed `delay()` → `delay_display()` |
| `static` in shared header | 红外人体感应灯 | removed `static` from prototype |
| Missing sbit defines | LED汉字/01一 | added SHCP/STCP/DS sbits |
| Typos (I vs 1, diaplay) | LED汉字/01一 | corrected |
| Missing semicolons | LED汉字/01一 | added |
| Array without size | 按键数码管×2 | added size or made extern |
| `extern` on definitions | multiple | removed `extern` on initialised arrays |
| `display_num` never defined | 5 projects | extern→definition or stub |

These are all bugs in the original Keil source code, not translator
issues. The Keil compiler is more permissive about implicit types,
missing declarations, and conflicting qualifiers.

## mogoreanu/8x16 STC15 corpus (2026-08-10)

Real-world STC15F2K60S2 firmware: 8×16 LED matrix pixel editor with
button input, Timer 0 interrupt-driven display refresh, port-mode
configuration (push-pull). Source: github mogoreanu/8x16 (MIT).

Keil→SDCC translation required (`sfr`/`sbit`/`interrupt` syntax).

| Project | Device | Compile | Hex bytes | TMOD | Pins (2s) | Verdict |
|---------|--------|---------|-----------|------|-----------|---------|
| 8x16 | STC15F2K60S2 | OK (SDCC 4.2.0) | 1589 | 0x01 | 71 | boots |

Exercises: P0–P5 port writes, P0M0/P1M0/P2M0/P4M0/P5M0 push-pull config,
Timer 0 mode 1 (16-bit), ET0+EA interrupt enable, SP above 0x7F (LCALL
into interrupt handler). This program was the **root-cause witness for the
PUSH/POP direct-addressing bug** (`f83ea4e`): SP=0x7F caused LCALL to
write the return address to SFR P0 instead of upper IRAM, wedging the CPU
in a restart loop.

## Post PUSH/POP fix re-verification (2026-08-10)

The PUSH/POP fix (`f83ea4e`) changes stack behavior for all firmware.
Re-verified key programs from the catalog census:

| Example | Post-fix pins | Status |
|---------|---------------|--------|
| 61-console-pong (STC15) | 5745 | OK (matches pre-fix ±2) |
| 60-retro-console (STC15) | 2443 | OK (exact match) |
| 01-blink (STC12) | 344 | OK |

Full test suite: **39/39 pass** (436+ assertions). No regressions.

## Cross-emulator oracle summary (2026-08-18)

All firmware cross-checked against ucsim at FOSC=11059200, 500ms sim.
Mode-change events (from PxM0/PxM1 writes) are stripped before comparison
— these are **intentional** per spec-update 011: the board's Thévenin
model depends on mode, so mode transitions are correct pin events.

| Corpus | Programs | Data-exact | Mode-only | Prefix | Data divergences |
|--------|----------|-----------|-----------|--------|-----------------|
| Catalog (test_images) | 32 | 22 | 7 | 3 | 0 |
| STC15 (pong, retro, multimeter, mogoreanu) | 4 | 2 | 0 | 2 | 0 |
| rainbowpeee (Keil→SDCC) | 31 | 13 | 0 | 13 | 0 (5 timer-boundary) |
| **Total** | **67** | **37** | **7** | **18** | **0** |

**Data-exact**: identical after stripping mode events.
**Mode-only**: program configures pin modes but never writes port data
(emu8051 emits mode events; ucsim emits nothing; both are correct).
**Prefix**: first N data events match; tail divergence at sim-time
boundary (timer ISR count difference).

**Zero data-level divergences across 67 programs and 3 part IDs
(STC12, STC15, STC89). Zero unexplained divergences. The mode-event
convention is settled in spec-update 011 and API.md.**

## A2 multiplexed-display parts (2026-08-18, stc-compiler 623e165)

SEVENSEG8 (ISR-scanned 8-digit 7-seg) and LEDBANK8 (ISR-owned LED bank),
compiled from pseudocode via the stc-compiler API.

### Boot census

| Program | Hex bytes | PIN (2s) | TMOD | IE | P0M0 | P1M0 | P2M0 | Verdict |
|---------|-----------|----------|------|------|------|------|------|---------|
| sevenseg8 | 1812 | 28475 | 0x01 | 0x82 | 0xFF | 0x00 | 0x07 | clean |
| ledbank8 | 1018 | 8 | 0x01 | 0x82 | 0x00 | 0xFF | 0x00 | clean |
| combined | 2134 | 32153 | 0x01 | 0x82 | 0xFF | 0xFF | 0x07 | clean |

### SFR init conformance

All three programs set the expected SFR values at 50 ms:

| SFR | Expected | sevenseg8 | ledbank8 | combined | Status |
|-----|----------|-----------|----------|----------|--------|
| AUXR (0x8E) | 0x00 (T0x12=0, 12T) | 0x00 | 0x00 | 0x00 | ✓ |
| TMOD (0x89) | 0x01 (Timer 0 mode 1) | 0x01 | 0x01 | 0x01 | ✓ |
| TCON (0x88) | 0x10 (TR0=1) | 0x10 | 0x10 | 0x10 | ✓ |
| IE (0xA8) | 0x82 (EA=1, ET0=1) | 0x82 | 0x82 | 0x82 | ✓ |
| P0M0 (0x94) | 0xFF (seg PP) or 0x00 | 0xFF | 0x00 | 0xFF | ✓ |
| P0M1 (0x93) | 0x00 | 0x00 | 0x00 | 0x00 | ✓ |
| P1M0 (0x92) | 0xFF (LED PP) or 0x00 | 0x00 | 0xFF | 0xFF | ✓ |
| P2M0 (0x96) | 0x07 (select PP) or 0x00 | 0x07 | 0x00 | 0x07 | ✓ |

### Cross-emulator agreement (emu8051 vs ucsim, 500ms)

| Program | emu events | ucsim events | Data match | Notes |
|---------|-----------|-------------|-----------|-------|
| sevenseg8 | 7124 | 7119 | prefix (7031 after mode strip) | +88 ucsim P2 select events (timer boundary) |
| ledbank8 | 8 | 0 | **exact** (0/0 after mode strip) | mode-only |
| combined | 8031 | 8029 | prefix (7901 after mode strip) | +128 ucsim P2 select events (timer boundary) |

The sevenseg8/combined divergence is the timer-ISR boundary pattern:
ucsim fires more timer interrupts in 500 ms, producing extra P2 digit-select
events. Same root cause as the rainbowpeee timer divergences. No logic bugs.

## KEYPAD4X4 + A2 sampler (2026-08-18, stc-compiler 623e165)

Keypad scanner firmware: row-scan with 2-NOP settle, polled inline.
A2 sampler combines KEYPAD4X4 + SEVENSEG8 + LEDBANK8 on a single timer ISR.

### Boot census

| Program | Device | Hex bytes | PIN (2s) | Verdict |
|---------|--------|-----------|----------|---------|
| keypad-stc89 | STC89C52RC | 2046 | 4352 | clean |
| keypad-hats | STC89C52RC | 1920 | 25104 | clean |
| keypad-stc12 | STC12C5A60S2 | 2052 | 544 | clean |
| a2-sampler | STC12C5A60S2 | 2716 | 7057 | clean |

### SFR init conformance

| SFR | keypad-stc89 | keypad-hats | keypad-stc12 | a2-sampler |
|-----|-------------|-------------|-------------|-----------|
| AUXR | 0x00 | 0x00 | 0x15 (BRT) | 0x00 |
| TMOD | 0x21 (T0+T1) | 0x01 | 0x01 | 0x01 |
| IE | 0x00 (polled) | 0x82 (EA+ET0) | 0x00 (polled) | 0x82 (EA+ET0) |
| SCON | 0x50 (UART) | 0x00 | 0x50 (UART) | 0x00 |
| P1M0 | 0x00 (quasi) | 0x00 (quasi) | 0xFF (PP) | 0xFF (PP) |
| P0M0 | — | — | — | 0xFF (segs) |
| P2M0 | — | — | — | 0x07 (select) |
| P3M0 | — | — | — | 0xFF (LEDs) |

STC89 programs have no PxM0/PxM1 writes (quasi-bidi only). STC12 programs
set all 8 keypad pins push-pull (both rows and columns — the scan drives
rows low one at a time, the pushed-high idle state on columns is readable).

Keypad scan does `EA=0` / `EA=1` around the row-scan critical section,
causing IE to toggle between 0x80 and 0x82 during the scan window.

### Cross-emulator agreement (emu8051 vs ucsim, 500ms)

| Program | emu | ucsim | Match | Notes |
|---------|-----|-------|-------|-------|
| keypad-stc89 | 136 | 136 | **exact** | STC89, no mode events |
| keypad-hats | 800 | 800 | **exact** | STC89, scheduler+debounce |
| keypad-stc12 | 144 | 136 | **exact** (−8 mode) | P1M0 push-pull init |
| a2-sampler | 1790 | 1763 | **exact** (−27 mode) | P0+P1+P2+P3 init |

**All four exact match after mode strip. Zero data divergences.**

## MATRIX8X8 fixtures (2026-08-19, stc-compiler 623e165)

KEYPAD4X4+MATRIX8X8 combined (keypress updates frame buffer) and
MATRIX8X8 BCM 4-level brightness. Both STC12 and STC89 variants.

### Boot census

| Program | Device | Hex bytes | PIN (2s) | Verdict |
|---------|--------|-----------|----------|---------|
| keypad-matrix | STC12C5A60S2 | 3598 | 40016 | clean |
| keypad-matrix-89 | STC89C52RC | 3516 | 114509 | clean |
| matrix-bcm | STC12C5A60S2 | 3004 | 41978 | clean |
| matrix-bcm-89 | STC89C52RC | 2954 | 114511 | clean |

### SFR init conformance

| SFR | keypad-matrix (STC12) | matrix-bcm (STC12) | Expected |
|-----|----------------------|-------------------|----------|
| AUXR | 0x00 (12T) | 0x00 | ✓ |
| TMOD | 0x01 | 0x01 | ✓ |
| IE | 0x82 (EA+ET0) | 0x82 | ✓ |
| P0M0 | 0xFF (columns PP) | 0xFF | ✓ |
| P1M0 | 0xFF (keypad PP) | 0x00 | ✓ |
| P3M0 | 0x70 (595 DATA+LATCH+CLOCK) | 0x70 | ✓ — bits 4,5,6 |
| P3M1 | 0x00 | 0x00 | ✓ |

STC89 variants: no PxM0/PxM1 writes (quasi-bidi only) — correct.

### Cross-emulator agreement (emu8051 vs ucsim, 500ms)

| Program | emu | ucsim | Match | Notes |
|---------|-----|-------|-------|-------|
| keypad-matrix-89 | 9991 | 9991 | **exact** | STC89, no mode events |
| matrix-bcm-89 | 10485 | 10485 | **exact** | STC89, no mode events |
| keypad-matrix | 10010 | 9991 | **exact** (−19 mode) | P0+P1+P3 push-pull init |
| matrix-bcm | 10496 | 10485 | **exact** (−11 mode) | P0+P3 push-pull init |

**All four exact match. Zero data divergences.**
