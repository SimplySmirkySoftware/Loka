import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {cp, mkdir, readFile, rename, rm, writeFile} from 'node:fs/promises';
import {dirname, join, resolve} from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const extension = resolve(here, '../..');
const installCommand = 'workbench.extensions.action.installWorkspaceRecommendedExtensions';
export const normalStatus = 'Loka: Web catalog missing; run Configure';
export const markedStatus = 'LokaW16: Web catalog missing; run Configure';
// Pin-dependent DOM protocol from the workspace-extension probe: this command needs this pane.
export const recommendationExpression = `(() => [...document.querySelectorAll('.extensions-viewlet .pane')].some(p =>
  p.offsetParent !== null && p.querySelector('.pane-header')?.textContent.includes('Workspace Recommendations') &&
  [...p.querySelectorAll('.monaco-list-row')].some(r => r.offsetParent !== null && r.textContent.includes('Loka Preview') &&
    [...r.querySelectorAll('.action-label')].some(a => a.offsetParent !== null && !a.classList.contains('hide') &&
      (a.innerText.trim() || a.getAttribute('aria-label') || a.title) === 'Install Workspace Extension'))))()`;
/** Failure evidence only: what the Extensions view showed when the expected row never appeared. */
export const recommendationDiagnostic = `JSON.stringify({view: !!document.querySelector('.extensions-viewlet'),
  panes: [...document.querySelectorAll('.extensions-viewlet .pane')].map(p => ({
    header: (p.querySelector('.pane-header')?.textContent || '').trim().slice(0, 60), shown: p.offsetParent !== null,
    expanded: p.querySelector('.pane-header')?.getAttribute('aria-expanded'),
    rows: [...p.querySelectorAll('.monaco-list-row')].slice(0, 6).map(r => r.textContent.trim().slice(0, 90))})),
  message: (document.querySelector('.extensions-viewlet .message')?.textContent || '').trim().slice(0, 120),
  dialogs: [...document.querySelectorAll('.monaco-dialog-box')].map(d => d.textContent.trim().slice(0, 120))})`;
export const cancelInstallExpression = `(() => {
  const dialog = [...document.querySelectorAll('.monaco-dialog-box')].find(d => d.offsetParent !== null &&
    [...d.querySelectorAll('.monaco-button')].some(b => b.textContent.trim() === 'Trust Workspace & Install'));
  const cancel = dialog && [...dialog.querySelectorAll('.monaco-button')].find(b => b.textContent.trim() === 'Cancel');
  if (!cancel) return false; cancel.click(); return true;
})()`;

