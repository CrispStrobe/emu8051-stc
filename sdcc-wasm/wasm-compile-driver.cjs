/**
 * wasm-compile-driver.cjs — compile a C file with WASM SDCC (MEMFS).
 *
 * Usage: node wasm-compile-driver.cjs <dist-dir> <installed-dir> <src.c> <out.ihx>
 *
 * SDCC is a multi-process compiler: sdcc spawns sdcpp, sdas8051, and sdld.
 * WebAssembly has no fork/exec, so system() returns ENOSYS.
 * This driver runs each tool as a separate WASM module instance, copying
 * intermediate files between their MEMFS filesystems.
 *
 * Pipeline: sdcpp (preprocess) → sdcc --c1mode (codegen) → sdas8051 (assemble) → sdld (link)
 *
 * All paths inside each VFS are simple absolute paths.
 * 64 MB fixed heap (INITIAL_MEMORY=67108864). No ALLOW_MEMORY_GROWTH.
 */
const { readFileSync, writeFileSync, readdirSync, statSync, existsSync } = require('fs');
const { join, resolve } = require('path');

const distDir = resolve(process.argv[2]);
const installedDir = resolve(process.argv[3]);
const srcFile = resolve(process.argv[4]);
const outFile = resolve(process.argv[5]);

if (!distDir || !installedDir || !srcFile || !outFile) {
  console.error('Usage: node wasm-compile-driver.cjs <dist-dir> <installed-dir> <src.c> <out.ihx>');
  process.exit(1);
}

const incDir = join(installedDir, 'share', 'sdcc', 'include');
const mcs51IncDir = join(incDir, 'mcs51');
// Native sdcc --model-small uses lib/small (not small-stack-auto).
// small-stack-auto is a different library set with different crt0.
const libDir = join(installedDir, 'share', 'sdcc', 'lib', 'small');
const libDirAlt = join(installedDir, 'share', 'sdcc', 'lib', 'small-stack-auto');

// Collect files from a directory
function collectFiles(dir) {
  const result = [];
  try {
    for (const name of readdirSync(dir)) {
      const full = join(dir, name);
      if (statSync(full).isFile()) {
        result.push({ name, data: readFileSync(full) });
      }
    }
  } catch (e) { /* dir may not exist */ }
  return result;
}

const sourceCode = readFileSync(srcFile);
const headerFiles = collectFiles(incDir);
const mcs51Headers = collectFiles(mcs51IncDir);
let libFiles = collectFiles(libDir);
if (libFiles.length === 0) libFiles = collectFiles(libDirAlt);

console.log('WASM SDCC pipeline (MEMFS, no fork)');
console.log(`  Source: ${srcFile} (${sourceCode.length} bytes)`);
console.log(`  Headers: ${headerFiles.length} top-level, ${mcs51Headers.length} mcs51`);
console.log(`  Lib files: ${libFiles.length}`);

// ── Startup checks: all tools must be modularized factory functions ──
// cc1 is the self-contained preprocessor (libcpp + main, no fork).
// 'cpp' is a driver that spawns cc1 — same fork wall as sdcc.
const TOOLS = ['sdcc', 'cc1', 'sdas8051', 'sdld'];
const toolFactories = {};
for (const tool of TOOLS) {
  const p = join(distDir, tool + '.js');
  if (!existsSync(p)) {
    console.error(`FATAL: ${tool}.js not found at ${p}`);
    process.exit(1);
  }
  const mod = require(p);
  if (typeof mod !== 'function') {
    console.error(`FATAL: ${tool}.js is not modularized (typeof=${typeof mod}). All tools must be linked with -sMODULARIZE=1.`);
    process.exit(1);
  }
  // Probe exported methods by instantiating briefly
  toolFactories[tool] = mod;
  console.log(`  ${tool}.js: OK (factory function)`);
}
if (libFiles.length === 0) {
  console.error('FATAL: No library files found. sdld will fail at stage 4.');
  console.error(`  Tried: ${libDir}`);
  console.error(`  Also:  ${libDirAlt}`);
  process.exit(1);
}

// Populate a module's VFS with headers and libs
function populateVFS(Module) {
  try { Module.FS.mkdir('/include'); } catch(e) {}
  try { Module.FS.mkdir('/include/mcs51'); } catch(e) {}
  try { Module.FS.mkdir('/lib'); } catch(e) {}

  for (const f of headerFiles) Module.FS.writeFile('/include/' + f.name, f.data);
  for (const f of mcs51Headers) Module.FS.writeFile('/include/mcs51/' + f.name, f.data);
  for (const f of libFiles) Module.FS.writeFile('/lib/' + f.name, f.data);
}

