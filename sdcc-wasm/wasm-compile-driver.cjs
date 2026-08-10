/**
 * wasm-compile-driver.cjs — compile a C file with WASM SDCC.
 *
 * Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>
 *
 * Runs WASM sdcc as a subprocess: `node sdcc.js [args]`.
 * Under Node.js, Emscripten with FORCE_FILESYSTEM uses NODEFS to
 * read the real filesystem — no VFS setup needed from the driver.
 *
 * 64 MB fixed heap. EXIT_RUNTIME=1 means one compile per invocation.
 * For multiple examples, invoke fresh each time.
 *
 * NOTE for browser use (bw-bundle): this driver uses NODEFS which
 * only works in Node. In the browser, use Module.preRun to populate
 * MEMFS with FS.writeFile before main() runs. The fixed-heap build
 * (no ALLOW_MEMORY_GROWTH) makes FS.writeFile work reliably.
 */
const { execFileSync, execSync } = require('child_process');
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

console.log('Compiling with WASM SDCC...');
console.log('  sdcc.js:', sdccJs);
console.log('  -I:', incDir);
console.log('  src:', srcFile);
console.log('  out:', outFile);

try {
  const output = execFileSync('node', [
    sdccJs,
    '-mmcs51', '--model-small',
    '-I' + incDir,
    '-L' + libDir,
    '-o', outFile,
    srcFile
  ], {
    timeout: 60000,
    encoding: 'utf8',
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  if (output.trim()) console.log(output.trim());
} catch(e) {
  if (e.status === 0 || e.status === null) {
    // Some Emscripten builds exit via throw rather than process.exit
    if (e.stdout) console.log(e.stdout.toString().trim());
  } else {
    console.error('WASM compile failed (exit', e.status + ')');
    if (e.stdout) console.error('stdout:', e.stdout.toString().trim());
    if (e.stderr) console.error('stderr:', e.stderr.toString().trim());
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
