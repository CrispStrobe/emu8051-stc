#!/usr/bin/env node
/**
 * check-exports.mjs — assert the WASM build exports are a SUPERSET of
 * the checked-in manifest. The manifest only ever grows.
 *
 * Usage: node check-exports.mjs [build/emu8051.js]
 *
 * Exit 0 = pass, 1 = missing exports, 2 = cannot load.
 */
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));

const jsPath = process.argv[2] || join(__dirname, 'build', 'emu8051.js');
const manifestPath = join(__dirname, 'exports-manifest.txt');

// Load the manifest
const manifest = readFileSync(manifestPath, 'utf-8')
    .split('\n')
    .map(s => s.trim())
    .filter(s => s.length > 0);

// Load the WASM module and check its exports
const createEmu8051 = (await import(jsPath)).default;
const Module = await createEmu8051();

// Get all exported function names
const exportedNames = Object.keys(Module)
    .filter(k => k.startsWith('_emu_') || k === '_malloc' || k === '_free');

// Also check via cwrap — some exports are only reachable through ccall/cwrap
const missing = [];
for (const name of manifest) {
    // Strip leading underscore for Module lookup
    const jsName = name;
    const found = exportedNames.includes(jsName) ||
                  typeof Module[jsName] === 'function' ||
                  typeof Module.cwrap(name.slice(1), 'number', []) === 'function';
    if (!found) {
        // Try calling it — cwrap succeeds silently for non-existent functions
        // but the actual call would fail. Check via asm exports instead.
        missing.push(name);
    }
}

// More reliable check: read the JS file and look for the export names
const jsSource = readFileSync(jsPath, 'utf-8');
const actualMissing = [];
for (const name of manifest) {
    // The export name appears as a string in the JS glue
    if (!jsSource.includes(`"${name}"`) && !jsSource.includes(`'${name}'`)) {
        actualMissing.push(name);
    }
}

if (actualMissing.length > 0) {
    console.error(`FAIL: ${actualMissing.length} export(s) missing from build:`);
    for (const name of actualMissing) console.error(`  ${name}`);
    console.error(`\nManifest: ${manifestPath}`);
    console.error(`Build:    ${jsPath}`);
    process.exit(1);
} else {
    console.log(`PASS: all ${manifest.length} manifest exports present in build`);

    // Check for new exports not in manifest (informational)
    const manifestSet = new Set(manifest);
    const extra = exportedNames.filter(n => n.startsWith('_emu_') && !manifestSet.has(n));
    if (extra.length > 0) {
        console.log(`INFO: ${extra.length} new export(s) not yet in manifest:`);
        for (const name of extra) console.log(`  ${name}`);
        console.log('Consider adding them to exports-manifest.txt');
    }
    process.exit(0);
}
