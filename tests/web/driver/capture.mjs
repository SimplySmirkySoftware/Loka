#!/usr/bin/env node
import {spawn, spawnSync} from 'node:child_process';
import {createServer} from 'node:http';
import {lstat, mkdir, mkdtemp, readFile, realpath, rename, rm, stat} from 'node:fs/promises';
import {extname, isAbsolute, join, relative, resolve, sep} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

const repo = fileURLToPath(new URL('../../../', import.meta.url));
const hostPath = '/__loka_capture.html';

// Also used on realpath results so a symlink cannot escape the served tree.
export function containedPath(root, file) {
  const tail = relative(root, file);
  return tail !== '' && tail !== '..' && !tail.startsWith('..' + sep) && !isAbsolute(tail);
}

export function resolveBuildPath(root, request) {
  let path;
  try { path = decodeURIComponent(request.split('?')[0]); }
  catch { return null; }
  if (!path.startsWith('/') || path.includes('\\') || path.includes('\0') || path.split('/').includes('..')) return null;
  const file = resolve(root, '.' + path);
  return containedPath(root, file) ? file : null;
}

export function capturePlan(catalog, skins, origin) {
  if (catalog.version !== 1 || !Array.isArray(catalog.apps)) throw new Error('Invalid Web catalog v1');
  if (!Array.isArray(skins) || !skins.length) throw new Error('Missing webSkins');
  const ids = new Set();
  for (const app of catalog.apps) {
    if (typeof app.id !== 'string' || typeof app.exportName !== 'string'
        || !/^[A-Za-z0-9_]+$/.test(app.id) || ids.has(app.id)
        || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(app.exportName)
        || typeof app.js !== 'string' || !app.js.endsWith('.js') || app.js.startsWith('/')
        || app.js.includes('\\') || app.js.split('/').includes('..')) throw new Error('Invalid catalog app: ' + app.id);
    ids.add(app.id);
  }
  const names = skins.map(skin => skin.name);
  if (names.some(name => typeof name !== 'string' || !/^[a-z0-9_]+$/.test(name))
      || new Set(names).size !== names.length) throw new Error('Invalid webSkins names');
  return catalog.apps.flatMap(app => names.map(skin => ({
    app, skin,
    url: origin + hostPath + '?' + new URLSearchParams({app: app.id, skin}),
    outputName: app.id + '-' + skin + '.png'
  })));
}

export function hostPage(app, skin) {
  // JSON string encoding plus '<' escaping keeps catalog text inside the script.
  const literal = value => JSON.stringify(value).replaceAll('<', '\\u003c');
  const source = '/' + app.js.split('/').map(encodeURIComponent).join('/');
  return `<!doctype html><html><head><meta charset="utf-8">
<style>html,body{margin:0;overflow:hidden}*{scrollbar-width:none}*::-webkit-scrollbar{display:none}</style>
</head><body><script>
(async () => {
  let failed = false;
  const fail = error => {
    failed = true;
    document.body.setAttribute('data-loka-capture', 'failed');
    document.body.setAttribute('data-loka-error', String(error));
  };
  window.addEventListener('error', event => fail(event.error || event.message));
  window.addEventListener('unhandledrejection', event => fail(event.reason));
  try {
    await new Promise((resolve, reject) => {
      const script = document.createElement('script');
      script.src = ${literal(source)};
      script.onload = resolve;
      script.onerror = () => reject(new Error('Module script failed to load'));
      document.head.appendChild(script);
    });
    const module = await window[${literal(app.exportName)}]({lokaSkin: ${literal(skin)}});
    const result = module.web.start();
    if (result !== 0) throw new Error('web.start() failed: ' + result);
    await document.fonts.ready;
    if (!failed) document.body.setAttribute('data-loka-capture', 'ready');
  } catch (error) { fail(error); }
})();
</script></body></html>`;
}

export async function runBrowser(chromium, args, signal, timeoutMs = 30000) {
  signal.throwIfAborted();
  return new Promise((resolve, reject) => {
    const child = spawn(chromium, args, {
      detached: process.platform !== 'win32', stdio: ['ignore', 'pipe', 'ignore']
    });
    const chunks = [];
    let bytes = 0, failure;
    const stop = error => {
      failure ||= error;
      if (!child.pid) return;
      // POSIX owns the launcher group. Windows descendant cleanup is unsupported.
      try {
        if (process.platform === 'win32') child.kill('SIGKILL');
        else process.kill(-child.pid, 'SIGKILL');
      } catch (error) { if (error.code !== 'ESRCH') failure = error; }
    };
    const abort = () => stop(signal.reason);
    const timer = setTimeout(() => stop(new Error('Chromium timed out')), timeoutMs);
    signal.addEventListener('abort', abort, {once: true});
    child.stdout.on('data', chunk => {
      if (failure) return;
      bytes += chunk.length;
      if (bytes > 8 * 1024 * 1024) stop(new Error('Chromium stdout exceeded 8 MiB'));
      else chunks.push(chunk);
    });
    child.once('error', error => {
      failure ||= error.code === 'ENOENT' ? new Error('Chromium missing: ' + chromium) : error;
    });
    child.once('close', (code, killedBy) => {
      clearTimeout(timer);
      signal.removeEventListener('abort', abort);
      if (failure) reject(failure);
      else if (code !== 0) reject(new Error('Chromium exited with ' + (killedBy || code)));
      else resolve(Buffer.concat(chunks).toString('utf8'));
    });
  });
}

