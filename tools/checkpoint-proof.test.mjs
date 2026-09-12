// Synthetic adapters test the TEST RUNNER, not the emulator or native codec.
import test from 'node:test';
import assert from 'node:assert/strict';
import { prove, groups, corruptionKinds } from './checkpoint-proof.mjs';
import { bindCheckpoint, status } from './checkpoint-wasm-binding.mjs';

function synthetic(fault) {
  return {
    contractVersion: 1, identity: { schema: 1, build: 123 },
    corrupt(blob) {
      return corruptionKinds.map((kind, i) => {
        const copy = blob.slice(); copy[i] ^= 0xff;
        return { kind, blob: copy };
      });
    },
    scenarios: [{
      name: 'synthetic-three-phase', expectedPhases: ['0', '1', '2'],
      points: fault === 'missing-phase' ? 2 : 3,
      continuation: [1, 2, 3], advance: 1,
      async create() {
        let value = 0, saves = 0, restores = 0, latent = 0;
        const encode = () => Uint8Array.of(0x81, 1, 123, 0, value);
        return {
          identity: () => ({ schema: 1, build: fault === 'identity' ? 124 : 123 }),
          observe() {
            return { phase: String(value % 3), state: Object.fromEntries(groups.map(g =>
              [g, fault === 'vacuous-time' && g === 'time' ? 0 : value])) };
          },
          save() {
            const b = encode();
            if (fault === 'nondeterministic-save') b[3] = saves++;
            return b;
          },
          restore(blob) {
            const valid = blob.length === 5 && blob[0] === 0x81 && blob[1] === 1 &&
              blob[2] === 123 && blob[3] === 0;
            if (!valid) {
              if (fault === 'accept-corruption') return true;
              if (fault === 'refusal-mutates') value++;
              if (fault === 'refusal-latent') latent++;
              return false;
            }
            restores++;
            value = blob[4] + (fault === 'roundtrip' ? 1 : 0);
            if (fault === 'restore-twice' && restores === 2) value++;
            if (fault === 'continuation') latent++;
            return true;
          },
          apply(n) { value += n; return { pin: value, uart: value + latent }; },
        };
      },
    }],
  };
}

test('synthetic clean adapter passes full runner mechanics', async () => {
  const result = await prove(synthetic());
  assert.equal(result.receipts.length, 3);
  assert.equal(result.receipts[0].refusals, 11);
});
for (const [fault, expected] of Object.entries({
  identity: /runtime schema\/build mismatch/,
  'nondeterministic-save': /save must be deterministic/,
  roundtrip: /round-trip bytes/,
  'restore-twice': /restore-twice bytes/,
  continuation: /continuation state\/events\/output diverged/,
  'accept-corruption': /accepted truncated/,
  'refusal-mutates': /refusal mutated bytes/,
  'refusal-latent': /refusal changed continuation/,
  'missing-phase': /incomplete phase census/,
  'vacuous-time': /vacuous coverage: time/,
})) test(`harness mutation fired: ${fault}`, async () => {
  await assert.rejects(() => prove(synthetic(fault)), expected);
});

test('ABI binding refuses missing exports', () => {
  assert.throws(() => bindCheckpoint({}), /CHECKPOINT_ABI_UNAVAILABLE/);
});
test('ABI binding owns bytes across heap growth, frees buffers, and retains raw codes', () => {
  let frees = 0;
  const module = {
    HEAPU8: new Uint8Array(64), _malloc: () => 8, _free: () => frees++,
    _emu_checkpoint_version: () => 1, _emu_checkpoint_build_id: () => 123,
    _emu_checkpoint_size: () => 4,
    _emu_checkpoint_save(ptr) {
      module.HEAPU8 = new Uint8Array(128);
      module.HEAPU8.set([1, 2, 3, 4], ptr); return 0;
    },
    _emu_checkpoint_restore: () => status.BAD_BUILD,
  };
  const abi = bindCheckpoint(module);
  const saved = abi.save();
  module.HEAPU8.fill(0);
  assert.deepEqual([...saved], [1, 2, 3, 4]);
  assert.equal(abi.restoreRaw(saved), -6);
  assert.equal(abi.restore(saved), false);
  assert.equal(frees, 3);
});
