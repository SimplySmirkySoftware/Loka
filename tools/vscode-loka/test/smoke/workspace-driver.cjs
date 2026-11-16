// Ordinary installed smoke driver, following the probe's serial file mailbox.
const vscode = require('vscode');
const fs = require('node:fs');
const path = require('node:path');
const id = 'simplysmirkysoftware.loka-preview';
exports.activate = context => {
  const box = process.env.LOKA_WORKSPACE_MAILBOX;
  if (!box) return;
  let busy = false;
  const write = (name, value) => {
    const file = path.join(box, name);
    fs.writeFileSync(file + '.tmp', JSON.stringify(value));
    fs.renameSync(file + '.tmp', file);
  };
  const outcome = async operation => {
    try { return {ok: true, value: await operation ?? null}; }
    catch (error) { return {ok: false, error: String(error.message || error)}; }
  };
  const timer = setInterval(async () => {
    if (busy) return;
    busy = true;
    try {
      for (const name of fs.readdirSync(box).filter(n => /^cmd-\d+\.json$/.test(n)).sort()) {
        const response = name.replace('cmd-', 'res-');
        if (fs.existsSync(path.join(box, response))) continue;
        const request = JSON.parse(fs.readFileSync(path.join(box, name), 'utf8'));
        if (request.facts) {
          const extension = vscode.extensions.getExtension(id);
          write(response, {ok: true, value: {trusted: vscode.workspace.isTrusted,
            defined: !!extension, active: extension?.isActive ?? false,
            extensionPath: extension?.extensionPath ?? null,
            folders: (vscode.workspace.workspaceFolders || []).map(f => f.uri.fsPath)}});
        } else {
          const operation = outcome(Promise.resolve().then(() =>
            vscode.commands.executeCommand(request.command, ...(request.args || []))));
          if (request.nowait) {
            operation.then(result => write(name.replace('cmd-', 'settled-'), result));
            write(response, {ok: true, value: 'started'});
          } else write(response, await operation);
        }
      }
    } catch (error) { write('error.json', {error: String(error.stack || error)}); }
    finally { busy = false; }
  }, 100);
  context.subscriptions.push({dispose: () => clearInterval(timer)});
};