export function workspaceArgs(directory, workspace, shared = join(directory, 'shared')) {
  return [`--user-data-dir=${directory}/user`, `--extensions-dir=${directory}/ext`,
    `--shared-data-dir=${shared}`, '--disable-gpu', '--remote-debugging-port=0',
    '--skip-welcome', '--skip-release-notes', '--disable-telemetry', '--disable-updates', workspace];
}
export function checkWorkspaceFacts(facts, workspace, {trusted, defined, active = false}) {
  assert.equal(facts.trusted, trusted, 'Workspace Trust');
  assert.equal(facts.defined, defined, 'workspace extension presence');
  assert.equal(facts.active, active, 'workspace extension activation');
  assert.deepEqual(facts.folders, [workspace], 'workspace folder');
  assert.equal(facts.extensionPath, defined ? join(workspace, '.vscode/extensions/loka-preview') : null,
    'extension must be loaded from placed copy');
}
async function optionalJSON(file) {
  try { return JSON.parse(await readFile(file, 'utf8')); }
  catch (error) { if (error.code === 'ENOENT') return undefined; throw error; }
}
/** One read per checkpoint; only this cell's workspace storage rows, never global user state. */
export function installation(directory, workspace) {
  const result = spawnSync('python3', ['-c', `import json, pathlib, sqlite3, sys
rows = []
for folder in pathlib.Path(sys.argv[1]).glob('User/workspaceStorage/*'):
    metadata = folder / 'workspace.json'
    database = folder / 'state.vscdb'
    if not metadata.exists() or not database.exists(): continue
    if json.loads(metadata.read_text()).get('folder') != sys.argv[2]: continue
    with sqlite3.connect(database.as_uri() + '?mode=ro', uri=True) as connection:
        for row in connection.execute("select value from ItemTable where key = 'workspaceExtensions.locations'"):
            rows.extend(json.loads(row[0]))
print(json.dumps(rows))`, join(directory, 'user'), pathToFileURL(workspace).href], {encoding: 'utf8', timeout: 5000});
  assert.ifError(result.error);
  assert.equal(result.status, 0, 'Read workspace install record: ' + (result.error || result.stderr));
  return JSON.parse(result.stdout);
}
/** Placement is bounded by the rig; source build and commit remain under place.cjs's lock. */
function place(workspace, payload) {
  const result = spawnSync(process.execPath, [join(extension, 'scripts/place.cjs'),
    ...(payload ? ['--test-payload', payload] : []), workspace], {encoding: 'utf8', timeout: 60000});
  assert.ifError(result.error);
  assert.equal(result.status, 0, 'Placement: ' + (result.error || result.stdout + result.stderr));
}
async function fixture(directory) {
  const workspace = join(directory, 'ws');
  await mkdir(join(workspace, '.vscode'), {recursive: true});
  await mkdir(join(directory, 'user/User'), {recursive: true});
  const driver = join(directory, 'ext/loka-smoke.workspace-driver-0.0.1');
  await mkdir(driver, {recursive: true});
  await cp(join(here, 'workspace-driver.cjs'), join(driver, 'driver.cjs'));
  await writeFile(join(driver, 'package.json'), JSON.stringify({name: 'workspace-driver', publisher: 'loka-smoke',
    version: '0.0.1', engines: {vscode: '^1.143.0'}, main: './driver.cjs', activationEvents: ['*'],
    extensionKind: ['workspace', 'ui'], capabilities: {untrustedWorkspaces: {supported: true}}}));
  await writeFile(join(directory, 'user/User/settings.json'), JSON.stringify({'git.enabled': false,
    'security.workspace.trust.enabled': true, 'security.workspace.trust.startupPrompt': 'never',
    'workbench.startupEditor': 'none', 'extensions.autoUpdate': false, 'extensions.autoCheckUpdates': false}));
  await writeFile(join(workspace, '.vscode/settings.json'), JSON.stringify({
    'loka.preview.buildDirectory': join(directory, 'missing-build')}));
  // No extensions.json: recommendations must come from the local placed directory.
  return workspace;
}
/** Launch borrows RunOwner's process/CDP lifetime; every restart first waits for graceful exit.
 * Mailbox sequence is session-local, with at most one awaited request at a time. */
async function session(owner, executable, env, display, directory, workspace, phase, shared, helpers) {
  const box = join(directory, 'mailbox-' + phase);
  await mkdir(box);
  await rm(join(directory, 'user/DevToolsActivePort'), {force: true});
  const childEnv = {...env, ...display, LOKA_WORKSPACE_MAILBOX: box};
  delete childEnv.WAYLAND_DISPLAY; delete childEnv.ELECTRON_RUN_AS_NODE;
  const args = workspaceArgs(directory, workspace, shared);
  console.log(phase + ': argv=' + JSON.stringify([executable, ...args]));
  owner.startCode(executable, args, childEnv);
  const cdp = await helpers.connectCDP(owner, directory, Date.now() + 40000);
  let sequence = 0;
  const read = async name => {
    const error = await optionalJSON(join(box, 'error.json'));
    if (error) throw new Error(error.error);
    return optionalJSON(join(box, name));
  };
  const request = async value => {
    const number = String(++sequence).padStart(4, '0');
    const file = join(box, `cmd-${number}.json`);
    await writeFile(file + '.tmp', JSON.stringify(value)); await rename(file + '.tmp', file);
    const result = await owner.wait(() => read(`res-${number}.json`), Date.now() + 30000, 'driver ' + JSON.stringify(value));
    return {...result, number};
  };
  const command = async (command, args = [], nowait = false) => {
    const result = await request({command, args, nowait});
    assert.equal(result.ok, true, result.error);
    return result;
  };
  const facts = async () => {
    const result = await request({facts: true}); assert.equal(result.ok, true, result.error); return result.value;
  };
  return {facts, request, command, cdp,
    async recommendations() {
      await command('workbench.extensions.search', ['@recommended ']);
      try {
        // 90 s: the first hosted-runner launch of a fresh ordinary profile once took longer than 30 s to list the row.
        await owner.wait(() => cdp.evaluate(recommendationExpression), Date.now() + 90000, 'Workspace Recommendations Loka Preview row');
      } catch (error) {
        const seen = await cdp.evaluate(recommendationDiagnostic).catch(e => 'diagnostic failed: ' + e.message);
        throw new Error(error.message + '; view=' + seen);
      }
    },
    async trust() {
      await command('workbench.trust.manage');
      await owner.wait(() => cdp.evaluate(helpers.trustExpression), Date.now() + 10000, 'Trust button');
      await owner.wait(async () => (await facts()).trusted, Date.now() + 10000, 'trusted state');
      await command('workbench.action.closeModalEditor');
    },
    async opened(expected) {
      await command('loka.preview.open');
      checkWorkspaceFacts(await facts(), workspace, {trusted: true, defined: true, active: true});
      await owner.wait(async () => await cdp.evaluate(helpers.statusExpression) === expected,
        Date.now() + 10000, 'workspace status ' + expected);
    },
    async settled(number) {
      return owner.wait(() => read(`settled-${number}.json`), Date.now() + 10000, 'install cancellation settled');
    },
    async quit() {
      // The acknowledgement may race the process exit; only graceful exit is the completion fact.
      await command('workbench.action.quit', [], true).catch(error => {
        if (!owner.code.outcome && owner.code.child.exitCode === null) throw error;
      });
      const result = await owner.limit(owner.code.closed, 15000, 'workspace VS Code exit');
      assert.equal(result.code, 0, 'graceful VS Code exit');
      owner.cdp.close(); owner.cdp = undefined; owner.code = undefined;
    }
  };
}

