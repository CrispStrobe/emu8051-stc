/**
 * wasm-compile-driver.cjs — compile a C file with WASM SDCC.
 *
 * Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>
 *
 * EXPORTED_RUNTIME_METHODS dependency: the WASM build uses
 *   -sEXPORTED_RUNTIME_METHODS=callMain,FS
 * This driver uses: m.callMain(), m.FS.writeFile(), m.FS.readFile(),
 * m.FS.mkdir(), m.FS.readdir(). All are under the FS export.
 * File data is written as plain strings — FS.writeFile accepts them
 * directly, no TextEncoder or heap views needed.
 *
 * IMPORTANT: FS.writeFile must not be called before the runtime is
 * initialized. Without MODULARIZE, require() returns the Module but
 * HEAPU8 may not exist yet. Use onRuntimeInitialized to wait.
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

let skipCount = 0;

function mkdirp(m, path) {
  const parts = path.split('/').filter(Boolean);
  let cur = '';
  for (const p of parts) {
    cur += '/' + p;
    try { m.FS.mkdir(cur); } catch(e) {}
  }
}

function addDirToFS(m, hostDir, vfsDir) {
  mkdirp(m, vfsDir);
  for (const name of readdirSync(hostDir)) {
    const hostPath = join(hostDir, name);
    const vfsPath = vfsDir + '/' + name;
    const st = statSync(hostPath);
    if (st.isDirectory()) {
      addDirToFS(m, hostPath, vfsPath);
    } else {
      try {
        // FS.writeFile with a string. HEAPU8 must be in
        // EXPORTED_RUNTIME_METHODS for MEMFS to work.
        m.FS.writeFile(vfsPath, readFileSync(hostPath, 'utf8'));
      } catch(e) {
        console.error('  FAIL:', vfsPath, e.message);
        skipCount++;
      }
    }
  }
}

function doCompile(m) {
  console.log('Runtime initialized, populating VFS...');

  // Populate VFS with mcs51 headers and libs ONLY
  const shareDir = join(installedDir, 'share', 'sdcc');
  const incDir = join(shareDir, 'include');

  // Top-level headers
  mkdirp(m, '/share/sdcc/include');
  for (const f of readdirSync(incDir)) {
    const p = join(incDir, f);
    if (statSync(p).isFile()) {
      try {
        m.FS.writeFile('/share/sdcc/include/' + f, readFileSync(p, 'utf8'));
      } catch(e) {
        console.error('  FAIL:', f, e.message);
        skipCount++;
      }
    }
  }

  // mcs51 sub-headers
  const mcs51Inc = join(incDir, 'mcs51');
  if (existsSync(mcs51Inc)) addDirToFS(m, mcs51Inc, '/share/sdcc/include/mcs51');

  // asm/mcs51 sub-headers
  const asmMcs51 = join(incDir, 'asm', 'mcs51');
  if (existsSync(asmMcs51)) addDirToFS(m, asmMcs51, '/share/sdcc/include/asm/mcs51');

  // asm/default (needed by some headers)
  const asmDefault = join(incDir, 'asm', 'default');
  if (existsSync(asmDefault)) addDirToFS(m, asmDefault, '/share/sdcc/include/asm/default');

  // mcs51 model-small library
  const libDir = join(shareDir, 'lib', 'small-mcs51-stack-auto');
  if (existsSync(libDir)) addDirToFS(m, libDir, '/share/sdcc/lib/small-mcs51-stack-auto');

  if (skipCount > 0) {
    console.error(skipCount, 'files failed to write — aborting');
    process.exit(1);
  }

  console.log('VFS populated. Writing source...');
  m.FS.writeFile('/test.c', readFileSync(srcFile, 'utf8'));

  console.log('Compiling...');
  try {
    m.callMain([
      '-mmcs51', '--model-small',
      '-I/share/sdcc/include',
      '-L/share/sdcc/lib/small-mcs51-stack-auto',
      '-o', '/out.ihx', '/test.c'
    ]);
  } catch(e) {
    // callMain throws on exit() — check if output was produced
    console.log('callMain exited:', e.message || e);
  }

  // Read output
  try {
    const ihx = m.FS.readFile('/out.ihx', { encoding: 'utf8' });
    writeFileSync(outFile, ihx);
    console.log('WASM compile: OK (' + ihx.length + ' bytes)');
  } catch(e) {
    console.error('WASM compile: output not found');
    try { console.error('  FS root:', m.FS.readdir('/').join(', ')); } catch(e2) {}
    process.exit(1);
  }
}

// --- Load and wait for runtime initialization ---

console.log('Loading WASM sdcc from', sdccPath);
const exported = require(sdccPath);

if (typeof exported === 'function') {
  // MODULARIZE: factory returns a promise
  exported().then(m => {
    doCompile(m);
  }).catch(e => {
    console.error('Module factory failed:', e);
    process.exit(1);
  });
} else if (exported && exported.onRuntimeInitialized !== undefined) {
  // Not modularized: Module is returned, wait for runtime
  exported.onRuntimeInitialized = () => doCompile(exported);
} else if (exported && exported.FS && exported.callMain) {
  // Already initialized (unlikely but handle it)
  doCompile(exported);
} else {
  console.error('Cannot determine module type');
  console.error('  typeof:', typeof exported);
  if (exported) console.error('  keys:', Object.keys(exported).slice(0, 20).join(', '));
  process.exit(1);
}
