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
 *
 * EXPORTED_RUNTIME_METHODS dependency: the WASM build uses
 *   -sEXPORTED_RUNTIME_METHODS=callMain,FS
 * This driver uses: m.callMain(), m.FS.writeFile(), m.FS.readFile(),
 * m.FS.mkdir(), m.FS.readdir(). All are under the FS export.
 * File data is written via TextEncoder (standard API, no Emscripten
 * helpers needed).
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
      try {
        // Convert to Uint8Array via TextEncoder — works in all environments
        const content = readFileSync(hostPath, 'utf8');
        const encoded = new TextEncoder().encode(content);
        m.FS.writeFile(vfsPath, encoded);
      } catch(e) {
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

  // Populate VFS with ONLY mcs51 headers and libs — not the whole tree.
  // Copying z80/f8/etc is wasted work and a source of encoding errors.
  const shareDir = join(installedDir, 'share', 'sdcc');
  const incDir = join(shareDir, 'include');
  // Top-level headers (8051.h, stdint.h, etc.)
  mkdirp(m, '/share/sdcc/include');
  for (const f of readdirSync(incDir)) {
    const p = join(incDir, f);
    if (statSync(p).isFile()) {
      const content = readFileSync(p, 'utf8');
      m.FS.writeFile('/share/sdcc/include/' + f, new TextEncoder().encode(content));
    }
  }
  // mcs51 sub-headers
  if (statSync(join(incDir, 'mcs51')).isDirectory()) {
    addDirToFS(m, join(incDir, 'mcs51'), '/share/sdcc/include/mcs51');
  }
  // asm/mcs51 sub-headers
  const asmMcs51 = join(incDir, 'asm', 'mcs51');
  if (statSync(asmMcs51).isDirectory()) {
    addDirToFS(m, asmMcs51, '/share/sdcc/include/asm/mcs51');
  }
  // mcs51 model-small library only
  const libDir = join(shareDir, 'lib', 'small-mcs51-stack-auto');
  if (statSync(libDir).isDirectory()) {
    addDirToFS(m, libDir, '/share/sdcc/lib/small-mcs51-stack-auto');
  }

  // Write source file
  const srcContent = readFileSync(srcFile, 'utf8');
  m.FS.writeFile('/test.c', new TextEncoder().encode(srcContent));

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
