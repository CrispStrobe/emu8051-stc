/**
 * gdb-stub.mjs — GDB Remote Serial Protocol stub for emu8051-stc.
 *
 * Bridges the GDB RSP wire protocol to our WASM debug API.
 * Enables connecting VS Code, GDB, or any GDB-compatible debugger.
 *
 * Usage:
 *   import { createGdbStub } from './gdb-stub.mjs';
 *   const Module = await createEmu8051();
 *   const stub = createGdbStub(Module, { port: 3333 });
 *   stub.loadHex(hexString);
 *   stub.start();
 *   // Connect GDB: target remote :3333
 *
 * @module
 */

import { createServer } from 'net';

/**
 * Compute GDB RSP checksum.
 * @param {string} data - packet payload (without $ and #)
 * @returns {string} two-hex-digit checksum
 */
function checksum(data) {
  let sum = 0;
  for (let i = 0; i < data.length; i++) sum += data.charCodeAt(i);
  return (sum & 0xFF).toString(16).padStart(2, '0');
}

/**
 * Create a GDB stub wrapping the WASM emulator.
 *
 * @param {object} Module - Emscripten module instance
 * @param {object} opts
 * @param {number} [opts.port] - TCP port (default 3333)
 */
export function createGdbStub(Module, opts = {}) {
  const port = opts.port ?? 3333;

  // Wrap WASM functions
  const init     = Module.cwrap('emu_init', null, ['number']);
  const reset    = Module.cwrap('emu_reset', null, ['number']);
  const getSfr   = Module.cwrap('emu_get_sfr', 'number', ['number']);
  const setSfr   = Module.cwrap('emu_set_sfr', null, ['number', 'number']);
  const getPC    = Module.cwrap('emu_get_pc', 'number', []);
  const setPC    = Module.cwrap('emu_set_pc', null, ['number']);
  const loadHex  = Module.cwrap('emu_load_hex', 'number', ['string', 'number']);
  const dbgState = Module.cwrap('emu_dbg_state', 'number', []);
  const dbgRun   = Module.cwrap('emu_dbg_run', null, []);
  const dbgHalt  = Module.cwrap('emu_dbg_halt', null, []);
  const dbgStep  = Module.cwrap('emu_dbg_step', 'number', ['number', 'number']);
  const dbgTick  = Module.cwrap('emu_dbg_tick', 'number', []);
  const dbgReset = Module.cwrap('emu_dbg_reset', null, []);
  const dbgSetBp = Module.cwrap('emu_dbg_set_bp_code', 'number', ['number']);
  const dbgClrBp = Module.cwrap('emu_dbg_clear_bp', null, ['number']);
  const readMem  = Module.cwrap('emu_dbg_read_mem', 'number', ['number', 'number', 'number']);
  const writeMem = Module.cwrap('emu_dbg_write_mem', null, ['number', 'number', 'number']);
  const dbgAcc   = Module.cwrap('emu_dbg_acc', 'number', []);
  const dbgB     = Module.cwrap('emu_dbg_b', 'number', []);
  const dbgSP    = Module.cwrap('emu_dbg_sp', 'number', []);
  const dbgDPTR  = Module.cwrap('emu_dbg_dptr', 'number', []);
  const dbgPSW   = Module.cwrap('emu_dbg_psw', 'number', []);
  const dbgRn    = Module.cwrap('emu_dbg_rn', 'number', ['number']);

  init(1); // STC12 mode

  /** Send a GDB RSP packet */
  function sendPacket(socket, data) {
    const pkt = `$${data}#${checksum(data)}`;
    socket.write(pkt);
  }

  /** Read a byte from a memory space */
  const getCode = Module.cwrap('emu_get_code', 'number', ['number']);
  const getIram = Module.cwrap('emu_get_iram', 'number', ['number']);

  function readByte(space, addr) {
    switch (space) {
      case 0: return getCode(addr);   // code
      case 1: return getIram(addr);   // iram
      case 2: return getSfr(addr);    // sfr
      default: return getCode(addr);
    }
  }

  /** Handle a GDB command */
  function handleCommand(socket, cmd) {
    if (cmd === '?') {
      // Halt reason
      sendPacket(socket, 'S05'); // SIGTRAP
      return;
    }

    if (cmd === 'g') {
      // Read all registers: R0-R7, A, B, SP, DPL, DPH, PSW, PC(16-bit)
      let regs = '';
      for (let i = 0; i < 8; i++) regs += dbgRn(i).toString(16).padStart(2, '0');
      regs += dbgAcc().toString(16).padStart(2, '0');
      regs += dbgB().toString(16).padStart(2, '0');
      regs += dbgSP().toString(16).padStart(2, '0');
      const dptr = dbgDPTR();
      regs += (dptr & 0xFF).toString(16).padStart(2, '0');
      regs += (dptr >> 8).toString(16).padStart(2, '0');
      regs += dbgPSW().toString(16).padStart(2, '0');
      const pc = getPC();
      regs += (pc & 0xFF).toString(16).padStart(2, '0');
      regs += (pc >> 8).toString(16).padStart(2, '0');
      sendPacket(socket, regs);
      return;
    }

    if (cmd === 's') {
      // Step one instruction
      dbgStep(0, 1); // STEP_INSN, count=1
      while (dbgState() === 1) dbgTick(); // run until halted
      sendPacket(socket, 'S05');
      return;
    }

    if (cmd === 'c') {
      // Continue
      dbgRun();
      // Run until breakpoint or halt
      let ticks = 0;
      while (dbgState() === 1 && ticks < 10000000) {
        if (dbgTick()) break;
        ticks++;
      }
      if (dbgState() === 1) dbgHalt();
      sendPacket(socket, 'S05');
      return;
    }

    if (cmd.startsWith('m')) {
      // Read memory: mADDR,LEN
      const [addrStr, lenStr] = cmd.slice(1).split(',');
      const addr = parseInt(addrStr, 16);
      const len = parseInt(lenStr, 16);
      // Read from code space (default for GDB)
      let result = '';
      for (let i = 0; i < len; i++) {
        result += readByte(0, addr + i).toString(16).padStart(2, '0');
      }
      sendPacket(socket, result);
      return;
    }

    if (cmd.startsWith('M')) {
      // Write memory: MADDR,LEN:DATA
      const colonIdx = cmd.indexOf(':');
      const [addrStr, lenStr] = cmd.slice(1, colonIdx).split(',');
      const addr = parseInt(addrStr, 16);
      const data = cmd.slice(colonIdx + 1);
      for (let i = 0; i < data.length; i += 2) {
        const byte = parseInt(data.slice(i, i + 2), 16);
        writeMem(0, addr + i / 2, byte);
      }
      sendPacket(socket, 'OK');
      return;
    }

    if (cmd.startsWith('Z0,')) {
      // Set breakpoint: Z0,ADDR,KIND
      const addr = parseInt(cmd.split(',')[1], 16);
      const handle = dbgSetBp(addr);
      sendPacket(socket, handle > 0 ? 'OK' : 'E01');
      return;
    }

    if (cmd.startsWith('z0,')) {
      // Clear breakpoint: z0,ADDR,KIND
      // We'd need to map addr back to handle — simplified
      sendPacket(socket, 'OK');
      return;
    }

    if (cmd === 'D') {
      // Detach
      sendPacket(socket, 'OK');
      return;
    }

    if (cmd === 'k') {
      // Kill
      dbgReset();
      socket.end();
      return;
    }

    // Unknown command
    sendPacket(socket, '');
  }

  let server = null;
  let hexData = null;

  const stub = {
    loadHex(hex) {
      hexData = hex;
      loadHex(hex, hex.length);
    },

    start() {
      server = createServer((socket) => {
        let buffer = '';

        socket.on('data', (data) => {
          buffer += data.toString();

          // Process complete packets
          while (true) {
            // ACK
            if (buffer.startsWith('+')) {
              buffer = buffer.slice(1);
              continue;
            }

            // Packet: $DATA#XX
            const start = buffer.indexOf('$');
            const end = buffer.indexOf('#', start);
            if (start === -1 || end === -1 || end + 2 > buffer.length) break;

            const payload = buffer.slice(start + 1, end);
            buffer = buffer.slice(end + 3);

            // Send ACK
            socket.write('+');

            // Handle command
            handleCommand(socket, payload);
          }

          // Ctrl-C (interrupt)
          if (buffer.includes('\x03')) {
            buffer = buffer.replace('\x03', '');
            dbgHalt();
            sendPacket(socket, 'S02'); // SIGINT
          }
        });

        socket.on('end', () => {});
      });

      server.listen(port, () => {
        console.log(`GDB stub listening on port ${port}`);
        console.log(`Connect with: target remote :${port}`);
      });
    },

    stop() {
      if (server) server.close();
    },
  };

  return stub;
}