/** W and WR own only fresh fixtures. Existing R/T/K/F launch behavior stays in run.mjs. */
export async function runWorkspaceCells(owner, executable, env, display, helpers) {
  const directory = join(owner.directory, 'W'), workspace = await fixture(directory);
  const expectedRecord = ['.vscode/extensions/loka-preview'];
  let phase = 'W3';
  try {
    let s = await session(owner, executable, env, display, directory, workspace, 'W-install', undefined, helpers);
    checkWorkspaceFacts(await s.facts(), workspace, {trusted: false, defined: false});
    place(workspace);
    await s.trust(); await s.recommendations(); await s.command(installCommand);
    await owner.wait(async () => (await s.facts()).defined, Date.now() + 15000, 'installed extension');
    await s.opened(normalStatus);
    await s.quit();
    assert.deepEqual(installation(directory, workspace), expectedRecord, 'installed record');
    console.log('[pass] W3: installed ordinary workspace extension and activated placed copy');

    phase = 'W4';
    const payload = join(directory, 'marked-payload');
    await cp(join(extension, 'out'), payload, {recursive: true});
    const wire = join(payload, 'src/wire.js');
    const original = await readFile(wire, 'utf8');
    assert.equal(original.split("'Loka: '").length, 2, 'one status prefix in built payload');
    await writeFile(wire, original.replace("'Loka: '", "'LokaW16: '"));
    place(workspace, payload);
    s = await session(owner, executable, env, display, directory, workspace, 'W-update', undefined, helpers);
    await s.opened(markedStatus);
    await s.quit();
    assert.deepEqual(installation(directory, workspace), expectedRecord, 'record persists without reinstall');
    console.log('[pass] W4: marked update after restart without reinstall');
    console.log('[pass] W');

    phase = 'WR1';
    const restricted = join(owner.directory, 'WR'), restrictedWorkspace = await fixture(restricted);
    place(restrictedWorkspace, join(extension, 'out'));
    s = await session(owner, executable, env, display, restricted, restrictedWorkspace, 'WR-gate', undefined, helpers);
    checkWorkspaceFacts(await s.facts(), restrictedWorkspace, {trusted: false, defined: false});
    await s.recommendations();
    const started = await s.command(installCommand, [], true);
    await owner.wait(() => s.cdp.evaluate(cancelInstallExpression), Date.now() + 10000, 'Trust Workspace & Install / Cancel');
    await s.settled(started.number);
    checkWorkspaceFacts(await s.facts(), restrictedWorkspace, {trusted: false, defined: false});
    await s.quit();
    assert.deepEqual(installation(restricted, restrictedWorkspace), [], 'cancel leaves no installation');
    console.log('[pass] WR1: untrusted install cancelled at VS Code trust gate');

    phase = 'WR2';
    s = await session(owner, executable, env, display, directory, workspace, 'WR-installed', join(directory, 'shared-untrusted'), helpers);
    checkWorkspaceFacts(await s.facts(), workspace, {trusted: false, defined: false});
    const refused = await s.request({command: 'loka.preview.open'});
    assert.equal(refused.ok, false); assert.match(refused.error, /command 'loka\.preview\.open' not found/);
    assert.equal(await s.cdp.evaluate(helpers.statusExpression), '', 'no Loka status in Restricted Mode');
    await s.quit();
    assert.deepEqual(installation(directory, workspace), expectedRecord, 'untrusted installed record persists');
    console.log('[pass] WR2: installed copy disabled without trust');
    console.log('[pass] WR');
  } catch (error) {
    if (phase.startsWith('W') && !phase.startsWith('WR')) console.log('[not run] WR: W positive control failed');
    throw new Error(phase + ': ' + error.message);
  }
}
