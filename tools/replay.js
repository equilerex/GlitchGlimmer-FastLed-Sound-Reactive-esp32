#!/usr/bin/env node
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

function compilerBin() {
  const localAppData = process.env.LOCALAPPDATA;
  if (localAppData) {
    const packages = path.join(localAppData, 'Microsoft', 'WinGet', 'Packages');
    try {
      const match = fs.readdirSync(packages)
        .filter((name) => name.startsWith('BrechtSanders.WinLibs.'))
        .map((name) => path.join(packages, name, 'mingw64', 'bin'))
        .find((p) => fs.existsSync(p) && fs.statSync(p).isDirectory());
      if (match) return match;
    } catch (_) {}
  }
  return null;
}

const root = path.resolve(__dirname, '..');
const env = { ...process.env };
const bin = compilerBin();
if (bin) {
  const pathKey = Object.keys(env).find((k) => k.toLowerCase() === 'path') || 'PATH';
  env[pathKey] = bin + path.delimiter + (env[pathKey] || '');
}

const exe = path.join(root, '.pio', 'build', 'native', process.platform === 'win32' ? 'program.exe' : 'program');
const args = process.argv.slice(2);

const result = spawnSync(exe, args, { cwd: root, env, stdio: 'inherit' });
process.exit(result.status ?? 1);
