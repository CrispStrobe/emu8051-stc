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
const { execFileSync } = require('child_process');
const { join, resolve } = require('path');
const { statSync, existsSync } = require('fs');

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

// SDCC under Emscripten uses its internal VFS, not the host filesystem.
// Inject files via Module.preRun, then read output from the VFS.
// The trick: use node -e to set up Module before requiring sdcc.js.
const { readFileSync: rfs } = require('fs');
const srcContent = rfs(srcFile, 'utf8');

// Build the include/lib trees as JSON for injection
function collectFiles(dir, prefix) {
  const files = [];
  const { readdirSync: rd, statSync: ss } = require('fs');
  for (const name of rd(dir)) {
    const p = join(dir, name);
    if (ss(p).isDirectory()) {
      files.push(...collectFiles(p, prefix + '/' + name));
    } else {
      files.push({ path: prefix + '/' + name, content: rfs(p, 'utf8') });
    }
  }
  return files;
}

const incFiles = collectFiles(incDir, '/share/sdcc/include');
const libFiles = existsSync(libDir) ? collectFiles(libDir, '/share/sdcc/lib/small-mcs51-stack-auto') : [];

const script = `
var fs = require('fs');
var filesJson = fs.readFileSync(process.argv[1], 'utf8');
var data = JSON.parse(filesJson);

Module = {
  arguments: ['-mmcs51', '--model-small',
    '-I/share/sdcc/include',
    '-L/share/sdcc/lib/small-mcs51-stack-auto',
    '-o', '/out.ihx', '/test.c'],
  preRun: [function() {
    function mkdirp(p) {
      var parts = p.split('/').filter(Boolean);
      var cur = '';
      for (var i = 0; i < parts.length; i++) {
        cur += '/' + parts[i];
        try { Module.FS.mkdir(cur); } catch(e) {}
      }
    }
    // Write headers and libs
    for (var i = 0; i < data.files.length; i++) {
      var f = data.files[i];
      var dir = f.path.split('/').slice(0, -1).join('/');
      mkdirp(dir);
      Module.FS.writeFile(f.path, f.content);
    }
    // Write source
    Module.FS.writeFile('/test.c', data.src);
  }],
  postRun: [function() {
    try {
      var ihx = Module.FS.readFile('/out.ihx', {encoding:'utf8'});
      fs.writeFileSync(data.outFile, ihx);
      process.stderr.write('WASM compile: OK (' + ihx.length + ' bytes)\\n');
    } catch(e) {
      process.stderr.write('WASM compile: no output\\n');
      var root = Module.FS.readdir('/');
      process.stderr.write('FS root: ' + root.join(',') + '\\n');
      process.exit(1);
    }
  }],
  print: function(t) {},
  printErr: function(t) { process.stderr.write('sdcc: ' + t + '\\n'); },
};
require(process.argv[2]);
`;

// Write data file
const dataFile = join(require('os').tmpdir(), 'sdcc-data-' + process.pid + '.json');
const { writeFileSync: wfs } = require('fs');
wfs(dataFile, JSON.stringify({
  files: [...incFiles, ...libFiles],
  src: srcContent,
  outFile: outFile,
}));

try {
  const result = execFileSync('node', ['-e', script, dataFile, sdccJs], {
    timeout: 60000,
    encoding: 'utf8',
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  if (result.trim()) console.log(result.trim());
  console.log('Done');
} catch(e) {
  if (e.stderr) {
    const lines = e.stderr.toString().trim().split('\n');
    for (const l of lines) console.log(l);
    if (lines.some(l => l.includes('WASM compile: OK'))) {
      // Success — sdcc exits with 0 but execFileSync may report stderr
    } else {
      console.error('WASM compile failed');
      process.exit(1);
    }
  } else {
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
