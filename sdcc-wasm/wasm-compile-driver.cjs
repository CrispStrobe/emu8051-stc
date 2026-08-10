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
const libDir = join(installedDir, 'share', 'sdcc', 'lib', 'small-mcs51-stack-auto');

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
const libFiles = collectFiles(libDir);

console.log('WASM SDCC pipeline (MEMFS, no fork)');
console.log(`  Source: ${srcFile} (${sourceCode.length} bytes)`);
console.log(`  Headers: ${headerFiles.length} top-level, ${mcs51Headers.length} mcs51`);
console.log(`  Lib files: ${libFiles.length}`);

// Populate a module's VFS with headers and libs
function populateVFS(Module) {
  try { Module.FS.mkdir('/include'); } catch(e) {}
  try { Module.FS.mkdir('/include/mcs51'); } catch(e) {}
  try { Module.FS.mkdir('/lib'); } catch(e) {}

  for (const f of headerFiles) Module.FS.writeFile('/include/' + f.name, f.data);
  for (const f of mcs51Headers) Module.FS.writeFile('/include/mcs51/' + f.name, f.data);
  for (const f of libFiles) Module.FS.writeFile('/lib/' + f.name, f.data);
}

// Run a WASM tool and return the Module (with FS still alive)
async function runTool(name, args, setupFn) {
  const jsPath = join(distDir, name + '.js');
  if (!existsSync(jsPath)) {
    throw new Error(`Tool not found: ${jsPath}`);
  }

  const createModule = require(jsPath);
  const Module = await createModule({
    noExitRuntime: true,
    arguments: args,
    preRun: [function(M) {
      if (setupFn) setupFn(M);
    }],
    print: function(text) { console.log(`  [${name}] ${text}`); },
    printErr: function(text) { console.error(`  [${name}] ${text}`); },
  });

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

    const sdcppArgs = [
      '-nostdinc', '-Wall', '-std=c11',
      '-obj-ext=.rel',
      '-D__SDCC_CHAR_UNSIGNED',
      '-D__SDCC_MODEL_SMALL',
      '-D__SDCC_FLOAT_REENT',
      '-D__SDCCCALL=0',
      '-D__SDCC=4_5_0',
      '-D__SDCC_VERSION_MAJOR=4',
      '-D__SDCC_VERSION_MINOR=5',
      '-D__SDCC_VERSION_PATCH=0',
      '-DSDCC=450',
      '-D__SDCC_mcs51',
      '-D__STDC_NO_COMPLEX__=1',
      '-D__STDC_NO_THREADS__=1',
      '-D__STDC_NO_ATOMICS__=1',
      '-D__STDC_NO_VLA__=1',
      '-isystem', '/include/mcs51',
      '-isystem', '/include',
      '-o', '/test.i',
      '/test.c'
    ];

    const cppMod = await runTool('sdcpp', sdcppArgs, function(M) {
      populateVFS(M);
      M.FS.writeFile('/test.c', sourceCode);
    });

    const preprocessed = vfsRead(cppMod, '/test.i');
    console.log(`  Preprocessed: ${preprocessed.length} bytes`);

    // ── Stage 2: Codegen with sdcc --c1mode ──
    console.log('\n=== Stage 2: sdcc --c1mode (codegen) ===');

    // --c1mode reads preprocessed from stdin, writes .asm to stdout.
    // But in practice sdcc --c1mode also takes a filename argument.
    // Let's try giving it the preprocessed file directly.
    const sdccArgs = [
      '-mmcs51', '--model-small',
      '--c1mode',
      '--nostdinc',
      '-o', '/test.asm',
      '/test.i'
    ];

    const ccMod = await runTool('sdcc', sdccArgs, function(M) {
      populateVFS(M);
      M.FS.writeFile('/test.i', preprocessed);
    });

    let asmCode;
    try {
      asmCode = vfsRead(ccMod, '/test.asm');
    } catch(e) {
      // c1mode may write to stdout — check what files were created
      const rootFiles = ccMod.FS.readdir('/').filter(x => x !== '.' && x !== '..');
      console.log('  VFS root after codegen:', rootFiles);
      throw new Error('Codegen did not produce /test.asm');
    }
    console.log(`  Assembly: ${asmCode.length} bytes`);

    // ── Stage 3: Assemble with sdas8051 ──
    console.log('\n=== Stage 3: sdas8051 (assemble) ===');

    const asmArgs = [
      '-plosgffw',
      '/test.rel',
      '/test.asm'
    ];

    const asMod = await runTool('sdas8051', asmArgs, function(M) {
      M.FS.writeFile('/test.asm', asmCode);
    });

    const relCode = vfsRead(asMod, '/test.rel');
    console.log(`  Object: ${relCode.length} bytes`);

    // ── Stage 4: Link with sdld ──
    console.log('\n=== Stage 4: sdld (link) ===');

    // sdld uses a linker script (.lk file)
    const lkContent = [
      '-muwx',
      '-i /out.ihx',
      '-b _CODE = 0x0000',
      '-b _DATA = 0x0030',
      '-b _XDATA = 0x0000',
      '-k /lib',
      '-l mcs51',
      '-l libsdcc',
      '-l liblong',
      '-l libint',
      '-l libfloat',
      '/test.rel',
      '-e'
    ].join('\n') + '\n';

    const ldArgs = ['-nf', '/test.lk'];

    const ldMod = await runTool('sdld', ldArgs, function(M) {
      populateVFS(M);
      M.FS.writeFile('/test.rel', relCode);
      M.FS.writeFile('/test.lk', lkContent);
    });

    const ihxOutput = vfsRead(ldMod, '/out.ihx');
    writeFileSync(outFile, ihxOutput);
    console.log(`\nWASM compile: OK (${ihxOutput.length} bytes written to ${outFile})`);

  } catch(err) {
    console.error('\nPipeline failed:', err.message);
    process.exit(1);
  }
}

main();
