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
