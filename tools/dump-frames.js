#!/usr/bin/env node
//
// Run the harness binary to record frames into web/data/ for the player.
//
//     npm run frames
//     npm run frames -- some/other/dir
//
// The binary PlatformIO builds is `program` on Linux and macOS and `program.exe`
// on Windows, which is the whole reason this exists rather than a one-line npm
// script. It also builds the harness first when asked with --build.
//
// `pio run -e native -t exec` cannot be used here: it takes no program arguments
// and rejects them as stray options, so the binary has to be named directly.

const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const exe = path.join(root, '.pio', 'build', 'native',
  process.platform === 'win32' ? 'program.exe' : 'program');

const args = process.argv.slice(2);
const build = args.includes('--build');
const dir = args.find((a) => !a.startsWith('--')) || 'web/data';

if (build) {
  const built = spawnSync('pio', ['run', '-e', 'native'], { cwd: root, stdio: 'inherit', shell: true });
  if (built.status !== 0) {
    process.exit(built.status ?? 1);
  }
}

if (!fs.existsSync(exe)) {
  console.error(`${path.relative(root, exe).split(path.sep).join('/')} is missing. Build it first:\n\n` +
    '    npm run frames -- --build\n' +
    '    # or: pio run -e native\n');
  process.exit(1);
}

// Run from the repo root and pass the directory relative, which is the form the
// recordings were verified byte-identical under on a different machine.
const result = spawnSync(exe, ['--dump-frames', dir], { cwd: root, stdio: 'inherit' });

if (result.error) {
  // A mingw-built host binary needs the same DLLs to run that it needed to link,
  // and exits with no output rather than an error when they are not on PATH.
  console.error(`Could not run ${path.basename(exe)}: ${result.error.message}`);
  process.exit(1);
}
if (result.status !== 0) {
  console.error(`\n${path.basename(exe)} exited ${result.status}. A host binary needs its compiler's ` +
    'bin directory on PATH to run, not just to link.');
}
process.exit(result.status ?? 1);
