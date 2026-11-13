const fs = require('node:fs/promises');
const {join} = require('node:path');

// One writer per file; rename publishes a completed value to the launcher.
async function publish(directory, name, value) {
  await fs.writeFile(join(directory, name + '.tmp'), JSON.stringify(value));
  await fs.rename(join(directory, name + '.tmp'), join(directory, name));
}

// The grant budget begins only after the launcher can act on ready.
exports.grantTrust = async function (vscode, ready, clock = globalThis) {
  let subscription, timer;
  try {
    const granted = new Promise(resolve => {
      subscription = vscode.workspace.onDidGrantWorkspaceTrust(resolve);
    });
    // Subscription setup can reject before command preparation finishes.
    granted.catch(() => {});
    await vscode.commands.executeCommand('workbench.trust.manage');
    await ready();
    await Promise.race([granted, new Promise((_, reject) => {
      timer = clock.setTimeout(() => reject(new Error('Trust grant timed out')), 20000);
    })]);
  } finally {
    clock.clearTimeout(timer);
    subscription?.dispose();
  }
};

exports.run = async function () {
  const vscode = require('vscode');
  const directory = process.env.LOKA_SMOKE_CELL;
  const result = {};
  try {
    result.initialTrusted = vscode.workspace.isTrusted;
    result.trustEnabled = vscode.workspace.getConfiguration('security.workspace.trust').get('enabled');
    const extension = vscode.extensions.getExtension('simplysmirkysoftware.loka-preview');
    if (!extension) throw new Error('Extension not found');
    await extension.activate();
    result.active = extension.isActive;
    result.commands = await vscode.commands.getCommands(true);
    result.folders = (vscode.workspace.workspaceFolders || []).map(folder => folder.uri.fsPath);
    result.buildDirectory = vscode.workspace.getConfiguration('loka.preview',
      vscode.workspace.workspaceFolders?.[0]?.uri).get('buildDirectory');
    result.trustGranted = false;
    if (process.env.LOKA_SMOKE_KIND !== 'R') {
      await exports.grantTrust(vscode, () => publish(directory, 'ready', true));
      result.trustGranted = true;
    }
    await vscode.commands.executeCommand('loka.preview.open');
    // The command returns before the tab model reports the new panel; give it a bounded moment.
    const tabs = () => vscode.window.tabGroups.all.flatMap(group => group.tabs.map(tab => tab.label));
    for (const until = Date.now() + 5000; !tabs().includes('Loka Preview') && Date.now() < until;) {
      await new Promise(resolve => setTimeout(resolve, 50));
    }
    result.tabs = tabs();
    result.finalTrusted = vscode.workspace.isTrusted;
    await publish(directory, 'result.json', result);
    const deadline = Date.now() + 30000;
    for (;;) {
      try { await fs.access(join(directory, 'release')); break; }
      catch (error) { if (error.code !== 'ENOENT') throw error; }
      if (Date.now() >= deadline) throw new Error('Release timed out');
      await new Promise(resolve => setTimeout(resolve, 50));
    }
  } catch (error) {
    result.error = String(error.stack || error);
    await publish(directory, 'result.json', result);
    throw error;
  }
};