async function main() {
  if (process.argv.length > 3) throw new Error('Usage: node tests/web/driver/capture.mjs [web build dir]');
  const requestedBuild = resolve(process.argv[2] || join(repo, 'build/wasm/web'));
  const output = join(repo, 'build/web-captures');
  await mkdir(join(repo, 'build'), {recursive: true});
  if (await realpath(join(repo, 'build')) !== join(repo, 'build')
      || (await lstat(output).catch(error => {
        if (error.code === 'ENOENT') return null;
        throw error;
      }))?.isSymbolicLink()) throw new Error('Capture output directory must not be a symlink');
  const cancellation = new AbortController();
  const cancel = () => cancellation.abort(new Error('Capture interrupted'));
  process.on('SIGINT', cancel);
  process.on('SIGTERM', cancel);
  try {
    // Invalidate before catalog/browser checks, including missing-input failures.
    await rm(output, {recursive: true, force: true});
    const catalogPath = join(requestedBuild, 'loka-web-apps.json');
    const catalog = JSON.parse(await readFile(catalogPath, 'utf8').catch(error => {
      throw new Error('Cannot read Web catalog ' + catalogPath + ': ' + error.message);
    }));
    const build = await realpath(requestedBuild);
    const skins = JSON.parse(await readFile(join(repo, 'tools/style-vocab.json'), 'utf8')).webSkins;
    capturePlan(catalog, skins, ''); // Validate before acquiring resources.
    // Same executable lookup as run.mjs; absence is an error for a capture request.
    const chromium = process.env.CHROMIUM || ['chromium', 'chromium-browser', 'google-chrome']
      .find(name => spawnSync('which', [name], {stdio: 'ignore'}).status === 0);
    if (!chromium) throw new Error('Chromium missing; set CHROMIUM to its executable');
    cancellation.signal.throwIfAborted();
    await mkdir(output, {recursive: true});
    const server = createServer(async (req, res) => {
      try {
        if (req.method !== 'GET' && req.method !== 'HEAD') { res.writeHead(405); res.end(); return; }
        const url = new URL(req.url, 'http://127.0.0.1');
        let data, type;
        if (req.url.split('?')[0] === hostPath) {
          const app = catalog.apps.find(item => item.id === url.searchParams.get('app'));
          const skin = url.searchParams.get('skin');
          if (!app || !skins.some(item => item.name === skin)) throw new Error('Unknown capture');
          data = hostPage(app, skin); type = 'text/html; charset=utf-8';
        } else {
          const path = resolveBuildPath(build, req.url);
          if (!path) throw new Error('Refused path');
          const file = await realpath(path);
          if (!containedPath(build, file) || !(await stat(file)).isFile()) throw new Error('Refused file');
          data = await readFile(file);
          type = {'.js': 'application/javascript', '.wasm': 'application/wasm', '.css': 'text/css'}[extname(file)] || 'application/octet-stream';
        }
        res.writeHead(200, {'Content-Type': type}); res.end(req.method === 'HEAD' ? undefined : data);
      } catch { res.writeHead(404); res.end(); }
    });
    try {
      await new Promise((resolve, reject) => {
        server.once('error', reject); server.listen(0, '127.0.0.1', resolve);
      });
      const plan = capturePlan(catalog, skins, 'http://127.0.0.1:' + server.address().port);
      for (const capture of plan) {
        cancellation.signal.throwIfAborted();
        // Pipeline images, even temporary ones, stay under build/.
        const staging = await mkdtemp(join(output, '.capture-'));
        try {
          const profile = await mkdtemp(join(tmpdir(), 'loka-web-capture-'));
          try {
            const png = join(staging, 'capture.png');
            const stdout = await runBrowser(chromium, ['--headless', '--no-sandbox', '--disable-gpu',
              '--hide-scrollbars', '--window-size=420,330', '--force-device-scale-factor=1',
              '--user-data-dir=' + profile, '--virtual-time-budget=5000', '--dump-dom',
              '--screenshot=' + png, capture.url], cancellation.signal);
            if (!/<body\b[^>]*\bdata-loka-capture="ready"/.test(stdout)) {
              const error = stdout.match(/\bdata-loka-error="([^"]*)"/);
              throw new Error(capture.outputName + ': ' + (error ? error[1] : 'Module not ready within 5000 ms virtual time'));
            }
            const size = await stat(png).catch(() => null);
            if (!size || !size.isFile() || size.size === 0) throw new Error('Capture missing or empty: ' + capture.outputName);
            cancellation.signal.throwIfAborted();
            await rename(png, join(output, capture.outputName));
            console.log('Wrote build/web-captures/' + capture.outputName);
          } finally {
            await rm(profile, {recursive: true, force: true, maxRetries: 3});
          }
        } finally {
          await rm(staging, {recursive: true, force: true});
        }
      }
    } finally {
      server.closeAllConnections();
      await new Promise(resolve => server.close(resolve));
    }
    cancellation.signal.throwIfAborted();
  } catch (error) {
    await rm(output, {recursive: true, force: true});
    throw error;
  } finally {
    process.off('SIGINT', cancel);
    process.off('SIGTERM', cancel);
  }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().catch(error => { console.error('Web capture: ' + error.message); process.exitCode = 1; });
}
