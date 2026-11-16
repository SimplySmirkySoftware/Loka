#!/usr/bin/env node
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {randomBytes} = require('node:crypto');
const {spawnSync} = require('node:child_process');
const {createRequire} = require('node:module');

const extension = path.resolve(__dirname, '..');
const lock = path.join(extension, '.place.lock');
const identity = 'simplysmirkysoftware.loka-preview';

function entry(file) {
  try { return fs.lstatSync(file); }
  catch (error) { if (error.code === 'ENOENT') return undefined; throw error; }
}
function directory(file) {
  const info = entry(file);
  if (info && (!info.isDirectory() || info.isSymbolicLink()))
    throw new Error('Expected a real directory: ' + file);
  if (!info) fs.mkdirSync(file);
}
/** Both placement and reclamation enter through the same ownership check. */
function destination(root) {
  // The workspace root belongs to the caller: it must already exist and is never created here.
  if (!entry(root)) throw new Error('Workspace root does not exist: ' + root);
  directory(root);
  let current = root;
  for (const name of ['.vscode', 'extensions', 'loka-preview']) {
    current = path.join(current, name);
    directory(current);
  }
  const file = path.join(current, 'package.json');
  const info = entry(file);
  if (info) {
    if (!info.isFile() || info.isSymbolicLink()) throw new Error('Manifest must be a real file');
    const manifest = JSON.parse(fs.readFileSync(file, 'utf8'));
    if (manifest.publisher + '.' + manifest.name !== identity)
      throw new Error('Destination belongs to another extension: ' + current);
    return {directory: current, manifest};
  }
  if (fs.readdirSync(current).some(name => !/^(?:\.stage-|gen-).+/.test(name)))
    throw new Error('Unowned destination contents: ' + current);
  return {directory: current, manifest: undefined};
}
/** Abrupt test interruption deliberately leaves the lock and private stage for recovery. */
function interrupt(point) {
  if (process.env.LOKA_PLACE_TEST_FAIL === point) process.exit(86);
}
function copyPayload(source, target) {
  fs.mkdirSync(target);
  for (const item of fs.readdirSync(source, {withFileTypes: true})) {
    const from = path.join(source, item.name), to = path.join(target, item.name);
    if (item.isDirectory()) copyPayload(from, to);
    else if (item.isFile()) { fs.copyFileSync(from, to); interrupt('mid-copy'); }
    else throw new Error('Payload must contain only real files and directories: ' + from);
  }
}
/** Validate the rewritten manifest in its staged layout, without executing extension code.
 * Emitted CommonJS uses literal require calls; reject computed calls rather than omit them.
 * Each reachable file is visited once, including cycles. Node owns relative resolution. */
function validate(stage, manifest) {
  if (typeof manifest.main !== 'string' || !/^\.\/gen-[\w-]+\/extension\.js$/.test(manifest.main))
    throw new Error('Invalid staged main: ' + manifest.main);
  const payload = path.resolve(stage, path.dirname(manifest.main));
  const visited = new Set();
  function visit(file) {
    file = fs.realpathSync(file);
    if (!file.startsWith(payload + path.sep)) throw new Error('Require escapes staged payload: ' + file);
    if (!fs.statSync(file).isFile()) throw new Error('Not a payload file: ' + file);
    if (visited.has(file)) return;
    visited.add(file);
    if (file.endsWith('.json')) { JSON.parse(fs.readFileSync(file, 'utf8')); return; }
    const source = fs.readFileSync(file, 'utf8');
    for (const call of source.matchAll(/\brequire\s*\(([^)]*)\)/g)) {
      const literal = /^\s*(['"])([^'"\n]+)\1\s*$/.exec(call[1]);
      if (!literal) throw new Error('Computed require in staged payload: ' + file);
      if (literal[2].startsWith('.')) visit(createRequire(file).resolve(literal[2]));
    }
  }
  visit(path.resolve(stage, manifest.main));
}
function clean(owned) {
  console.log('Clean requires all windows for this workspace to be closed.');
  const main = owned.manifest?.main;
  const match = main === undefined ? undefined : /^\.\/(gen-[\w-]+)\/extension\.js$/.exec(main);
  if (owned.manifest && !match) throw new Error('Refusing clean: current generation is not identifiable');
  for (const name of fs.readdirSync(owned.directory)) {
    if (name.startsWith('.stage-') || name.startsWith('gen-') && name !== match?.[1])
      fs.rmSync(path.join(owned.directory, name), {recursive: true, force: true});
  }
}
function main(args) {
  let cleanup = false, payload, root;
  for (let i = 0; i < args.length; i++) {
    if (args[i] === '--clean') cleanup = true;
    else if (args[i] === '--test-payload' && args[i + 1]) payload = path.resolve(args[++i]);
    else if (!args[i].startsWith('-') && !root) root = path.resolve(args[i]);
    else throw new Error('Usage: place.cjs [--clean] [--test-payload directory] <workspace>');
  }
  if (!root || cleanup && payload) throw new Error('Usage: place.cjs [--clean] [--test-payload directory] <workspace>');
  try { fs.mkdirSync(lock); }
  catch (error) {
    if (error.code !== 'EEXIST') throw error;
    throw new Error('Another place is running, or a previous place was interrupted. If none is running, remove ' + lock);
  }
  let stage;
  try {
    const owned = destination(root);
    if (cleanup) { clean(owned); return; }
    if (!payload) {
      const build = spawnSync(process.platform === 'win32' ? 'npm.cmd' : 'npm', ['run', 'build'],
        {cwd: extension, stdio: 'inherit'});
      if (build.error || build.status !== 0) throw new Error('Build failed: ' + (build.error?.message || build.status));
      payload = path.join(extension, 'out');
    }
    interrupt('after-build');
    const generation = `gen-${Date.now()}-${process.pid}-${randomBytes(8).toString('hex')}`;
    stage = fs.mkdtempSync(path.join(owned.directory, '.stage-'));
    copyPayload(payload, path.join(stage, generation));
    const manifest = JSON.parse(fs.readFileSync(path.join(extension, 'package.json'), 'utf8'));
    manifest.main = `./${generation}/extension.js`;
    const stagedManifest = path.join(stage, 'package.json');
    fs.writeFileSync(stagedManifest, JSON.stringify(manifest, null, 2) + '\n');
    validate(stage, JSON.parse(fs.readFileSync(stagedManifest, 'utf8')));
    fs.renameSync(path.join(stage, generation), path.join(owned.directory, generation));
    interrupt('after-publish');
    interrupt('before-commit');
    fs.renameSync(stagedManifest, path.join(owned.directory, 'package.json'));
    interrupt('after-commit');
    const count = fs.readdirSync(owned.directory).filter(name => name.startsWith('gen-')).length;
    console.log(`Placed ${owned.directory}; ${count} generations. If Loka Preview is absent in Extensions, choose Install Workspace Extension; otherwise reopen the window.`
      + (count > 20 ? ' Close all workspace windows and run npm run place:clean.' : ''));
  } finally {
    try { if (stage) fs.rmSync(stage, {recursive: true, force: true}); }
    finally { fs.rmdirSync(lock); }
  }
}
try { main(process.argv.slice(2)); }
catch (error) { console.error('place: ' + error.message); process.exitCode = 1; }
