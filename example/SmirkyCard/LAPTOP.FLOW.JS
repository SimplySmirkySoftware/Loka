// LAPTOP.FLOW.JS - drives LAPTOP.JS through the owner's scenario sequence:
// initial browse state, next machine, open details, back with the selection
// kept, repeat with another machine, then the other presentation.
scenario('first', function (c) {
  function expect(id, text) {
    const actual = c.test.text(id);
    if (actual !== text) throw Error(id + ': expected "' + text + '", got "' + actual + '"');
  }
  // The shipped pictures are 320x200; the Linux harness decodes a fixture of
  // another size and says so through this global before the scenario runs.
  const size = globalThis.LAPTOP_PICTURE_SIZE || {width: 320, height: 200};
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
  function detailShowsPb180c() {
    expect('Laptop.Title', 'PowerBook 180c');
    expect('Laptop.Detail.Name', 'PowerBook 180c');
    expect('Laptop.Detail.Year', '1993');
    expect('Laptop.Detail.cpu', 'CPU: Motorola 68030, 33 MHz + 68882 FPU');
  }
  function openDetail() { c.test.click(listId + 'pb180c'); }
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
      expect(listId + 'macplus', 'Macintosh Plus (1986)');
      expect(listId + 'neo', 'MacBook Neo (2026)');
    }).named('initial-browse')
    .step(function () {
      picture('MACPLUS', 'macplus');
      picture('PB180C', 'pb180c');
      picture('PB550C', 'pb550c');
      picture('PDQ', 'pdq');
      picture('PISMO', 'pismo');
      picture('TIBOOK', 'tibook');
      picture('IBOOKG3', 'ibookg3');
      picture('MB2006', 'mb2006');
      picture('MBP2011', 'mbp2011');
      picture('MBP2015', 'mbp2015');
      picture('MBP2019', 'mbp2019');
      picture('MBP2023', 'mbp2023');
      picture('NEO', 'neo');
    }).named('pictures-loaded');
  if (cell === 'startup') { c.run(flow); return; }
  if (cell === 'laptop-carousel') {
    c.run(flow.step(function () {
      c.test.click('Laptop.Presentation');
    }).named('show-carousel').step(function () {
      expect('Laptop.Presentation', 'Tall');
      expect('Laptop.Heading', 'Macintosh Plus');
      expect('Laptop.Counter', '1 of 13  -  1986');
    }).named('carousel-shows-macplus'));
    return;
  }
  if (cell === 'laptop-detail') {
    c.run(flow.step(openDetail).named('open-detail')
      .step(detailShowsPb180c).named('detail-shows-pb180c'));
    return;
  }
  if (cell !== undefined) throw Error('Unknown LAPTOP_CELL: ' + cell);
  c.run(flow
    // A Show branch materializes when the step settles, so every click that
    // reveals a pane is followed by its expectations in the next step.
    .step(openDetail).named('open-detail')
    .step(function () {
      detailShowsPb180c();
      c.test.click('Laptop.Back');
    }).named('detail-shows-pb180c')
    .step(function () {
      expect('Laptop.Title', 'LaptopTracker');
      expect('Laptop.Presentation', 'Wide');
      expect(listId + 'pb180c', 'PowerBook 180c (1993)');
      c.test.click('Laptop.Presentation');
    }).named('back-to-list')
    .step(function () {
      expect('Laptop.Presentation', 'Tall');
      expect('Laptop.Heading', 'PowerBook 180c');
      expect('Laptop.Counter', '2 of 13  -  1993');
      c.test.click('Laptop.Next');
      expect('Laptop.Heading', 'PowerBook 550c');
      c.test.click('Laptop.Prev');
      c.test.click('Laptop.Prev');
      expect('Laptop.Heading', 'Macintosh Plus');
      c.test.click('Laptop.Prev');
      expect('Laptop.Heading', 'MacBook Neo');
      expect('Laptop.Counter', '13 of 13  -  2026');
    }).named('carousel-keeps-selection-and-wraps')
    .step(function () {
      c.test.click('Laptop.Wide.neo');
    }).named('open-from-carousel')
    .step(function () {
      expect('Laptop.Detail.cpu', 'CPU: Apple A18 Pro, 6-core CPU / 5-core GPU');
      c.test.click('Laptop.Back');
    }).named('detail-shows-neo')
    .step(function () {
      expect('Laptop.Heading', 'MacBook Neo');
      c.test.click('Laptop.Presentation');
    }).named('back-to-carousel')
    .step(function () {
      expect('Laptop.Presentation', 'Wide');
      expect(listId + 'neo', 'MacBook Neo (2026)');
      c.test.log('browse, detail, back and both presentations settled');
    }).named('second-machine'));
});
