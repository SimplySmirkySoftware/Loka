/** Fixture identities and their distinct layout meanings, shared by both instruments. */
const markers = Object.freeze({
  LokaHelloWeb: {selector: '*', text: 'Loka Sample', leaf: true, meaning: 'inner Box padding'},
  LokaMineWeb: {selector: 'button', text: 'New Game', leaf: false, meaning: 'Window content margin'}
});
const message = 'Build the module JavaScript and Wasm files first';
/** Cost: once per F instrument after Configure; check the two fixture rows. */
function fixture(catalog) {
  if (catalog.version !== 1 || !Array.isArray(catalog.apps)
      || Object.keys(markers).some((id, i) => catalog.apps[i]?.id !== id))
    throw new Error('Fixture premise changed: expected catalog apps[0]/apps[1] ids ' + Object.keys(markers));
  return catalog.apps;
}
/** Cost: once per F instrument; scan the supplied vocabulary rows. */
function skins(vocab) {
  const rows = vocab.webSkins;
  if (!Array.isArray(rows) || new Set(rows.map(r => r.name)).size !== rows.length
      || rows.some(r => !Number.isFinite(r.spacing?.[2]) || typeof r.look?.ground !== 'string'
        || typeof r.look?.face !== 'string' || !Number.isInteger(r.look?.radius)))
    throw new Error('Invalid webSkins spacing or paint');
  const web = rows.find(r => r.name === 'web'), mac = rows.find(r => r.name === 'macos');
  if (!web || !mac || web.spacing[2] === mac.spacing[2]) throw new Error('web and macos margins must differ');
  return rows;
}
/** Cost: plan construction only; one row per bounded move plus open/close. */
function pickerSteps(cell, prefix, target) {
  return [[cell, prefix + 'closed-before'], [cell, prefix + 'open'],
    ...Array.from({length: target}, (_, i) => [cell, prefix + 'next-' + (i + 1)]),
    [cell, prefix + 'closed-after']];
}
/** Cost: one launcher plan; scan skin names twice and allocate its finite step list. */
function plan(rows) {
  const mac = rows.findIndex(r => r.name === 'macos'), web = rows.findIndex(r => r.name === 'web');
  return [['F1', 'ready'], ['F1', 'trusted'], ['F1', 'entry'], ['F1', 'observed', 75000],
    ['F2', 'entry'], ['F2', 'observed', 135000], ['F3', 'entry'], ['F3', 'observed'],
    ['F4', 'entry'], ...pickerSteps('F4', '', mac), ['F4', 'observed'], ...pickerSteps('F4', 'reopen-', 0),
    ['F5', 'entry'], ...pickerSteps('F5', '', 1), ['F5', 'observed'],
    ['F6', 'entry'], ['F6', 'observed', 135000], ['F7', 'entry'], ...pickerSteps('F7', '', web),
    ['F7', 'observed'], ...pickerSteps('F7', 'reopen-', 0)];
}
/** Cost: one selection; one acknowledged observation per move, never wrapping. */
async function picker(vscode, life, step, cell, target, reopen = false) {
  const prefix = reopen ? 'reopen-' : '';
  await step(cell, prefix + 'closed-before');
  const pending = life.command(vscode, cell === 'F5' ? 'loka.preview.selectExample' : 'loka.preview.selectSkin');
  await step(cell, prefix + 'open');
  for (let index = 1; index <= target; index++) {
    await life.command(vscode, 'workbench.action.quickOpenSelectNext');
    await step(cell, prefix + 'next-' + index);
  }
  await life.command(vscode, reopen ? 'workbench.action.closeQuickOpen' : 'workbench.action.acceptSelectedQuickOpenItem');
  await life.limit(pending, 5000, 'Picker command settlement');
  await step(cell, prefix + 'closed-after');
}
module.exports = {markers, message, fixture, skins, plan, picker};
