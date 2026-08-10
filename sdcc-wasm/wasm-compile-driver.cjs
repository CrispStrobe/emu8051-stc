/**
 * wasm-compile-driver.cjs — compile a C file with WASM SDCC.
 *
 * Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>
 *
 * Populates Emscripten's virtual filesystem with SDCC headers and
 * libraries from <installed-dir>/share/sdcc/, writes the source file,
 * and calls sdcc via callMain. Output .ihx is written to <out.ihx>.
 *
 * This is the VFS recipe bw-bundle needs for the browser compile path.
 */
const { readFileSync, readdirSync, statSync, writeFileSync } = require('fs');
const { join } = require('path');

const sdccPath = process.argv[2];
const installedDir = process.argv[3];
const srcFile = process.argv[4];
const outFile = process.argv[5];

if (!sdccPath || !installedDir || !srcFile || !outFile) {
  console.error('Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>');
  process.exit(1);
}

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
      // Text files (.h, .c, .lib, .rel) — write as string
      // Binary approach failed with 'Cannot read properties of undefined (reading buffer)'
      try {
        const content = readFileSync(hostPath, 'utf8');
        m.FS.writeFile(vfsPath, content);
      } catch(e) {
        // Skip files that can't be read as text (shouldn't happen for SDCC)
        console.log('  skip:', vfsPath, e.message);
      }
    }
  }
}

async function main() {
  console.log('Loading WASM sdcc from', sdccPath);
  const factory = require(sdccPath);
  let m;
  if (typeof factory === 'function') {
    m = await factory();
  } else {
    m = factory;
  }

  if (!m || !m.callMain) {
    console.error('WASM module loaded but missing callMain');
    console.error('  typeof m:', typeof m);
    if (m) console.error('  keys:', Object.keys(m).slice(0, 20).join(', '));
    process.exit(1);
  }

  // Debug FS state
  console.log('  FS exists:', !!m.FS);
  console.log('  FS.writeFile:', typeof (m.FS && m.FS.writeFile));
  console.log('  FS.mkdir:', typeof (m.FS && m.FS.mkdir));
  console.log('  FS.readdir /:', m.FS ? m.FS.readdir('/').join(',') : 'no FS');

  if (!m.FS || !m.FS.writeFile) {
    console.error('FS module not available — FORCE_FILESYSTEM may not have taken effect');
    process.exit(1);
  }

  console.log('WASM sdcc loaded, populating VFS...');

  // Populate VFS with installed SDCC headers and libs
  const shareDir = join(installedDir, 'share', 'sdcc');
  addDirToFS(m, join(shareDir, 'include'), '/share/sdcc/include');
  addDirToFS(m, join(shareDir, 'lib'), '/share/sdcc/lib');

  // Write source file
  m.FS.writeFile('/test.c', readFileSync(srcFile, 'utf8'));

  console.log('Compiling', srcFile, '...');

  // Compile
  try {
    m.callMain([
      '-mmcs51', '--model-small', '--no-std-crt0',
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

main().catch(e => { console.error('Driver error:', e); process.exit(1); });