// Run a WASM tool and return the Module (with FS still alive).
// setupFn receives the Module during preRun to populate VFS.
// opts can include { stdin: function } for --c1mode piping.
async function runTool(name, args, setupFn, opts) {
  const createModule = toolFactories[name];
  if (!createModule) {
    throw new Error(`Tool ${name} not loaded at startup`);
  }
  // thisProgram sets argv[0]. Critical for sdas8051: sdas_init() checks
  // whether argv[0] starts with "sdas" to enable sdas-specific behaviour
  // (addr field in A records, O record from .optsdcc). Without it,
  // is_sdas() returns false and the assembler acts as generic ASxxxx.
  const config = {
    noExitRuntime: true,
    thisProgram: name,
    arguments: args,
    preRun: [function(M) {
      if (setupFn) setupFn(M);
      if (opts && opts.stdinBytes) {
        try { M.FS.mkdir('/work'); } catch(e) {}
        M.FS.writeFile('/work/test.i', opts.stdinBytes);
        if (!M.FS.init.initialized) M.FS.init();
        const stdin = M.FS.getStream ? M.FS.getStream(0) : M.FS.streams[0];
        if (stdin) M.FS.close(stdin);
        M.FS.unlink('/dev/stdin');
        M.FS.symlink('/work/test.i', '/dev/stdin');
        const input = M.FS.open('/work/test.i', 'r');
        if (input.fd !== 0) throw new Error(`expected compiler input on fd 0, got ${input.fd}`);
        M.FS.chdir('/work');
      }
    }],
    quit: function(status, error) {
      if (status) console.error(`  [${name}] exited with status ${status}`);
      if (error && error.message && !/^Program terminated/.test(error.message)) {
        console.error(`  [${name}] ${error.message}`);
      }
    },
    print: function(text) { console.log(`  [${name}] ${text}`); },
    printErr: function(text) { console.error(`  [${name}] ${text}`); },
  };

  // Allow caller to provide stdin (for --c1mode)
  if (opts && opts.stdin) {
    config.stdin = opts.stdin;
  }

  const Module = await createModule(config);
  return Module;
}

// Read a file from a module's VFS, return as Buffer
function vfsRead(Module, path) {
  return Buffer.from(Module.FS.readFile(path));
}

// Read all files matching a pattern from VFS root
function vfsReadAll(Module, dir) {
  const result = [];
  try {
    for (const name of Module.FS.readdir(dir)) {
      if (name === '.' || name === '..') continue;
      try {
        const data = Module.FS.readFile(dir + '/' + name);
        result.push({ name, data: Buffer.from(data) });
      } catch(e) { /* skip directories */ }
    }
  } catch(e) {}
  return result;
}

