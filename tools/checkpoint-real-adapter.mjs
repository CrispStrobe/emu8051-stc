import assert from 'node:assert/strict';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { bindCheckpoint } from './checkpoint-wasm-binding.mjs';
import { identity, layout, corruptions } from './checkpoint-layout.mjs';

const artifact = resolve(process.env.CHECKPOINT_PROBE_WASM ?? 'build-proof/emu8051.js');
const { default: createModule } = await import(pathToFileURL(artifact).href);
const phases = n => Array.from({ length: n }, (_, i) => String(i));
// mul_ab() returns 4 in opcodes.c. tick() preserves that at scale=1;
// at scale=12 it assigns (4 + 1) * 12 - 1 = 59, then counts down to zero.
// These are source-derived expectations, NOT sets learned from the tested run.
const mulPhases = part => phases(part === 2 ? (4 + 1) * 12 : 4 + 1);
// Owned firmware: MUL; toggle P1; transmit both UARTs; increment RAM; loop.
const program = [0xa4, 0x63, 0x90, 0xff, 0x75, 0x99, 0x41,
  0x75, 0x9b, 0x42, 0x05, 0x30, 0x80, 0xf2];
const continuation = [{ ticks: 5, input: 0x31 }, { ticks: 17, input: 0xa6 },
  { ticks: 53, input: 0x59 }];

