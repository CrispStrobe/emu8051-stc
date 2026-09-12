// Fast independent reachability evidence using the hosted, read-only-probed
// artifact. No builds, checkpoint corruption loop, or inferred expected sets.
import assert from 'node:assert/strict';
import adapter from './checkpoint-real-adapter.mjs';
const receipts = [];
for (const scenario of adapter.scenarios) {
  const machine = await scenario.create();
  const seen = new Set(), active = new Set();
  try {
    for (let i = 0; i < scenario.points; i++) {
      const o = machine.observe();
      seen.add(o.phase); active.add(o.state.interrupts.active);
      const checkpoint = machine.save();
      for (const action of scenario.continuation) machine.apply(action);
      assert.equal(machine.restore(checkpoint), true);
      machine.apply(scenario.advance);
    }
    assert.deepEqual([...seen].sort(), [...scenario.expectedPhases].sort(), scenario.name);
    if (scenario.name === 'nested-interrupts')
      assert.deepEqual([...active].sort(), [0, 1, 2, 3], 'checkpointed IRQ priority states');
    machine.verifyCoverage();
    receipts.push({ scenario: scenario.name, points: scenario.points,
      expected: scenario.expectedPhases, observed: [...seen].sort((a, b) => a - b),
      checkpointInterruptStates: [...active].sort() });
  } finally { machine.dispose(); }
}
console.log(JSON.stringify({ scope: 'reachability census, not full replay qualification', receipts }, null, 2));
