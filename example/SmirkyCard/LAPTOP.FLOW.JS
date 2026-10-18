// LAPTOP.FLOW.JS - drives LAPTOP.JS through the owner's scenario sequence:
// initial browse state, next machine, open details, back with the selection
// kept, repeat with another machine, then the other presentation.
scenario('first', function (c) {
  function expect(id, text) {
    const actual = c.test.text(id);
    if (actual !== text) throw Error(id + ': expected "' + text + '", got "' + actual + '"');
  }
  // The shipped pictures are 160x100; the Linux harness decodes a fixture of
  // another size and says so through this global before the scenario runs.
  const size = globalThis.LAPTOP_PICTURE_SIZE || {width: 160, height: 100};
  function picture(stem, id) {
    const facts = c.test.imageFacts(id);
    c.test.log('picture.' + stem + ' ' + (facts.empty ? 'empty' : facts.width + 'x' + facts.height));
    if (facts.empty || facts.width !== size.width || facts.height !== size.height)
      throw Error(stem + ' did not load as ' + size.width + 'x' + size.height);
  }
  // Null reports a 100-wide seat; the harness requires the one-column arm.
  // Classic keeps the existing two-column ids at its 420-pixel window size.
  const listId = globalThis.LAPTOP_NARROW_LIST ? 'Laptop.Narrow.' : 'Laptop.Open.';
  const cell = globalThis.LAPTOP_CELL;
  function detailShowsPb100() {
    expect('Laptop.Title', 'PowerBook 100');
    expect('Laptop.Detail.Name', 'PowerBook 100');
    expect('Laptop.Detail.Year', '1991');
    expect('Laptop.Detail.cpu', 'CPU: Motorola 68HC000, 16 MHz');
  }
  function openDetail() { c.test.click(listId + 'pb100'); }
  let flow = Flow()
    .step(function () {
      // The card's own status line, or the refusal UI's: either way the audit
      // carries the message when the card did not compose.
      c.test.log('status ' + c.test.text('SmirkyCard.Status'));
    }).named('status')
    .step(function () {
      // Tall list is the default; the carousel branch is not materialized yet.
      expect('Laptop.Title', 'LaptopTracker');
      expect('Laptop.Presentation', 'Wide');
      expect(listId + 'macport', 'Macintosh Portable (1989)');
      expect(listId + 'pb5300c', 'PowerBook 5300c/100 (1995)');
    }).named('initial-browse')
    .step(function () {
      picture('MACPORT', 'macport');
      picture('PB100', 'pb100');
      picture('PB180C', 'pb180c');
      picture('PB550C', 'pb550c');
      picture('PB5300C', 'pb5300c');
    }).named('pictures-loaded');
  if (cell === 'startup') { c.run(flow); return; }
  if (cell === 'laptop-carousel') {
    c.run(flow.step(function () {
      c.test.click('Laptop.Presentation');
    }).named('show-carousel').step(function () {
      expect('Laptop.Presentation', 'Tall');
      expect('Laptop.Heading', 'Macintosh Portable');
      expect('Laptop.Counter', '1 of 5  -  1989');
    }).named('carousel-shows-macport'));
    return;
  }
  if (cell === 'laptop-detail') {
    c.run(flow.step(openDetail).named('open-detail')
      .step(detailShowsPb100).named('detail-shows-pb100'));
    return;
  }
  if (cell !== undefined) throw Error('Unknown LAPTOP_CELL: ' + cell);
  c.run(flow
    // A Show branch materializes when the step settles, so every click that
    // reveals a pane is followed by its expectations in the next step.
    .step(openDetail).named('open-detail')
    .step(function () {
      detailShowsPb100();
      c.test.click('Laptop.Back');
    }).named('detail-shows-pb100')
    .step(function () {
      expect('Laptop.Title', 'LaptopTracker');
      expect('Laptop.Presentation', 'Wide');
      expect(listId + 'pb100', 'PowerBook 100 (1991)');
      c.test.click('Laptop.Presentation');
    }).named('back-to-list')
    .step(function () {
      expect('Laptop.Presentation', 'Tall');
      expect('Laptop.Heading', 'PowerBook 100');
      expect('Laptop.Counter', '2 of 5  -  1991');
      c.test.click('Laptop.Next');
      expect('Laptop.Heading', 'PowerBook 180c');
      c.test.click('Laptop.Prev');
      c.test.click('Laptop.Prev');
      expect('Laptop.Heading', 'Macintosh Portable');
      c.test.click('Laptop.Prev');
      expect('Laptop.Heading', 'PowerBook 5300c/100');
      expect('Laptop.Counter', '5 of 5  -  1995');
    }).named('carousel-keeps-selection-and-wraps')
    .step(function () {
      c.test.click('Laptop.Wide.pb5300c');
    }).named('open-from-carousel')
    .step(function () {
      expect('Laptop.Detail.cpu', 'CPU: PowerPC 603e, 100 MHz');
      c.test.click('Laptop.Back');
    }).named('detail-shows-pb5300c')
    .step(function () {
      expect('Laptop.Heading', 'PowerBook 5300c/100');
      c.test.click('Laptop.Presentation');
    }).named('back-to-carousel')
    .step(function () {
      expect('Laptop.Presentation', 'Wide');
      expect(listId + 'pb5300c', 'PowerBook 5300c/100 (1995)');
      c.test.log('browse, detail, back and both presentations settled');
    }).named('second-machine'));
});
