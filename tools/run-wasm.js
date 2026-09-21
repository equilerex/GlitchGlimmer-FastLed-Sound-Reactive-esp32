const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const version = fs.readFileSync(path.join(__dirname, 'emscripten-version.txt'), 'utf8').trim();
const localSdk = path.join(root, '.tools', 'emsdk');

function firstExisting(candidates) {
  return candidates.find((candidate) => fs.existsSync(candidate));
}

function sdkDir() {
  return firstExisting([
    process.env.EMSDK,
    localSdk,
    path.join(os.homedir(), 'emsdk'),
    'D:\\emsdk'
  ].filter(Boolean));
}

function fail(message) {
  console.error(message);
  console.error(`Run: npm run setup:wasm  (installs Emscripten ${version} under .tools/emsdk)`);
  process.exit(1);
}

const sdk = sdkDir();
const args = process.argv.slice(2).map((arg) => {
  // The old documented form was `npm run wasm -- --watch`; npm passes the
  // separator away, but retaining this makes direct calls unsurprising.
  return arg === '--' ? null : arg;
}).filter(Boolean);

if (process.platform === 'win32') {
  if (!sdk || !fs.existsSync(path.join(sdk, 'emsdk_env.bat'))) {
    fail('Emscripten SDK not found.');
  }
  // Use Git Bash for the build on Windows. Calling emsdk_env.bat through cmd
  // cannot reliably preserve its generated POSIX paths when npm launches it,
  // while the repository build is already a Bash script. Git Bash is also what
  // provides find, xargs and the shell features used by build-wasm.sh.
  const drive = sdk[0].toLowerCase();
  const bashSdk = `/${drive}/${sdk.slice(3).replaceAll('\\', '/')}`;
  const source = `${bashSdk}/emsdk_env.sh`;
  const quotedArgs = args.map((arg) => `'${arg.replaceAll("'", "'\\''")}'`).join(' ');
  const command = `source '${source.replaceAll("'", "'\\''")}' && exec bash tools/build-wasm.sh ${quotedArgs}`;
  const bash = process.env.GIT_BASH ||
    (fs.existsSync('C:\\Program Files\\Git\\bin\\bash.exe')
      ? 'C:\\Program Files\\Git\\bin\\bash.exe'
      : 'bash');
  const result = spawnSync(bash, ['-lc', command], {
    cwd: root,
    stdio: 'inherit',
    windowsHide: true
  });
  process.exit(result.status ?? 1);
}

if (!sdk || !fs.existsSync(path.join(sdk, 'emsdk_env.sh'))) {
  fail('Emscripten SDK not found.');
}
const quotedArgs = args.map((arg) => `'${arg.replaceAll("'", "'\\''")}'`).join(' ');
const command = `source '${path.join(sdk, 'emsdk_env.sh').replaceAll("'", "'\\''")}' && exec bash tools/build-wasm.sh ${quotedArgs}`;
const result = spawnSync('bash', ['-lc', command], { cwd: root, stdio: 'inherit' });
process.exit(result.status ?? 1);
