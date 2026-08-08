/**
 * load-symbols.mjs — load a spec-updates/004 symbol table JSON
 * into the emu8051 WASM debug API.
 *
 * Usage:
 *   import { loadSymbols } from './load-symbols.mjs';
 *   const Module = await createEmu8051();
 *   loadSymbols(Module, symbolsJson);
 *
 * @param {object} Module - the Emscripten module instance
 * @param {object} json - parsed JSON from stc_symtab.py
 */
export function loadSymbols(Module, json) {
  const setBwMs = Module.cwrap('emu_dbg_set_bw_ms_addr', null, ['number']);
  const setTask = Module.cwrap('emu_dbg_set_task', null, ['number', 'number', 'number']);
  const setBpYield = Module.cwrap('emu_dbg_set_bp_yield', 'number',
    ['number', 'number', 'number']);

  const sched = json.scheduler;
  if (!sched) return { tasks: 0, yields: 0 };

  // Set bw_ms address
  if (sched.bw_ms) {
    setBwMs(sched.bw_ms.addr);
  }

  // Set task addresses
  let totalYields = 0;
  if (sched.tasks) {
    for (let i = 0; i < sched.tasks.length && i < 8; i++) {
      const task = sched.tasks[i];
      const stateAddr = task.state ? task.state.addr : 0;
      const untilAddr = task.until ? task.until.addr : 0;
      setTask(i, stateAddr, untilAddr);

      // Register yield breakpoints if requested
      if (task.yields) {
        totalYields += task.yields.length;
      }
    }
  }

  return {
    tasks: sched.tasks ? sched.tasks.length : 0,
    yields: totalYields,
    bwMsAddr: sched.bw_ms ? sched.bw_ms.addr : 0,
  };
}

/**
 * Set yield breakpoints from the symbol table.
 * Returns an array of breakpoint handles.
 */
export function setYieldBreakpoints(Module, json) {
  const setBp = Module.cwrap('emu_dbg_set_bp_yield', 'number',
    ['number', 'number', 'number']);
  const handles = [];

  const sched = json.scheduler;
  if (!sched || !sched.tasks) return handles;

  for (let i = 0; i < sched.tasks.length; i++) {
    const task = sched.tasks[i];
    if (!task.yields) continue;
    for (const y of task.yields) {
      const h = setBp(y.addr, i, y.state);
      if (h > 0) handles.push({ handle: h, task: i, state: y.state, addr: y.addr });
    }
  }

  return handles;
}
