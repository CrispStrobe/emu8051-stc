// The ABI names and codes are taken from bwcx's checkpoint v1 interface.
import assert from 'node:assert/strict';

export const status = Object.freeze({
  OK: 0, NOT_INITIALIZED: -1, NULL_BUFFER: -2, WRONG_LENGTH: -3,
  BAD_HEADER: -4, BAD_VERSION: -5, BAD_BUILD: -6, INVALID_STATE: -7,
  ALLOCATION_FAILURE: -8,
});
export const checkpointExports = Object.freeze([
  'emu_checkpoint_version', 'emu_checkpoint_build_id', 'emu_checkpoint_size',
  'emu_checkpoint_save', 'emu_checkpoint_restore',
]);

export function bindCheckpoint(module) {
  for (const name of [...checkpointExports, 'malloc', 'free'])
    assert.equal(typeof module[`_${name}`], 'function', `CHECKPOINT_ABI_UNAVAILABLE: ${name}`);
  assert.equal(module._emu_checkpoint_version(), 1, 'checkpoint ABI version');
  const size = module._emu_checkpoint_size() >>> 0;
  assert.ok(size > 0, 'nonzero fixed checkpoint size');
  const allocate = length => {
    const ptr = module._malloc(Math.max(1, length));
    assert.ok(ptr, 'test buffer allocation failed');
    return ptr;
  };
  return {
    identity: { schema: 1, build: module._emu_checkpoint_build_id() >>> 0 },
    size,
    save() {
      const ptr = allocate(size);
      try {
        assert.equal(module._emu_checkpoint_save(ptr, size), status.OK, 'checkpoint save status');
        // Reacquire HEAPU8 after native calls, which may grow memory.
        return Uint8Array.from(module.HEAPU8.subarray(ptr, ptr + size));
      } finally { module._free(ptr); }
    },
    restoreRaw(blob) {
      assert.ok(blob instanceof Uint8Array);
      const ptr = allocate(blob.length);
      try {
        module.HEAPU8.set(blob, ptr);
        return module._emu_checkpoint_restore(ptr, blob.length);
      } finally { module._free(ptr); }
    },
    restore(blob) { return this.restoreRaw(blob) === status.OK; },
    nullRestore() { return module._emu_checkpoint_restore(0, size); },
    nullSave() { return module._emu_checkpoint_save(0, size); },
  };
}
