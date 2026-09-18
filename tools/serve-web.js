#!/usr/bin/env node
//
// Serve web/ over HTTP for the browser visualiser.
//
//     npm start
//     node tools/serve-web.js 8080
//
// Python's `http.server` does the same job in one line, and this exists next to
// it rather than instead of it because the page needs two things that server
// does not guarantee: `application/wasm` on glitchglimmer.wasm, since the
// wrong type stops the browser streaming-compiling the module, and no caching
// anywhere, because build-wasm.sh replaces the module and the frame recordings
// under the same paths and a cached copy of either is indistinguishable from a
// build that did not land.
//
// No dependencies, so `npm start` works on a fresh clone without `npm install`.

const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..', 'web');
const port = Number(process.argv[2] || process.env.PORT || 8000);

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.bin': 'application/octet-stream',
  '.f32': 'application/octet-stream',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.map': 'application/json; charset=utf-8',
};

function notFound(res, target) {
  const body = `Not found: ${target}\n\n` +
    (target.endsWith('.wasm') || target.includes('/live/')
      ? 'The live view needs web/live/glitchglimmer.wasm, which is generated.\n' +
        'Run tools/build-wasm.sh first.\n'
      : 'The frame player needs web/data/, which is generated.\n' +
        'Run `.pio/build/native/program --dump-frames web/data` first.\n');
  res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end(body);
}

const server = http.createServer((req, res) => {
  if (req.method !== 'GET' && req.method !== 'HEAD') {
    res.writeHead(405, { Allow: 'GET, HEAD' });
    res.end();
    return;
  }

  let target;
  try {
    target = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
  } catch {
    res.writeHead(400);
    res.end('Malformed URL\n');
    return;
  }

  let file = path.resolve(ROOT, '.' + target);
  // A decoded `..` escapes ROOT above, so check the resolved path rather than
  // the request text.
  if (file !== ROOT && !file.startsWith(ROOT + path.sep)) {
    res.writeHead(403);
    res.end('Forbidden\n');
    return;
  }

  let stat;
  try {
    stat = fs.statSync(file);
  } catch {
    notFound(res, target);
    return;
  }
  if (stat.isDirectory()) {
    file = path.join(file, 'index.html');
    if (!fs.existsSync(file)) {
      notFound(res, target);
      return;
    }
  }

  const type = TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream';
  res.writeHead(200, {
    'Content-Type': type,
    'Content-Length': fs.statSync(file).size,
    'Cache-Control': 'no-store',
  });
  if (req.method === 'HEAD') {
    res.end();
    return;
  }
  fs.createReadStream(file).pipe(res);
});

server.on('error', (err) => {
  console.error(err.code === 'EADDRINUSE'
    ? `Port ${port} is in use. Try: npm start -- 8001`
    : err.message);
  process.exit(1);
});

server.listen(port, '127.0.0.1', () => {
  console.log(`GlitchGlimmer visualiser on http://127.0.0.1:${port}`);
  console.log('Recording mode reads web/data/, live mode reads web/live/.');
});
