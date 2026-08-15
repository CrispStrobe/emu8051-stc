# CC0 Example Differential Verification Ledger

Cross-emulator verification of CC0 example programs: emu8051-stc vs ucsim-stc.
Each program compiled from its `.bw` pseudocode via the hosted stc-compiler API,
then run under both emulators for 2 seconds of simulated time.

**Comparison method:** pin state-change events (the observable output). Init
mode-set events are expected to differ (emu8051 reports mode changes, ucsim
may not). Timing differences under 1ms at 2s of runtime are interleaving noise.

## Batch 1 (2026-08-15)

| Example | Status | Detail |
|---------|--------|--------|
| 01-blink | agree | P1.0 toggles at ~500ms; emu has extra init PP-H event; timing drift ~250µs/1.5s |
| 06-active-low-high | agree | init events differ (mode-set); steady-state pin sequence matches |
| 07-buzzer-siren | agree | P1.3 toggle pattern matches; init event count differs |
| 12-dual-blink | agree | P1.0+P1.1 toggle at ~500ms; init mode-set events differ |
| 13-sos-morse | agree | P1.0 SOS pattern (dit-dit-dit dah-dah-dah); timing matches |
| 14-traffic-light | agree | P1.0-P1.2 sequence matches; init events differ |
| 08-led-chaser-595 | not-projectable | compile error: `shiftleft` expression not supported |
| 11-toggle-button | not-projectable | compile error: `btn` pin read expression not supported |
| 18-logic-and-gate | not-projectable | compile error: `btnA` pin read expression not supported |
| 19-logic-or-gate | not-projectable | compile error: `btnA` pin read expression not supported |

**Summary:** 6 compiled, all 6 agree on observable pin behaviour. 4 not projectable
(pseudocode compiler doesn't support their expression syntax yet). The timing drift
(~250µs at 1.5s) is consistent with previously documented interleaving differences
between the two emulators and is NOT a content disagreement.

## Analog examples (blocked)

| Example | Status | Detail |
|---------|--------|--------|
| 02-dimmer | not-projectable | compile error: `read pot` expression |
| 03-night-light | not-projectable | compile error: `read ldr` expression |
| 04-thermostat | not-projectable | compile error: `read sensor` expression |
| 05-counter-7seg | not-projectable | compile error: `read button` expression |
| 15-voltage-divider | not-projectable | ANALOG pin |
| 16-ldr-bargraph | not-projectable | ANALOG pin |
| 17-comparator | not-projectable | ANALOG pin |

Blocked on the pseudocode compiler supporting analog pin read expressions.

## Batch 2 (2026-08-15)

| Example | Status | Detail |
|---------|--------|--------|
| 20-shift-register-binary | not-projectable | compile: `shiftleft` expression |
| 24-pwm-fade | emu-only | ucsim timeout (PWM rapid toggling, too slow) |
| 25-reaction-timer | not-projectable | compile: `btn` pin read |
| 26-debounce | not-projectable | compile: `btn` pin read |
| 27-led-dice | not-projectable | compile: `btn` pin read |
| 30-multi-led-pattern | agree | extra emu events are init mode-set (PP H); LED sequence matches |
| 32-source-vs-sink | inconclusive | ucsim produces no output (timeout); emu shows correct pattern |
| 33-inductive-no-flyback | agree | state sequence matches (emu=4, ucsim=2 events) |
| 46-port-overcurrent | agree | extra emu events are init PP-H; L transitions match at same pins |
| 53-servo-sweep | not-projectable | compile: servo API not supported |
| 54-motor-driver | not-projectable | compile: motor API not supported |

**Summary batch 2:** 3 agree (after investigation: init mode-set events),
1 inconclusive (ucsim timeout), 1 emu-only (timeout), 6 not-projectable.

**Note on init mode-set events:** emu8051 reports a PIN event when PxM0/PxM1
is written (mode change from quasi to push-pull), producing a PP-H event at
init time. ucsim does not report mode-set as a pin event. This is a trace
format difference, not a program behaviour disagreement. The actual pin-value
transitions (L events where LEDs turn on) match between both emulators.

## Running totals

| Status | Count |
|--------|-------|
| agree | 9 |
| inconclusive (ucsim timeout) | 1 |
| emu-only (ucsim timeout) | 1 |
| not-projectable (compiler) | 10 |
| analog (blocked) | 7 |
