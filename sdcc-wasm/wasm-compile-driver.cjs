/**
 * wasm-compile-driver.cjs — compile a C file with WASM SDCC (MEMFS).
 *
 * Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>
 *
 * This driver runs the BROWSER artifact (MEMFS, no NODERAWFS).
 * It populates the in-module VFS with the source file, headers, and
 * libraries before main() runs, then reads the output back from the
 * VFS after compilation.
 *
 * All paths inside the VFS are simple absolute paths (/test.c, /out.ihx).
 * The host layout is NOT mirrored.
 *
 * 64 MB fixed heap (INITIAL_MEMORY=67108864). No ALLOW_MEMORY_GROWTH.
 */
const { readFileSync, writeFileSync, readdirSync, statSync } = require('fs');
const { join, resolve, basename } = require('path');

const sdccJs = resolve(process.argv[2]);
const installedDir = resolve(process.argv[3]);
const srcFile = resolve(process.argv[4]);
const outFile = resolve(process.argv[5]);

if (!sdccJs || !installedDir || !srcFile || !outFile) {
  console.error('Usage: node wasm-compile-driver.cjs <sdcc.js> <installed-dir> <src.c> <out.ihx>');
  process.exit(1);
}

const incDir = join(installedDir, 'share', 'sdcc', 'include');
const mcs51IncDir = join(incDir, 'mcs51');
const libDir = join(installedDir, 'share', 'sdcc', 'lib', 'small-mcs51-stack-auto');

// Read the source file from the host filesystem
const sourceCode = readFileSync(srcFile);

// Collect header files to populate the VFS
function collectFiles(dir) {
  const result = [];
  try {
    for (const name of readdirSync(dir)) {
      const full = join(dir, name);
      const st = statSync(full);
      if (st.isFile()) {
        result.push({ name, data: readFileSync(full) });
      }
    }
  } catch (e) {
    console.warn('Warning: could not read', dir, '-', e.message);
  }
  return result;
}

const headerFiles = collectFiles(incDir);
const mcs51Headers = collectFiles(mcs51IncDir);
const libFiles = collectFiles(libDir);

console.log('Compiling with WASM SDCC (MEMFS)...');
console.log(`  Source: ${srcFile} (${sourceCode.length} bytes)`);
console.log(`  Headers: ${headerFiles.length} top-level, ${mcs51Headers.length} mcs51`);
console.log(`  Lib files: ${libFiles.length}`);

// Load the Emscripten module factory (MODULARIZE=1, EXPORT_NAME=createSDCC)
const createSDCC = require(sdccJs);

// Create the module with VFS population in preRun.
// No EXIT_RUNTIME — we need the FS alive to read output after main() returns.
createSDCC({
  noExitRuntime: true,
  // Arguments for sdcc main()
  arguments: [
    '-mmcs51', '--model-small',
    '--nostdinc',
    '-I/include',
    '-I/include/mcs51',
    '--nostdlib',
    '-L/lib',
    '-l', 'mcs51',
    '-l', 'libsdcc',
    '-l', 'liblong',
    '-l', 'libint',
    '-l', 'libfloat',
    '-o', '/out.ihx',
    '/test.c'
  ],

  // Populate VFS before main() runs
  preRun: [function(Module) {
    console.log('Runtime initialized');

    // Create directories
    Module.FS.mkdir('/include');
    Module.FS.mkdir('/include/mcs51');
    Module.FS.mkdir('/lib');

    // Write the source file
    Module.FS.writeFile('/test.c', sourceCode);
    console.log('VFS populated: /test.c');

    // Write headers
    for (const f of headerFiles) {
      Module.FS.writeFile('/include/' + f.name, f.data);
    }
    console.log(`VFS populated: ${headerFiles.length} headers in /include/`);

    for (const f of mcs51Headers) {
      Module.FS.writeFile('/include/mcs51/' + f.name, f.data);
    }
    console.log(`VFS populated: ${mcs51Headers.length} headers in /include/mcs51/`);

    // Write library files
    for (const f of libFiles) {
      Module.FS.writeFile('/lib/' + f.name, f.data);
    }
    console.log(`VFS populated: ${libFiles.length} lib files in /lib/`);

    // List root to confirm
    console.log('VFS root:', Module.FS.readdir('/').filter(x => x !== '.' && x !== '..'));
  }],

  print: function(text) { console.log(text); },
  printErr: function(text) { console.error(text); },
}).then(function(Module) {
  // main() has returned. noExitRuntime keeps the FS alive.
  // Read the output from the VFS, then exit.
  try {
    const output = Module.FS.readFile('/out.ihx');
    writeFileSync(outFile, output);
    console.log('WASM compile: OK (' + output.length + ' bytes written to ' + outFile + ')');
  } catch (e) {
    console.error('Failed to read /out.ihx from VFS:', e.message);
    try {
      console.log('VFS root after compile:', Module.FS.readdir('/'));
    } catch (e2) { /* ignore */ }
    process.exit(1);
  }
}).catch(function(err) {
  console.error('Module failed:', err.message || err);
  process.exit(1);
});
