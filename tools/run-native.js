const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

function existingDir(candidate) {
  try {
    return fs.statSync(candidate).isDirectory() ? candidate : null;
  } catch (_) {
    return null;
  }
}

function compilerBin() {
  const configured = process.env.CXX;
  if (configured) {
    const configuredPath = path.dirname(configured.replace(/^"|"$/g, ''));
    if (existingDir(configuredPath)) return configuredPath;
  }

  const localAppData = process.env.LOCALAPPDATA;
  if (localAppData) {
    const packages = path.join(localAppData, 'Microsoft', 'WinGet', 'Packages');
    try {
      const match = fs.readdirSync(packages)
        .filter((name) => name.startsWith('BrechtSanders.WinLibs.'))
        .map((name) => path.join(packages, name, 'mingw64', 'bin'))
        .map(existingDir)
        .find(Boolean);
      if (match) return match;
    } catch (_) {
      // Fall through to the process PATH and let PlatformIO report the error.
    }
  }

  return null;
}

const bin = compilerBin();
const env = { ...process.env };
if (bin) {
  env.PATH = `${bin}${path.delimiter}${env.PATH || ''}`;
  console.log(`Native toolchain: ${bin}`);
} else {
  const probe = spawnSync(process.platform === 'win32' ? 'where' : 'which', ['g++'], {
    encoding: 'utf8',
  });
  if (probe.status !== 0) {
    console.error('g++ was not found. Install a MinGW toolchain or set CXX to g++.exe.');
    process.exit(1);
  }
}

const args = process.argv.slice(2);
if (args.length === 0) args.push('run', '-e', 'native', '-t', 'exec');

const cwd = path.resolve(__dirname, '..');
const shell = process.platform === 'win32';
const candidates = process.platform === 'win32'
  ? [['pio', []], ['python', ['-m', 'platformio']], ['py', ['-m', 'platformio']]]
  : [['pio', []], ['python3', ['-m', 'platformio']], ['python', ['-m', 'platformio']]];

let platformio = null;
for (const [command, prefix] of candidates) {
  const probe = spawnSync(command, [...prefix, '--version'], {
    cwd, env, stdio: 'ignore', shell,
  });
  if (probe.status === 0) {
    platformio = [command, prefix];
    break;
  }
}
if (!platformio) {
  console.error('PlatformIO was not found. Install it with `python -m pip install platformio`, then retry.');
  process.exit(1);
}

const [command, prefix] = platformio;
const result = spawnSync(command, [...prefix, ...args], {
  cwd, env, stdio: 'inherit', shell,
});

if (result.error) {
  console.error(result.error.message);
  process.exit(1);
}
process.exit(result.status == null ? 1 : result.status);
