// Finds PlatformIO without depending on the terminal's PATH. `pip install` puts
// `pio` in a per-user Scripts directory that is often not on PATH in the terminal
// that ran it, and the PlatformIO installer keeps its own copy under ~/.platformio.
// Both are tried after the PATH names, and every attempt is reported when none works.

const fs = require('fs');
const os = require('os');
const path = require('path');
const { spawnSync } = require('child_process');

const win = process.platform === 'win32';

function candidates() {
  const home = os.homedir();
  const list = [['pio', []], ['platformio', []]];
  list.push(win ? ['python', ['-m', 'platformio']] : ['python3', ['-m', 'platformio']]);
  list.push(win ? ['py', ['-3', '-m', 'platformio']] : ['python', ['-m', 'platformio']]);

  // The PlatformIO installer's own environment.
  const penv = path.join(home, '.platformio', 'penv', win ? 'Scripts' : 'bin');
  list.push([path.join(penv, win ? 'pio.exe' : 'pio'), []]);
  list.push([path.join(penv, win ? 'python.exe' : 'python'), ['-m', 'platformio']]);

  // pip install --user: %APPDATA%\Python\PythonNNN\Scripts on Windows.
  if (win && process.env.APPDATA) {
    const base = path.join(process.env.APPDATA, 'Python');
    let versions = [];
    try { versions = fs.readdirSync(base).sort().reverse(); } catch (_) { /* none */ }
    for (const v of versions) list.push([path.join(base, v, 'Scripts', 'pio.exe'), []]);
  } else if (!win) {
    list.push([path.join(home, '.local', 'bin', 'pio'), []]);
  }
  return list;
}

// Returns { command, prefix } or null. `tried` receives one line per attempt.
function findPlatformio(cwd, env, tried) {
  for (const [command, prefix] of candidates()) {
    const isPath = path.isAbsolute(command);
    if (isPath && !fs.existsSync(command)) {
      if (tried) tried.push(command + ' (does not exist)');
      continue;
    }
    // shell: true for the bare names, so .cmd and .bat shims resolve on Windows. An
    // absolute path is quoted, since the repository and the profile may have spaces.
    const cmd = isPath && win ? '"' + command + '"' : command;
    const probe = spawnSync(cmd, [...prefix, '--version'], { cwd, env, stdio: 'ignore', shell: win });
    if (probe.status === 0) return { command: cmd, prefix };
    if (tried) tried.push([command, ...prefix].join(' ') + ' (exit ' + probe.status + ')');
  }
  return null;
}

module.exports = { findPlatformio };
