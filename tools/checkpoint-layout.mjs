// Explicit layout derived from the frozen a519b0d codec, not host C offsetof.
import assert from 'node:assert/strict';
export const identity = Object.freeze({ schema: 1, build: 0x80510101 });
let cursor = 20;
const offsets = {};
const field = (name, width) => { offsets[name] = cursor; cursor += width; };
field('initialized', 1); field('idle', 1);
field('codeMax', 2); field('xramMax', 2);
field('code', 65536); field('xram', 65536);
field('iram', 128); field('upper', 128); field('sfr', 128);
field('pc', 2); field('delay', 1); field('scale', 1); field('interruptActive', 1);
field('interruptSaved', 6); field('serialBytes', 18); field('serialIndex', 1);
field('serialBits', 1); field('serialPending', 1); field('skipTimers', 1);
field('timer0', 1); field('timer1', 1); field('brt', 1); field('adcCountdown', 2);
field('adcInputs', 16); field('dptr', 3); field('watchdog', 4); field('watchdogPrescaler', 1);
field('pca', 1); field('pcaPending', 1); field('cex', 3); field('tf1', 1);
field('scaled', 1); field('portExt', 6); field('mode', 1); field('fosc', 4); field('vcc', 8);
field('hasHistory', 1); field('historyHead', 4); field('historyCount', 4); field('history', 4096 * 12);
field('part', 1); field('unmodelled', 4); field('clocks', 8); field('idleClocks', 8);
field('quantum', 8); field('shadows', 18);
field('dbgState', 1); field('nextBp', 4); field('bps', 32 * 14);
field('bwMs', 2); field('taskCount', 1); field('tasks', 32);
field('stepKind', 1); field('stepCount', 4); field('stepSp', 1);
field('haltCause', 1); field('haltPc', 2); field('haltBp', 4); field('haltTime', 8);
field('isWatch', 1); field('watchSpace', 1); field('watchAddr', 2); field('watchValues', 2);
field('watchShadow', 32); field('taskState', 8); field('profiling', 1);
field('hasHistogram', 1); field('profileTotal', 4); field('histogram', 65536 * 4);
export const layout = Object.freeze({ ...offsets, size: cursor });
export function rechecksum(blob) {
  let hash = 2166136261;
  for (let i = 20; i < blob.length; i++) hash = Math.imul(hash ^ blob[i], 16777619) >>> 0;
  new DataView(blob.buffer, blob.byteOffset, blob.byteLength).setUint32(16, hash, true);
  return blob;
}
export function corruptions(original) {
  assert.equal(original.length, layout.size, 'frozen v1 layout size');
  const mutate = (kind, offset, value, expectedCode, width = 1, checksum = true) => {
    const blob = original.slice();
    const view = new DataView(blob.buffer);
    if (width === 4) view.setUint32(offset, value, true);
    else if (width === 2) view.setUint16(offset, value, true);
    else blob[offset] = value;
    if (checksum) rechecksum(blob);
    return { kind, blob, expectedCode };
  };
  return [
    mutate('bad-header', 0, 0, -4), mutate('bad-version', 4, 2, -5, 4),
    mutate('bad-build', 8, 0x80510102, -6, 4), mutate('header-size', 12, 1, -4, 4),
    mutate('bad-checksum', 16, original[16] ^ 1, -4, 1, false),
    mutate('invalid-state', layout.scale, 2, -7),
    ...['initialized', 'idle', 'serialPending', 'skipTimers', 'pcaPending', 'scaled',
      'mode', 'hasHistory', 'isWatch', 'profiling', 'hasHistogram'].map(k => mutate(`boolean:${k}`, layout[k], 2, -4)),
    ...[['codeMax', 0], ['serialIndex', 18], ['serialBits', 11], ['interruptActive', 4],
      ['timer0', 12], ['timer1', 12], ['brt', 12], ['pca', 12], ['part', 5],
      ['dbgState', 2], ['taskCount', 9], ['stepKind', 6], ['haltCause', 5]].map(([k,v]) => mutate(`invariant:${k}`, layout[k], v, -7)),
    mutate('adc-range', layout.adcInputs, 1024, -7, 2),
    mutate('negative-step-count', layout.stepCount, 0xffffffff, -7, 4),
    mutate('negative-next-bp', layout.nextBp, 0xffffffff, -7, 4),
    ...(original[layout.hasHistory] ? [mutate('history-port', layout.history + 8, 6, -7),
      mutate('history-count', layout.historyCount,
        (new DataView(original.buffer, original.byteOffset).getUint32(layout.historyHead, true) + 1) >>> 0, -7, 4)] : []),
  ];
}