async function main() {
  try {
    // ── Stage 1: Preprocess with sdcpp ──
    console.log('\n=== Stage 1: sdcpp (preprocess) ===');

    // cc1 is GCC's frontend — it takes -E to preprocess only.
    // Flags adapted from native sdcc 4.5.0 --verbose output (what
    // sdcpp/cpp passes to cc1 internally). Verify against the CI
    // log's "Native compile (--verbose)" step if they drift.
    const cc1Args = [
      '-E',                     // preprocess only
      '-quiet',
      '-nostdinc',
      '-Wall',
      '-std=c11',
      '-D__SDCC_CHAR_UNSIGNED',
      '-D__SDCC_MODEL_SMALL',
      '-D__SDCC_FLOAT_REENT',
      '-D__SDCCCALL=0',
      '-D__SDCC=4_5_0',
      '-D__SDCC_VERSION_MAJOR=4',
      '-D__SDCC_VERSION_MINOR=5',
      '-D__SDCC_VERSION_PATCH=0',
      '-DSDCC=450',
      '-D__SDCC_REVISION=15242',
      '-D__SDCC_mcs51',
      '-D__STDC_NO_COMPLEX__=1',
      '-D__STDC_NO_THREADS__=1',
      '-D__STDC_NO_ATOMICS__=1',
      '-D__STDC_NO_VLA__=1',
      '-D__STDC_ISO_10646__=201409L',
      '-D__STDC_UTF_16__=1',
      '-D__STDC_UTF_32__=1',
      '-D__SIZEOF_FLOAT__=4',
      '-D__SIZEOF_DOUBLE__=4',
      '-D__SDCC_BITINT_MAXWIDTH=64',
      '-isystem', '/include/mcs51',
      '-isystem', '/include',
      '-o', '/work/test.i',
      '/work/test.c'
    ];

    const cppMod = await runTool('cc1', cc1Args, function(M) {
      populateVFS(M);
      try { M.FS.mkdir('/work'); } catch(e) {}
      M.FS.writeFile('/work/test.c', sourceCode);
    });

    const preprocessed = vfsRead(cppMod, '/work/test.i');
    console.log(`  Preprocessed: ${preprocessed.length} bytes`);
    // Write .i to host for comparison with native -E output
    const wasmIPath = outFile.replace(/\.[^.]+$/, '.i');
    writeFileSync(wasmIPath, preprocessed);
    console.log(`  .i written to ${wasmIPath}`);

    // ── Stage 2: Codegen with sdcc --c1mode ──
    console.log('\n=== Stage 2: sdcc --c1mode (codegen) ===');

    // --c1mode reads preprocessed code from STDIN (not a file argument).
    // "warning 160: only standard input is compiled in c1 mode"
    // We feed it via Emscripten's stdin hook.
    // --c1mode derives module name from -o path. Use bare 'test.asm'
    // so .module is 'test' not '_work_test'.
    const sdccArgs = [
      '--c1mode',
      '-mmcs51', '--model-small',
      '-o', 'test.asm',
    ];

    const ccMod = await runTool('sdcc', sdccArgs, function(M) {
      populateVFS(M);
      try { M.FS.mkdir('/work'); } catch(e) {}
      M.FS.writeFile('/work/test.c', sourceCode);
    }, {
      stdinBytes: preprocessed
    });

    let asmCode;
    try {
      // Emscripten's c1mode can resolve the bare output beside the #line
      // source path. Accept both locations; the browser pipeline deliberately
      // chdirs to /work and uses the latter.
      const asmPath = ccMod.FS.analyzePath('/test.asm').exists
        ? '/test.asm' : '/work/test.asm';
      asmCode = vfsRead(ccMod, asmPath);
    } catch(e) {
      // Check what files were created
      const rootFiles = ccMod.FS.readdir('/').filter(x => x !== '.' && x !== '..');
      console.log('  VFS root after codegen:', rootFiles);
      const workFiles = ccMod.FS.readdir('/work').filter(x => x !== '.' && x !== '..');
      console.log('  VFS /work after codegen:', workFiles);
      throw new Error('Codegen did not produce test.asm: ' + e.message);
    }
    // --c1mode does not emit the .optsdcc directive that the driver adds.
    // Without it, the assembler misses the O record and area tables differ.
    // Inject it after the .module line, matching what native sdcc produces.
    let asmText = asmCode.toString('utf8');
    if (!asmText.includes('.optsdcc')) {
      asmText = asmText.replace(
        /^(\t\.module\s+\S+)/m,
        '$1\n\t.optsdcc -mmcs51 --model-small'
      );
      asmCode = Buffer.from(asmText, 'utf8');
    }
    console.log(`  Assembly: ${asmCode.length} bytes`);
    // Write .asm to host for comparison with native -S output
    const wasmAsmPath = outFile.replace(/\.[^.]+$/, '.asm');
    writeFileSync(wasmAsmPath, asmCode);
    console.log(`  .asm written to ${wasmAsmPath}`);

    // ── Stage 3: Assemble with sdas8051 ──
    console.log('\n=== Stage 3: sdas8051 (assemble) ===');

    const asmArgs = [
      '-plosgffw',
      '/work/test.rel',
      '/work/test.asm'
    ];

    const asMod = await runTool('sdas8051', asmArgs, function(M) {
      try { M.FS.mkdir('/work'); } catch(e) {}
      M.FS.writeFile('/work/test.asm', asmCode);
    });

    let relCode = vfsRead(asMod, '/work/test.rel');

    // Check whether is_sdas() was active (addr field present in A records).
    // If thisProgram: 'sdas8051' worked, addr should be present and the
    // O record should be in the .rel from .optsdcc processing.
    const relText = relCode.toString('utf8');
    const hasAddr = relText.match(/^A .* addr /m);
    const hasO = relText.includes('\nO -mmcs51');
    console.log('  is_sdas() indicators: addr=%s, O-record=%s',
      hasAddr ? 'present' : 'MISSING', hasO ? 'present' : 'MISSING');
    // Also grab .lst and .sym if the assembler produced them —
    // sdld expects /test.lst beside /test.rel
    let lstCode = null, symCode = null;
    try { lstCode = vfsRead(asMod, '/work/test.lst'); } catch(e) {}
    try { symCode = vfsRead(asMod, '/work/test.sym'); } catch(e) {}
    console.log(`  Object: ${relCode.length} bytes` +
      (lstCode ? `, lst: ${lstCode.length}` : '') +
      (symCode ? `, sym: ${symCode.length}` : ''));
    // Write .rel to host for comparison with native
    const wasmRelPath = outFile.replace(/\.[^.]+$/, '.rel');
    writeFileSync(wasmRelPath, relCode);
    console.log(`  .rel written to ${wasmRelPath} for comparison`);
    // Show first 5 lines
    const relTextForLog = relCode.toString('utf8');
    console.log('  .rel first 5 lines:');
    for (const line of relTextForLog.split('\n').slice(0, 5)) {
      console.log('    ' + line);
    }

    // ── Stage 4: Link with sdld ──
    console.log('\n=== Stage 4: sdld (link) ===');

    // Linker script — must match what native sdcc --verbose produces.
    // Captured from native 4.5.0: sdld -nf /tmp/byte-test/native.lk
    // Key: -b HOME = 0x0000 places crt0/reset vector at address 0.
    // User .rel comes AFTER -l lines (libraries provide crt0).
    const lkContent = [
      '-muwx',
      '-i /work/out.ihx',
      '-M',
      '-b HOME = 0x0000',
      '-b XSEG = 0x0001',
      '-b PSEG = 0x0001',
      '-b ISEG = 0x0000',
      '-b BSEG = 0x0000',
      '-k /lib',
      '-l mcs51',
      '-l libsdcc',
      '-l libint',
      '-l liblong',
      '-l libfloat',
      '/work/test.rel',
      '',
      '-e'
    ].join('\n') + '\n';

    const ldArgs = ['-nf', '/work/test.lk'];

    const ldMod = await runTool('sdld', ldArgs, function(M) {
      populateVFS(M);
      try { M.FS.mkdir('/work'); } catch(e) {}
      M.FS.writeFile('/work/test.rel', relCode);
      M.FS.writeFile('/work/test.lk', lkContent);
      // sdld looks for .lst and .sym beside .rel
      if (lstCode) M.FS.writeFile('/work/test.lst', lstCode);
      if (symCode) M.FS.writeFile('/work/test.sym', symCode);
    });

    // Dump the linker script for diagnostics
    console.log('  Linker script:');
    for (const line of lkContent.split('\n')) {
      if (line.trim()) console.log('    ' + line);
    }

    // Dump map file if produced (-M flag)
    try {
      const mapData = vfsRead(ldMod, '/work/out.map');
      console.log('  Map file (' + mapData.length + ' bytes):');
      const mapText = mapData.toString('utf8');
      // Show ALL lines that contain addresses (not just named areas)
      const mapLines = mapText.split('\n');
      // Print ALL area lines (name + addr + size) not just first 40
      console.log('  Area table:');
      for (let i = 0; i < mapLines.length; i++) {
        if (mapLines[i].match(/bytes \(/) || mapLines[i].match(/User Base/)) {
          console.log('    ' + mapLines[i]);
        }
      }
      console.log('  First 40 map lines:');
      for (let i = 0; i < Math.min(40, mapLines.length); i++) {
        console.log('    ' + mapLines[i]);
      }
      console.log('  User bases:');
      for (const line of mapLines) {
        if (line.match(/User Base|HOME|XSEG|PSEG|ISEG|BSEG/)) {
          console.log('    ' + line);
        }
      }
    } catch(e) {
      console.log('  No map file produced');
    }

    let ihxOutput;
    try {
      ihxOutput = vfsRead(ldMod, '/work/out.ihx');
    } catch(e) {
      console.error('  /work/out.ihx not found in VFS after link');
      try {
        console.log('  VFS root:', ldMod.FS.readdir('/'));
      } catch(e2) {}
      throw new Error('Linker did not produce /work/out.ihx');
    }
    writeFileSync(outFile, ihxOutput);
    console.log(`\nWASM compile: OK (${ihxOutput.length} bytes written to ${outFile})`);

  } catch(err) {
    console.error('\nPipeline failed:', err && err.message ? err.message : err);
    process.exit(1);
  }
}

main();
