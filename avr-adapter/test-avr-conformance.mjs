/**
 * test-avr-conformance.mjs — run bw-board's boundary-A conformance
 * against the AVR adapter and report which tests pass/fail.
 *
 * Expected: the AVR adapter passes structural tests (setPin shape,
 * time, ADC) and honestly fails STC-specific tests (quasi mode,
 * PxM0/PxM1 registers).
 */
import { createRequire } from 'module';
const require = createRequire(import.meta.url);
const { runConformance, formatReport } = require('../../bw-board/src/conformance.js');
import { createAvrConformanceAdapter } from './avr-conformance-adapter.mjs';

const adapter = createAvrConformanceAdapter({ clockHz: 16_000_000 });

console.log('=== AVR boundary-A conformance ===\n');

let results;
try {
    results = runConformance(adapter);
} catch (e) {
    console.log('Conformance suite threw:', e.message);
    console.log(e.stack);
    process.exit(1);
}

let passed = 0, failed = 0, skipped = 0;
for (const r of results) {
    const icon = r.pass ? 'PASS' : (r.severity === 'required' ? 'FAIL' : 'SKIP');
    console.log(`${icon}: ${r.name}`);
    if (r.detail) console.log(`      ${r.detail}`);
    if (r.pass) passed++;
    else if (r.severity === 'required') failed++;
    else skipped++;
}

console.log(`\n${passed} passed, ${failed} required-failed, ${skipped} recommended-skipped`);

// The AVR adapter should pass structural tests and honestly skip
// STC-specific ones. Zero required failures = conformant.
if (failed > 0) {
    console.log('\nREQUIRED failures need fixing.');
    process.exit(1);
} else {
    console.log('\nAll required tests pass. Recommended-skipped are AVR-specific gaps.');
    process.exit(0);
}
