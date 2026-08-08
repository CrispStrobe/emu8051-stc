/**
 * test_conformance.mjs — run bw-board's boundary-A conformance suite
 * against our WASM build + adapter.
 *
 * Usage: node test_conformance.mjs
 */

import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Load our WASM module
const createEmu8051 = (await import(join(__dirname, 'build', 'emu8051.js'))).default;
const Module = await createEmu8051();

// Load the adapter and conformance kit from bw-board
const { createEmu8051Adapter } = await import(
  '/mnt/volume1/code/bw-board/src/emu8051-adapter.js'
);
const { runConformance, formatReport } = await import(
  '/mnt/volume1/code/bw-board/src/conformance.js'
);

// Create the adapter in poll mode (push mode requires careful setup)
const adapter = createEmu8051Adapter(Module, {
  fosc: 11059200,
  vcc: 5.0,
  mode: 'poll',
  ports: [0, 1, 2, 3],
});

// Run the conformance suite
console.log('Running boundary-A conformance suite...\n');
const results = runConformance(adapter);
console.log(formatReport(results));

// Exit with failure if any required checks failed
const requiredFails = results.filter(r => !r.pass && r.severity === 'required');
process.exit(requiredFails.length > 0 ? 1 : 0);
