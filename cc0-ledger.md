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
| 30-multi-led-pattern | disagree | emu=23 ucsim=1 pin events; ucsim may not trace multi-pin init |
| 32-source-vs-sink | disagree | emu=5 pin events, ucsim=0; ucsim produces no pin output for this program |
| 33-inductive-no-flyback | agree | state sequence matches (emu=4, ucsim=2 events) |
| 46-port-overcurrent | disagree | emu=24 ucsim=8; event counts differ, needs investigation |
| 53-servo-sweep | not-projectable | compile: servo API not supported |
| 54-motor-driver | not-projectable | compile: motor API not supported |

**Summary batch 2:** 1 agree, 3 disagree (need investigation), 1 emu-only (timeout), 6 not-projectable.
The disagrees may be init mode-set differences; needs deeper trace comparison.

## Running totals

| Status | Count |
|--------|-------|
| agree | 7 |
| disagree (under investigation) | 3 |
| emu-only (ucsim timeout) | 1 |
| not-projectable (compiler) | 10 |
| analog (blocked) | 7 |
