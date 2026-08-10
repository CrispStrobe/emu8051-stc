/**
 * wasm-compile-driver.cjs — compile a C file with WASM SDCC (NODERAWFS).
 *
 * Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>
 *
 * This driver runs the NODERAWFS-linked SDCC, which reads the real
 * filesystem under Node.js. It is used for the CI byte-identity test.
 *
 * The BROWSER artifact (MEMFS, no NODERAWFS) is a separate binary
 * from the same object files. The compiler code is identical; only
 * the filesystem shim differs. BUILD-INFO records this distinction.
 *
 * 64 MB fixed heap (INITIAL_MEMORY=67108864). No ALLOW_MEMORY_GROWTH.
 */
const { execFileSync } = require('child_process');
const { join, resolve } = require('path');
const { statSync } = require('fs');

const sdccJs = resolve(process.argv[2]);
const installedDir = resolve(process.argv[3]);
const srcFile = resolve(process.argv[4]);
const outFile = resolve(process.argv[5]);

if (!sdccJs || !installedDir || !srcFile || !outFile) {
  console.error('Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>');
  process.exit(1);
}

const incDir = join(installedDir, 'share', 'sdcc', 'include');
const libDir = join(installedDir, 'share', 'sdcc', 'lib', 'small-mcs51-stack-auto');

console.log('Compiling with WASM SDCC (NODERAWFS)...');

try {
  execFileSync('node', [
    sdccJs,
    '-mmcs51', '--model-small',
    '-I' + incDir,
    '-L' + libDir,
    '-o', outFile,
    srcFile
  ], { timeout: 60000, stdio: 'inherit' });
} catch(e) {
  if (e.status !== 0 && e.status !== null) {
    console.error('WASM compile failed (exit', e.status + ')');
    process.exit(1);
  }
}

try {
  const st = statSync(outFile);
  console.log('WASM compile: OK (' + st.size + ' bytes)');
} catch(e) {
  console.error('Output file not found:', outFile);
  process.exit(1);
}
