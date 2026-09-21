const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const sdk = path.join(root, '.tools', 'emsdk');
const version = fs.readFileSync(path.join(__dirname, 'emscripten-version.txt'), 'utf8').trim();

function run(command, args) {
  let executable = command;
  let spawnArgs = args;
  let shell = false;
  if (process.platform === 'win32' && command.toLowerCase().endsWith('.bat')) {
    const quote = (value) => `"${String(value).replaceAll('"', '""')}"`;
    executable = [quote(command), ...args.map(quote)].join(' ');
    spawnArgs = [];
    shell = true;
  }
  const result = spawnSync(executable, spawnArgs, {
    cwd: root, stdio: 'inherit', windowsHide: true, shell,
  });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
}

fs.mkdirSync(path.dirname(sdk), { recursive: true });
if (!fs.existsSync(path.join(sdk, '.git'))) {
  console.log(`Cloning emsdk into ${sdk}`);
  run('git', ['clone', '--depth', '1', 'https://github.com/emscripten-core/emsdk.git', sdk]);
}

const emsdk = path.join(sdk, process.platform === 'win32' ? 'emsdk.bat' : 'emsdk');
run(emsdk, ['install', version]);
run(emsdk, ['activate', version]);
console.log(`Emscripten ${version} is ready. Run: npm run wasm`);
