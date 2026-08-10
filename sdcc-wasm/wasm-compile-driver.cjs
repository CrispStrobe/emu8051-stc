/**
 * wasm-compile-driver.cjs — compile a C file with WASM SDCC.
 *
 * Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>
 *
 * Strategy: set Module.arguments and Module.preRun BEFORE requiring
 * sdcc.js. The module runs main() on load (INVOKE_RUN=1), reads
 * arguments from Module.arguments, and the VFS is populated in preRun.
 *
 * This avoids callMain entirely, which requires stack helpers that
 * may not be exported. It is the better-trodden path for running a
 * CLI tool once under Emscripten.
 *
 * EXIT_RUNTIME=1 means one compile per module instance.
 * For multiple examples, instantiate fresh each time.
 *
 * Build flags this depends on:
 *   -sEXPORTED_RUNTIME_METHODS=FS
 *   -sFORCE_FILESYSTEM
 *   -sEXIT_RUNTIME=1
 *   -sINITIAL_MEMORY=67108864 (64 MB fixed heap)
 */
const { readFileSync, readdirSync, statSync, writeFileSync, existsSync } = require('fs');
const { join } = require('path');

const sdccPath = process.argv[2];
const installedDir = process.argv[3];
const srcFile = process.argv[4];
const outFile = process.argv[5];

if (!sdccPath || !installedDir || !srcFile || !outFile) {
  console.error('Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>');
  process.exit(1);
}

function mkdirp(FS, path) {
  const parts = path.split('/').filter(Boolean);
  let cur = '';
  for (const p of parts) {
    cur += '/' + p;
    try { FS.mkdir(cur); } catch(e) {}
  }
}

function addDirToFS(FS, hostDir, vfsDir) {
  mkdirp(FS, vfsDir);
  for (const name of readdirSync(hostDir)) {
    const hostPath = join(hostDir, name);
    const vfsPath = vfsDir + '/' + name;
    const st = statSync(hostPath);
    if (st.isDirectory()) {
      addDirToFS(FS, hostPath, vfsPath);
    } else {
      FS.writeFile(vfsPath, readFileSync(hostPath, 'utf8'));
    }
  }
}

// Set up Module BEFORE requiring sdcc.js
const shareDir = join(installedDir, 'share', 'sdcc');

globalThis.Module = {
  // Command-line arguments for sdcc's main()
  arguments: [
    '-mmcs51', '--model-small',
    '-I/share/sdcc/include',
    '-L/share/sdcc/lib/small-mcs51-stack-auto',
    '-o', '/out.ihx', '/test.c'
  ],

  // Populate VFS before main() runs
  preRun: [function() {
    const FS = globalThis.Module.FS || Module.FS;
    console.log('preRun: populating VFS...');

    // mcs51 headers
    const incDir = join(shareDir, 'include');
    mkdirp(FS, '/share/sdcc/include');
    for (const f of readdirSync(incDir)) {
      if (statSync(join(incDir, f)).isFile()) {
        FS.writeFile('/share/sdcc/include/' + f, readFileSync(join(incDir, f), 'utf8'));
      }
    }
    const mcs51Inc = join(incDir, 'mcs51');
    if (existsSync(mcs51Inc)) addDirToFS(FS, mcs51Inc, '/share/sdcc/include/mcs51');
    const asmMcs51 = join(incDir, 'asm', 'mcs51');
    if (existsSync(asmMcs51)) addDirToFS(FS, asmMcs51, '/share/sdcc/include/asm/mcs51');
    const asmDefault = join(incDir, 'asm', 'default');
    if (existsSync(asmDefault)) addDirToFS(FS, asmDefault, '/share/sdcc/include/asm/default');

    // mcs51 library
    const libDir = join(shareDir, 'lib', 'small-mcs51-stack-auto');
    if (existsSync(libDir)) addDirToFS(FS, libDir, '/share/sdcc/lib/small-mcs51-stack-auto');

    // Source file
    FS.writeFile('/test.c', readFileSync(srcFile, 'utf8'));
    console.log('preRun: VFS ready, source written');
  }],

  // After main() finishes, read the output
  onExit: function(code) {
    console.log('sdcc exited with code', code);
  },

  // Suppress default print to stdout (sdcc version banner etc.)
  print: function(text) { console.log('sdcc:', text); },
  printErr: function(text) { console.error('sdcc:', text); },
};

console.log('Loading WASM sdcc (main will run on load)...');

try {
  require(sdccPath);
} catch(e) {
  // Module may throw on exit(0)
  console.log('Module exited:', e.message || e);
}

// Read output after main() ran
const FS = globalThis.Module.FS;
if (FS) {
  try {
    const ihx = FS.readFile('/out.ihx', { encoding: 'utf8' });
    writeFileSync(outFile, ihx);
    console.log('WASM compile: OK (' + ihx.length + ' bytes)');
  } catch(e) {
    console.error('WASM compile: output not found');
    try { console.error('  FS root:', FS.readdir('/').join(', ')); } catch(e2) {}
    process.exit(1);
  }
} else {
  console.error('FS not available after module exit');
  process.exit(1);
}
