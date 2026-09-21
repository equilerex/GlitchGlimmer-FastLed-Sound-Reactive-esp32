const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');

function winlibsBins() {
  if (process.platform !== 'win32' || !process.env.LOCALAPPDATA) return [];
  const packages = path.join(process.env.LOCALAPPDATA, 'Microsoft', 'WinGet', 'Packages');
  try {
    return fs.readdirSync(packages)
      .filter((name) => name.startsWith('BrechtSanders.WinLibs.'))
      .map((name) => path.join(packages, name, 'mingw64', 'bin'))
      .filter((bin) => fs.existsSync(path.join(bin, 'g++.exe')));
  } catch (_) {
    return [];
  }
}

function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, stdio: 'inherit', windowsHide: true });
  if (result.error) throw result.error;
  return result.status === 0;
}

const installed = winlibsBins();
if (installed.length > 0) {
  console.log(`Native toolchain ready: ${installed[0]}`);
  process.exit(0);
}

if (process.platform === 'win32') {
  console.log('Installing the native host toolchain through WinGet...');
  if (!run('winget', ['install', '--id', 'BrechtSanders.WinLibs.POSIX.UCRT', '-e',
    '--accept-source-agreements', '--accept-package-agreements'])) {
    console.error('Native toolchain installation failed.');
    process.exit(1);
  }
  console.log('Native toolchain installed. Run npm run native.');
  process.exit(0);
}

if (spawnSync('g++', ['--version'], { stdio: 'inherit' }).status !== 0) {
  console.error('g++ is required for the native harness. Install it with your OS package manager.');
  process.exit(1);
}
console.log('Native toolchain ready from PATH.');
