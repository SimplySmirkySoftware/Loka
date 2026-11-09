/** Cost: style boundary once per render; reads nine borrowed heap words, retains no view. */
LokaWebStyle.decodeTextDescriptor = function(heap, ptr, count) {
  const w = this.wire;
  if (!Number.isInteger(count) || count !== w.count || (ptr >>> 0) !== ptr || ptr % 4 !== 0
      || ptr > heap.byteLength - count * 4) return null;
  const words = new Int32Array(heap.buffer, heap.byteOffset + ptr, count);
  if (words[w.fields.version] !== w.version || words[w.fields.count] !== count) return null;
  const d = {};
  for (const name of Object.keys(w.fields)) d[name] = words[w.fields[name]];
  const tags = w.ranges.sizeKind;
  if (!Object.values(tags).includes(d.sizeKind)) return null;
  if (d.sizeKind === tags.NONE && d.sizePayload !== 0) return null;
  if (d.sizeKind === tags.POINTS && (d.sizePayload < -32768 || d.sizePayload > 32767)) return null;
  if (d.sizeKind === tags.ROLE && !w.ranges.sizePayload.ROLE.includes(d.sizePayload)) return null;
  for (const name of ['weight', 'italic', 'family', 'underline', 'color'])
    if (!w.ranges[name].includes(d[name])) return null;
  return Object.freeze(d);
};
/** Cost: after decode once per render; six generated size comparisons, no owner rows. */
LokaWebStyle.fontKey = function(d) {
  const points = d.sizeKind === 0 ? 12 : d.sizeKind === 2 ? 10 : d.sizePayload;
  // Deliberate cross-language twin: common/app/style/Style.cpp SnapTextSizePoints, ties low.
  let index = 0;
  for (let i = 1; i < this.sizes.length; ++i)
    if (Math.abs(points - this.sizes[i]) < Math.abs(points - this.sizes[index])) index = i;
  return index * 8 + (d.weight === this.weights.BOLD ? 4 : 0)
    + (d.italic === 1 ? 2 : 0) + (d.family === this.family.MONOSPACE ? 1 : 0);
};

/** Session service factory. Cost: admission once; its own fixed property table, one detached canvas.
 * Rows carry canvas accessor, CSS declaration, resolved value and non-default canvas probe.
 * Font is first because its shorthand resets longhands. No line-height belongs to this table. */
