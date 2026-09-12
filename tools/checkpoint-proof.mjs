// Independent orchestration only: the native ABI binding belongs to integration.
import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

export const groups = Object.freeze([
  'cpu', 'timers', 'interrupts', 'uartInput', 'uartOutput', 'time',
  'inputs', 'pins', 'history',
]);
export const corruptionKinds = Object.freeze([
  'bad-version', 'bad-header', 'bad-build', 'invalid-state',
]);
const clone = value => structuredClone(value);
const bytes = value => {
  assert.ok(value instanceof Uint8Array && value.length > 0, 'nonempty checkpoint required');
  return Uint8Array.from(value); // Never retain a potentially invalidated WASM heap view.
};
const same = (a, b, label) => assert.deepStrictEqual(a, b, label);

export async function prove(adapter) {
  assert.equal(adapter.contractVersion, 1, 'proof adapter contract version');
  assert.ok(adapter.identity && Object.hasOwn(adapter.identity, 'schema') &&
    Object.hasOwn(adapter.identity, 'build'), 'exact identity required');
  assert.ok(adapter.scenarios?.length, 'scenarios required; no empty green run');
  const receipts = [];
  const coverage = new Map(groups.map(g => [g, new Set()]));
  for (const scenario of adapter.scenarios) {
    assert.ok(scenario.expectedPhases?.length, 'explicit reachable phase census required');
    assert.equal(new Set(scenario.expectedPhases).size, scenario.expectedPhases.length);
    assert.ok(Number.isSafeInteger(scenario.points) && scenario.points > 0);
    assert.ok(scenario.continuation?.length, 'nonempty continuation required');
    const machine = await scenario.create();
    const seen = new Set();
    try {
      same(await machine.identity(), adapter.identity, 'runtime schema/build mismatch');
      for (let point = 0; point < scenario.points; point++) {
        const original = clone(await machine.observe());
        const evidence = clone(await machine.checkpointObserved?.(original));
        assert.ok(scenario.expectedPhases.includes(original.phase), 'unexpected phase');
        seen.add(original.phase);
        for (const group of groups) {
          assert.ok(Object.hasOwn(original.state, group), `missing independent probe: ${group}`);
          coverage.get(group).add(JSON.stringify(original.state[group]));
        }
        const checkpoint = bytes(await machine.save());
        same(bytes(await machine.save()), checkpoint, 'save must be deterministic');
        same(await machine.observe(), original, 'save must be observationally pure');
        const run = async () => {
          const trace = [];
          for (const action of scenario.continuation) {
            // apply returns emitted events/output for this action, not an accumulating host log.
            const output = clone(await machine.apply(clone(action)));
            trace.push({ output, observation: clone(await machine.observe()) });
          }
          return { trace, checkpoint: bytes(await machine.save()) };
        };
        const forward = await run();
        assert.equal(await machine.restore(checkpoint.slice()), true, 'valid restore refused');
        same(bytes(await machine.save()), checkpoint, 'round-trip bytes');
        same(await machine.observe(), original, 'round-trip independent observation');
        assert.equal(await machine.restore(checkpoint.slice()), true, 'second restore refused');
        same(bytes(await machine.save()), checkpoint, 'restore-twice bytes');
        same(await machine.observe(), original, 'restore-twice observation');
        same(await run(), forward, 'continuation state/events/output diverged');
        assert.equal(await machine.restore(checkpoint.slice()), true);

        const malformed = [];
        // Exhaustive prefix testing is opt-in: full memory/histogram blobs are large.
        const lengths = adapter.exhaustiveLengths
          ? Array.from({ length: checkpoint.length }, (_, i) => i)
          : [...new Set([0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32,
            checkpoint.length >> 1, checkpoint.length - 1])].filter(n => n < checkpoint.length);
        for (const length of lengths)
          malformed.push({ kind: `truncated:${length}`, blob: checkpoint.slice(0, length), expectedCode: -3 });
        for (const extra of [1, 16]) {
          const blob = new Uint8Array(checkpoint.length + extra);
          blob.set(checkpoint);
          malformed.push({ kind: `oversized:${extra}`, blob, expectedCode: -3 });
        }
        const corruptions = await adapter.corrupt(checkpoint.slice());
        for (const kind of corruptionKinds)
          assert.ok(corruptions.some(m => m.kind === kind), `missing corruption: ${kind}`);
        malformed.push(...corruptions);
        for (const { kind, blob, expectedCode } of malformed) {
          assert.ok(blob instanceof Uint8Array, `invalid mutation ${kind}`);
          assert.notDeepStrictEqual(blob, checkpoint, `ineffective mutation ${kind}`);
          const before = bytes(await machine.save());
          const observation = clone(await machine.observe());
          if (machine.restoreRaw) {
            assert.ok(Number.isInteger(expectedCode) && expectedCode < 0, `missing expected status: ${kind}`);
            assert.equal(await machine.restoreRaw(blob.slice()), expectedCode, `wrong refusal status: ${kind}`);
          } else assert.equal(await machine.restore(blob.slice()), false, `accepted ${kind}`);
          same(bytes(await machine.save()), before, `refusal mutated bytes: ${kind}`);
          same(await machine.observe(), observation, `refusal mutated observation: ${kind}`);
        }
        // Also catch hidden corruption omitted by save/observe via replay after refusals.
        same(await run(), forward, 'refusal changed continuation');
        assert.equal(await machine.restore(checkpoint.slice()), true);
        await machine.apply(clone(scenario.advance));
        receipts.push({ scenario: scenario.name, point, phase: original.phase,
          refusals: malformed.length, mutationKinds: malformed.map(m => m.kind), evidence });
      }
      same([...seen].sort(), [...scenario.expectedPhases].sort(), `incomplete phase census: ${scenario.name}`);
      await machine.verifyCoverage?.();
    } finally {
      await machine.dispose?.();
    }
  }
  for (const [group, values] of coverage)
    assert.ok(values.size > 1, `vacuous coverage: ${group} never changed`);
  return { identity: adapter.identity, receipts,
    distinctObservations: Object.fromEntries([...coverage].map(([k, v]) => [k, v.size])) };
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    assert.ok(process.argv[2], 'Native adapter required: node tools/checkpoint-proof.mjs /path/to/adapter.mjs');
    const { default: adapter } = await import(pathToFileURL(resolve(process.argv[2])).href);
    console.log(JSON.stringify(await prove(adapter), null, 2));
  } catch (error) {
    console.error(`CHECKPOINT_PROOF_FAILED: ${error.stack}`);
    process.exitCode = 1;
  }
}
