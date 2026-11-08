/** Browser-only cells. Served by the driver; no test access is shipped in the module. */
window.runTextCells = async function() {
  const check = (condition, message) => { if (!condition) throw new Error(message); };
  const normal = fixture.textTest;
  check(normal.service() !== null, 'admitted service');
  check(!normal.service().canvas.isConnected, 'canvas stays detached');
  const face = new FontFace('Loka Text Probe', 'url(text-probe.ttf)');
  document.fonts.add(await face.load());
  const widths = [];
  for (const lang of ['ja', 'zh', 'tr']) {
    document.documentElement.lang = lang;
    const service = normal.admit();
    check(service && service.lang === lang, 'language admission '+lang);
    const env = service.environment(16);
    document.documentElement.lang = 'en';
    const fresh = service.environment(8);
    fresh.applyToCanvas(service.ctx);
    check(service.ctx.lang === lang, 'new key retains admitted language');
    for (const polluted of [false, true]) {
      const parent = document.createElement('div');
      if (polluted) parent.style.cssText = 'text-transform:uppercase;letter-spacing:2px;font-synthesis:none;text-autospace:normal;direction:rtl;';
      const el = document.createElement('span'); el.textContent = 'aaaa';
      parent.appendChild(el); document.body.appendChild(parent);
      for (const key of [16, 20]) {
        const resolved = service.environment(key);
        resolved.applyToCanvas(service.ctx); resolved.applyToElement(el);
        // Fixture substitutes its original regular-only face on BOTH targets after Env application.
        service.ctx.font = service.ctx.font.replace('system-ui', '"Loka Text Probe"');
        el.style.fontFamily = '"Loka Text Probe"';
        const advance = service.ctx.measureText(el.textContent).width;
        const range = document.createRange(); range.selectNodeContents(el);
        check(Math.abs(range.getBoundingClientRect().width-advance)<1, 'canvas/element advance '+lang);
        if (!polluted && key === 16) widths.push(advance);
        check(el.lang === lang, 'element fixed language');
      }
      parent.remove();
    }
    check(service.environment(16) === env, 'Env identity stable');
  }
  check(new Set(widths).size === 3, 'language positive control: three distinct advances '+widths);
  const element = document.querySelector('span[data-loka-token]');
  const before = element.style.cssText;
  check(normal.badDescriptor(+element.dataset.lokaToken) === false, 'release-active protocol refusal');
  check(element.style.cssText === before, 'protocol refusal preserves element');
  return {widths, cells: ['polluted ancestor', 'language positive control', 'regular-only bold synthesis',
    'detached canvas', 'language fixed after first key', 'protocol refusal']};
};