var LokaWebText = (() => {
  const table = Object.freeze([
    ['font', 'font', null, '17px serif'],
    ['fontKerning', 'font-kerning', 'normal', 'none'],
    ['letterSpacing', 'letter-spacing', '0px', '1px'],
    ['wordSpacing', 'word-spacing', '0px', '1px'],
    ['fontVariantCaps', 'font-variant-caps', 'normal', 'small-caps'],
    ['fontStretch', 'font-stretch', 'normal', 'condensed'],
    ['textRendering', 'text-rendering', 'auto', 'optimizeSpeed'],
    ['direction', 'direction', 'ltr', 'rtl'],
    ['lang', null, null, 'tr'],
    [null, 'unicode-bidi', 'normal'],
    [null, 'font-synthesis', 'initial'],
    [null, 'font-size-adjust', 'none'],
    [null, 'font-feature-settings', 'normal'],
    [null, 'font-variation-settings', 'normal'],
    [null, 'font-optical-sizing', 'auto'],
    [null, 'text-transform', 'none'],
    [null, 'writing-mode', 'horizontal-tb'],
    [null, 'text-orientation', 'mixed'],
    [null, 'text-indent', '0'],
    [null, 'text-spacing-trim', 'normal'],
    [null, 'text-autospace', 'normal'],
    [null, 'text-size-adjust', 'none', null, '-webkit-text-size-adjust'],
    [null, 'font-palette', 'normal'],
    [null, 'white-space', 'pre']
  ].map(Object.freeze));
  /** Cost: environment miss once per key (<=48); constructs its own fixed table of values. */
  function environment(key, lang) {
    const font = (key & 2 ? 'italic ' : '') + (key & 4 ? 'bold ' : '')
      + LokaWebStyle.sizes[Math.floor(key / 8)] + 'px ' + (key & 1 ? 'monospace' : 'system-ui');
    const values = Object.freeze(table.map(row => row[0] === 'font' ? font : row[0] === 'lang' ? lang : row[2]));
    return Object.freeze({
      /** Cost: service caller once per canvas use; this Env's fixed rows only. */
      applyToCanvas(ctx) {
        table.forEach((row, i) => { if (row[0]) ctx[row[0]] = values[i]; });
      },
      /** Cost: style boundary once per render; this Env's fixed rows, one owned element. */
      applyToElement(el) {
        table.forEach((row, i) => {
          if (row[0] === 'lang') el.setAttribute('lang', values[i]);
          else {
            el.style.setProperty(row[1], values[i]);
            if (row[4]) el.style.setProperty(row[4], values[i]);
          }
        });
      }
    });
  }
  return Object.freeze({
    /** Cost: bootstrap once; private service table probes and one metric probe, no DOM rows. */
    admit(fingerprint, count, document, navigator, css, log) {
      const refuse = reason => { log('[web-text] refused: ' + reason); return null; };
      if ((fingerprint >>> 0) !== (LokaWebStyle.wire.fingerprint >>> 0)
          || count !== LokaWebStyle.wire.count) return refuse('wire');
      const validLang = value => typeof value === 'string' && value.trim() && value.trim().toLowerCase() !== 'inherit';
      const lang = validLang(document.documentElement.lang) ? document.documentElement.lang.trim()
        : validLang(navigator.language) ? navigator.language.trim() : 'en';
      try {
        const canvas = document.createElement('canvas');
        const ctx = canvas.getContext('2d');
        if (!ctx || !css || typeof css.supports !== 'function') return refuse('canvas');
        const env = environment(16, lang);
        for (const row of table) {
          if (row[0]) {
            if (!(row[0] in ctx)) return refuse(row[0] === 'lang' ? 'lang' : 'canvas');
            ctx[row[0]] = row[3];
            if (ctx[row[0]] !== row[3]) return refuse(row[0] === 'lang' ? 'lang' : 'canvas');
          }
          if (row[1] && !css.supports(row[1], row[0] === 'font' ? '12px system-ui' : row[2])
              && !(row[4] && css.supports(row[4], row[2]))) return refuse('canvas');
        }
        env.applyToCanvas(ctx);
        const metrics = ctx.measureText('x');
        if (!Number.isFinite(metrics.fontBoundingBoxAscent) || !Number.isFinite(metrics.fontBoundingBoxDescent))
          return refuse('canvas');
        const environments = new Map();
        const fontMetrics = new Map();
        return Object.freeze({canvas, ctx, lang,
          /** Cost: metrics query once; one own font-key lookup, canvas probe only on a miss. */
          metrics(key) {
            if (ctx.isContextLost()) return null;
            if (fontMetrics.has(key)) return fontMetrics.get(key);
            this.environment(key).applyToCanvas(ctx);
            const m=ctx.measureText('x'), a=m.fontBoundingBoxAscent, d=m.fontBoundingBoxDescent;
            if (!Number.isFinite(a) || !Number.isFinite(d) || a<0 || d<0) return null;
            const ascent=Math.ceil(a), descent=Math.ceil(d), height=ascent+descent;
            if (height<1 || height>32767) return null;
            const value=Object.freeze({ascent,descent});
            fontMetrics.set(key,value); return value;
          },
          /** Cost: style caller once per render; one lookup in this service's <=48 Env rows. */
          environment(key) {
            if (!Number.isInteger(key) || key < 0 || key >= LokaWebStyle.sizes.length * 8)
              throw new Error('invalid font key');
            if (!environments.has(key)) environments.set(key, environment(key, lang));
            return environments.get(key);
          }
        });
      } catch (_) { return refuse('canvas'); }
    }
  });
})();
