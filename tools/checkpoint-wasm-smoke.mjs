// Native ABI boundary smoke ONLY; not the full phase/peripheral qualification.
import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';
import { bindCheckpoint, status } from './checkpoint-wasm-binding.mjs';

try {
  const artifact = resolve(process.argv[2] ?? 'build/emu8051.js');
  const { default: createModule } = await import(pathToFileURL(artifact).href);
  const module = await createModule();
  const abi = bindCheckpoint(module);
  // Non-null and correctly sized buffer isolates NOT_INITIALIZED precedence.
  assert.equal(abi.restoreRaw(new Uint8Array(abi.size)), status.NOT_INITIALIZED);
  module._emu_init(1);
  const original = abi.save();
  assert.deepEqual(abi.save(), original, 'deterministic save');
  const refusals = [];
  const refuse = (name, call, expected) => {
    const before = abi.save();
    assert.equal(call(), expected, name);
    assert.deepEqual(abi.save(), before, `refusal atomicity: ${name}`);
    refusals.push(name);
  };
  refuse('null restore', () => abi.nullRestore(), status.NULL_BUFFER);
  refuse('null save', () => abi.nullSave(), status.NULL_BUFFER);
  for (const n of [...new Set([0, 1, 4, 8, 16, abi.size >> 1, abi.size - 1])]) {
    if (n >= abi.size) continue;
    refuse(`truncated:${n}`, () => abi.restoreRaw(original.slice(0, n)), status.WRONG_LENGTH);
  }
  for (const n of [1, 16]) {
    const longer = new Uint8Array(abi.size + n);
    longer.set(original);
    refuse(`oversized:${n}`, () => abi.restoreRaw(longer), status.WRONG_LENGTH);
  }
  for (let i = 0; i < 2; i++) {
    module._emu_set_iram(0x30, 0x71 + i);
    module._emu_tick();
    assert.equal(abi.restoreRaw(original), status.OK);
    assert.deepEqual(abi.save(), original, 'restore bytes');
    assert.equal(abi.restoreRaw(original), status.OK);
    assert.deepEqual(abi.save(), original, 'restore-twice bytes');
  }
  console.log(JSON.stringify({ scope: 'ABI boundary smoke only', artifact,
    identity: abi.identity, size: abi.size, refusals,
    pending: ['header/state mutations', 'phase census', 'peripheral continuation'] }, null, 2));
} catch (error) {
  console.error(`CHECKPOINT_SMOKE_FAILED: ${error.stack}`);
  process.exitCode = 1;
}
