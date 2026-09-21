const { spawnSync } = require('node:child_process');

for (const script of ['setup-native.js', 'setup-emscripten.js']) {
  const result = spawnSync(process.execPath, [`tools/${script}`], { stdio: 'inherit' });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
}

console.log('Project toolchains are ready. Run npm run native or npm run wasm.');
