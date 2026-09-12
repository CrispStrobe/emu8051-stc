import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { identity } from './checkpoint-layout.mjs';
const receipt = JSON.parse(readFileSync(process.argv[2], 'utf8'));
assert.deepEqual(receipt.identity, identity);
assert.ok(receipt.receipts.length >= 190, 'missing real checkpoint receipts');
assert.equal(new Set(receipt.receipts.map(r => r.scenario)).size, 8);
for (const r of receipt.receipts) {
  assert.ok(r.refusals >= 40, 'empty adversarial run');
  for (const kind of ['bad-header', 'bad-version', 'bad-build', 'invalid-state',
    'boolean:mode', 'invariant:serialIndex', 'adc-range', 'negative-step-count'])
    assert.ok(r.mutationKinds.includes(kind), `mutation did not fire: ${kind}`);
}
console.log(`REAL_CHECKPOINT_PROOF: ${receipt.receipts.length} checkpoints, ` +
  `${receipt.receipts.reduce((n, r) => n + r.refusals, 0)} exact-status refusals, 8 scenarios`);