function scenario({ name, part = 0, classic = false, irq = false, warm = 0, activeState = '',
  history = false, points = 6, expectedPhases = phases(5) }) {
  // In the IRQ fixture T0's ISR generates the high-priority serial interrupt.
  // RX on EVERY single-tick advance would instead starve the foreground in the
  // high ISR. Future RX remains exercised by the longer continuation actions.
  return { name, points, expectedPhases, continuation,
    advance: irq || activeState === 'idle' ? { ticks: 1 } : { ticks: 1, input: 0x5a },
    async create() {
      // A fresh module avoids cross-scenario optional allocations/callback state.
      const m = await createModule();
      const abi = bindCheckpoint(m);
      assert.equal(abi.size, layout.size);
      assert.equal(typeof m._proof_observe, 'function', 'test-only live probe required');
      const events = [];
      const callback = (kind, sig) => m.addFunction((...args) => events.push([kind, ...args]), sig);
      const pin = callback('pin', 'viiiii');
      const serial = callback('uart1', 'vii');
      const serial2 = callback('uart2', 'vii');
      const halt = m.addFunction(() => events.push(['halt', m._emu_dbg_halt_cause(),
        m._emu_dbg_halt_bp(), m._emu_dbg_halt_is_watch(), m._emu_dbg_halt_watch_addr(),
        m._emu_dbg_halt_watch_value(), m._emu_dbg_halt_watch_prev()]), 'vii');
      m._emu_init(classic ? 0 : 1);
      if (!classic) m._emu_set_part(part);
      m._emu_set_board_callbacks(pin, 0, 0, 0, 0);
      m._emu_set_serial_callback(serial); m._emu_set_serial2_callback(serial2);
      m._emu_dbg_set_on_halt(halt, 0);
      if (history) m._emu_pin_history_enable();
      m._emu_dbg_profile_start();
      const code = (addr, bytes) => bytes.forEach((b, i) => m._emu_dbg_write_mem(0, addr + i, b));
      code(0x100, program);
      code(0x0b, [0x02, 0x02, 0x00]); // Timer0 -> 0200.
      code(0x23, [0x02, 0x02, 0x20]); // Serial -> 0220.
      code(0x200, [0x75, 0x99, 0x77, 0x00, 0x00, 0x32]); // Trigger high serial within low T0 ISR.
      code(0x220, [0xc2, 0x98, 0xc2, 0x99, 0x32]); // Clear RI/TI; RETI.
      m._emu_set_pc(0x100);
      m._emu_set_sfr(0xe0, 3); m._emu_set_sfr(0xf0, 7);
      m._emu_set_sfr(0x89, 0x22); // T0/T1 auto-reload, independent prescalers.
      m._emu_set_sfr(0x8c, 0xf0); m._emu_set_sfr(0x8a, 0xf0);
      m._emu_set_sfr(0x8d, 0xfa); m._emu_set_sfr(0x8b, 0xfa);
      m._emu_set_sfr(0x88, 0x50); // TR0/TR1.
      m._emu_set_sfr(0x98, 0x50);
      m._emu_set_sfr(0x8e, irq ? 0x80 : 0);
      if (irq) { m._emu_set_sfr(0xa8, 0x92); m._emu_set_sfr(0xb8, 0x10); }
      m._emu_set_iram(0x40, 0x39); m._emu_set_iram(0xc0, 0xa7);
      m._emu_set_xdata(0x4321, 0xc5);
      if (activeState === 'peripheral') {
        code(0x80, [0x75, 0xbc, 0xe8, 0x02, 0x01, 0x00]); // Start 70-clock ADC, jump to foreground.
        m._emu_set_pc(0x80); m._emu_set_sfr(0x9d, 1); // P1.0 analog.
        m._emu_set_sfr(0xc1, 0x20); // Enable watchdog, /2 prescaler.
        m._emu_set_sfr(0xd8, 0x40); m._emu_set_sfr(0xd9, 0); // PCA runs at FOSC/12.
      }
      if (activeState === 'idle') {
        code(0x100, [0x43, 0x87, 1, 0x80, 0xfb]); // IDL; repeat after interrupt return.
        code(0x200, [0x05, 0x30, 0x32]); // Visible wake counter; RETI.
        m._emu_set_sfr(0xa8, 0x82); m._emu_set_sfr(0x8e, 0x80); // T0 only, overflow after 16 clocks.
      }
      if (activeState === 'debug') {
        assert.ok(m._emu_dbg_set_bp_code(0x104) > 0);
        assert.ok(m._emu_dbg_set_bp_write(1, 0x30) > 0);
        m._emu_dbg_set_task(0, 0x30, 0x40);
        m._emu_dbg_run();
      }
      for (let i = 0; i < warm; i++) m._emu_tick();
      events.length = 0;
      const active = new Set(), indices = new Set(), counts = new Set();
      const outputKinds = new Set();
      const checkpointWitness = { idle: false, woke: false, adcActive: false, adcDone: false,
        pca: new Set(), watchdog: new Set(), task: new Set(), watch: false, codeBp: false };
      const observe = () => {
        const o = JSON.parse(m.UTF8ToString(m._proof_observe()));
        // Cross-check public accessors against the direct, read-only C probes.
        assert.equal(o.state.cpu.pc, m._emu_get_pc());
        assert.equal(o.state.uartOutput.index, m._emu_serial_read_idx());
        assert.equal(o.state.history.count, m._emu_pin_history_count());
        const ns = (BigInt(m._emu_get_time_ns_hi() >>> 0) << 32n) | BigInt(m._emu_get_time_ns_lo() >>> 0);
        assert.equal(o.state.time.ns, String(ns));
        active.add(o.state.interrupts.active); indices.add(o.state.uartOutput.index);
        counts.add(o.state.history.count);
        return o;
      };
      const checkpointObserved = o => {
        if (activeState) {
          const w = checkpointWitness;
          w.idle ||= !!(o.state.cpu.sfr[7] & 1);
          w.woke ||= o.state.cpu.iram[0x30] > 0;
          const p = o.state.peripheral;
          w.adcActive ||= p[0] > 0;
          w.adcDone ||= p[0] === 0 && !!(p[1] & 0x10) && (p[2] !== 0 || p[3] !== 0);
          w.pca.add(p[4] + p[5] * 256); w.watchdog.add(p[6]);
          w.task.add(o.state.cpu.iram[0x30]);
          w.watch ||= o.state.debug.halt[3] === 1;
          w.codeBp ||= o.state.debug.halt[0] === 0 && o.state.debug.halt[2] > 0 && o.state.debug.halt[3] === 0;
        }
        return activeState ? { activeState, idle: !!(o.state.cpu.sfr[7] & 1),
          peripheral: o.state.peripheral, task: o.state.cpu.iram[0x30], halt: o.state.debug.halt } : undefined;
      };
      const silent = operation => {
        assert.equal(events.length, 0, 'undrained callback log before checkpoint operation');
        const result = operation();
        assert.deepEqual(events, [], 'checkpoint operation emitted callbacks');
        return result;
      };
      return {
        identity: () => ({ schema: m._emu_checkpoint_version(), build: m._emu_checkpoint_build_id() >>> 0 }),
        save: () => silent(() => abi.save()),
        restore: b => silent(() => abi.restore(b)),
        restoreRaw: b => silent(() => abi.restoreRaw(b)), observe, checkpointObserved,
        apply({ ticks, input }) {
          assert.equal(events.length, 0);
          // Host future-input lists are deliberately replayed, not serialized.
          const clock = m._emu_get_time_ns_lo() >>> 0;
          if (input !== undefined) {
            m._emu_serial_write((input + clock) & 255);
            m._emu_serial2_write((input ^ clock) & 255);
          }
          m._emu_set_port_input(3, clock & 255);
          m._emu_set_pin_input(1, 0, clock & 1);
          m._emu_set_adc_input(0, ((input ?? 0) + clock) & 1023);
          for (let i = 0; i < ticks; i++) {
            if (activeState === 'debug') {
              if (m._emu_dbg_state() === 0) m._emu_dbg_run();
              m._emu_dbg_tick();
            } else m._emu_run(1); // also exercises profiling state across restoration
            active.add(m._emu_get_interrupt_active());
          }
          for (const e of events) outputKinds.add(e[0]);
          return events.splice(0);
        },
        verifyCoverage() {
          if (irq) {
            assert.ok(active.has(1), 'low-priority ISR never active');
            assert.ok(active.has(3), 'nested high-priority ISR never active');
            assert.ok(active.has(0), 'foreground never observed');
          }
          if (!classic && activeState !== 'idle') {
            assert.ok(outputKinds.has('pin') && outputKinds.has('uart1'), 'no real pin/UART1 events');
            // The nested ISR fixture exercises UART1 priority preemption;
            // UART2 transmission is mandatory in the foreground/wrapped fixtures.
            if (part === 0 && !irq) assert.ok(outputKinds.has('uart2'), 'no real UART2 events');
          }
          if (history) assert.ok(counts.size > 1, 'history did not advance');
          if (warm) {
            assert.ok(Math.max(...counts) > 4096, 'history never wrapped');
            assert.ok(indices.size > 1, 'UART ring index never advanced');
          }
          const w = checkpointWitness;
          if (activeState === 'idle') assert.ok(w.idle && w.woke, 'no active idle/wake trajectory');
          if (activeState === 'peripheral') {
            assert.ok(w.adcActive && w.adcDone, 'ADC countdown/result not reached');
            assert.ok(w.pca.size > 1 && w.watchdog.size > 1, 'PCA/watchdog did not progress');
          }
          if (activeState === 'debug') assert.ok(w.watch && w.codeBp && w.task.size > 1,
            'debug breakpoint/watch/task trajectory did not fire');
        },
        dispose() { for (const p of [pin, serial, serial2, halt]) m.removeFunction(p); },
      };
    },
  };
}

export default {
  contractVersion: 1, identity, corrupt: corruptions,
  scenarios: [
    ...[0, 1, 2, 3, 4].map(part => scenario({ name: `part-${part}-delay-census`, part,
      points: part === 2 ? 61 : 6, expectedPhases: mulPhases(part) })),
    scenario({ name: 'classic-delay-census', classic: true }),
    scenario({ name: 'nested-interrupts', irq: true, points: 80, history: true }),
    scenario({ name: 'wrapped-history-uart', warm: 15000, history: true, points: 24,
      expectedPhases: ['1', '2', '3', '4'] }),
    scenario({ name: 'idle-wake', activeState: 'idle', points: 40, expectedPhases: phases(3) }),
    scenario({ name: 'adc-pca-watchdog', activeState: 'peripheral', points: 80 }),
    scenario({ name: 'debug-breakpoint-watch-task', activeState: 'debug', points: 32 }),
  ],
};
