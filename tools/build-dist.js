#!/usr/bin/env node
// Build the complete browser demo bundle and copy it to dist/.
//
// The generated bundle is committed. This command is intentionally the one
// source of truth used by the pre-push hook and by contributors who want to
// refresh the published demo.

const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');

function run(command, args) {
  // These are Node-to-Node invocations. Do not route them through cmd.exe on
  // Windows: process.execPath commonly lives under `C:\Program Files`, and a
  // shell launch splits that path at the space before Node can start.
  const result = spawnSync(command, args, { cwd: root, stdio: 'inherit' });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
}

// Build through the existing wrappers so Windows gets the project-owned MinGW
// PATH and Emscripten gets the pinned SDK activation.
run(process.execPath, [path.join('tools', 'run-native.js'), 'run', '-e', 'native']);
run(process.execPath, [path.join('tools', 'dump-frames.js')]);
run(process.execPath, [path.join('tools', 'run-wasm.js')]);

const source = path.join(root, 'web');
const destination = path.join(root, 'dist');
fs.rmSync(destination, { recursive: true, force: true });
fs.cpSync(source, destination, {
  recursive: true,
  filter: (entry) => {
    const parts = entry.split(path.sep);
    return !parts.includes('.obj') && !parts.includes('local') && path.basename(entry) !== 'build.json';
  },
});

console.log('Wrote committed demo bundle to dist/.');
