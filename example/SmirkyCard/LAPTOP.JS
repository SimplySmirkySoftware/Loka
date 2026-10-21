// LAPTOP.JS - SmirkyTracker: an illustrated guide to Apple's Macs.
// Different eras. Different CPUs. Same declarative app.
//
// One card owns everything: pictures loaded by name at construction,
// the selected index, the browse/detail page and the wide/tall presentation.
// Nothing reloads when a page or presentation switches.

const BAR = 30;

const MACHINES = [
  {id: 'macplus', art: 'MACPLUS', short: 'Mac Plus', name: 'Macintosh Plus', year: '1986',
   cpu: 'Motorola 68000, 8 MHz', ram: '1 MB (up to 4 MB)',
   display: '9-inch 512x342 monochrome CRT', storage: '800 KB floppy, external SCSI',
   weight: '7.5 kg (16.5 lb)',
   blurb: 'Not in the collection, but used now and then in online emulators for 68000 @ 8 MHz tests.'},
  {id: 'pb180c', art: 'PB180C', short: 'PB 180c', name: 'PowerBook 180c', year: '1993',
   cpu: 'Motorola 68030, 33 MHz + 68882 FPU', ram: '4 MB (up to 14 MB)',
   display: '8.4-inch 640x480 active-matrix, 256 colors', storage: '80 or 160 MB HDD',
   weight: '3.2 kg (7.1 lb)',
   blurb: 'The first PowerBook with a 640x480 active-matrix colour panel; the top of the 100-series line. Bought headless in 2026, without its display; a display is still to be found.'},
  {id: 'pb550c', art: 'PB550C', short: 'PB 550c', name: 'PowerBook 550c', year: '1995',
   cpu: 'Motorola 68040, 33 MHz (FPU built in)', ram: '8 MB (up to 36 MB)',
   display: '10.4-inch 640x480 active-matrix, 256 colors', storage: '500 or 750 MB HDD',
   weight: '3.3 kg (7.3 lb)',
   blurb: 'Sold only in Japan: a black 500-series body with a full 68040 and the largest 68k PowerBook screen.'},
  {id: 'pdq', art: 'PDQ', short: 'PDQ', name: 'PowerBook G3 (PDQ)', year: '1998',
   cpu: 'PowerPC G3, 233/266/300 MHz', ram: '32 MB (up to 192 MB)',
   display: '14.1-inch 1024x768 active-matrix', storage: '2, 4 or 8 GB IDE HDD',
   weight: '3.5 kg (7.8 lb)',
   blurb: 'The last Wallstreet revision: expansion bays and a G3 in a curving black case. In the collection since around 2006.'},
  {id: 'pismo', art: 'PISMO', short: 'Pismo', name: 'PowerBook G3 (Pismo)', year: '2000',
   cpu: 'PowerPC G3, 400 or 500 MHz', ram: '64 or 128 MB (up to 512 MB)',
   display: '14.1-inch 1024x768 active-matrix', storage: '6, 12 or 18 GB ATA HDD',
   weight: '2.8 kg (6.1 lb)',
   blurb: 'FireWire joins the PowerBook. Added in 2026; stands in for the iBook G3 once owned.'},
  {id: 'tibook', art: 'TIBOOK', short: 'TiBook', name: 'PowerBook G4 Titanium', year: '2002',
   cpu: 'PowerPC G4, 1 GHz (now 867 MHz)', ram: '512 MB (up to 1 GB)',
   display: '15.2-inch 1280x854 widescreen', storage: '60 GB Ultra ATA HDD',
   weight: '2.45 kg (5.4 lb)',
   blurb: 'Titanium and a wide screen. Owned since 2022; its logic board failed, so it now runs on an 867 MHz board.'},
  {id: 'ibookg3', art: 'IBOOKG3', short: 'iBook G3', name: 'iBook G3 500 MHz', year: '2001',
   cpu: 'PowerPC G3, 500 MHz', ram: '64 or 128 MB (up to 640 MB)',
   display: '12.1-inch 1024x768 active-matrix', storage: '10 GB Ultra ATA HDD',
   weight: '2.2 kg (4.9 lb)',
   blurb: 'The small white iBook, owned from 2001 to 2011: a decade of G3 computing.'},
  {id: 'mb2006', art: 'MB2006', short: 'MacBook', name: 'MacBook', year: '2006',
   cpu: 'Intel Core Duo, 1.83 or 2 GHz', ram: '512 MB (up to 2 GB)',
   display: '13.3-inch 1280x800 widescreen', storage: '60 or 80 GB SATA HDD',
   weight: '2.36 kg (5.2 lb)',
   blurb: 'The white Intel MacBook, in the collection since 2011.'},
  {id: 'mbp2011', art: 'MBP2011', short: 'MBP 2011', name: 'MacBook Pro 15"', year: '2011',
   cpu: 'Intel Core i7, 2 or 2.2 GHz quad-core', ram: '4 GB (up to 16 GB)',
   display: '15.4-inch 1680x1050 high-resolution anti-glare', storage: '500 or 750 GB SATA HDD',
   weight: '2.54 kg (5.6 lb)',
   blurb: 'My first MacBook Pro, owned since 2011, and the GPU is still fine. This picture also stands for the 2009 15-inch added in 2025 as a spare.'},
  {id: 'mbp2015', art: 'MBP2015', short: 'MBP 2015', name: 'MacBook Pro 13"', year: '2015',
   cpu: 'Intel Core i5, 2.7 GHz dual-core', ram: '8 GB (up to 16 GB)',
   display: '13.3-inch 2560x1600 Retina', storage: '128 GB PCIe flash',
   weight: '1.58 kg (3.48 lb)',
   blurb: 'Bought as a stand-in for the 2011; I used one at work. Its shape already resembles the Apple Silicon MacBook Pro.'},
  {id: 'mbp2019', art: 'MBP2019', short: 'MBP 2019', name: 'MacBook Pro 16"', year: '2019',
   cpu: 'Intel Core i9, 2.3 GHz; Radeon Pro 5600M', ram: '16 GB (up to 64 GB)',
   display: '16-inch 3072x1920 Retina', storage: '1 TB SSD',
   weight: '2.0 kg (4.3 lb)',
   blurb: 'The last Intel generation, owned since 2025. Runs the Tahoe + Windows 10 + Mavericks VMs for Loka around the clock.'},
  {id: 'mbp2023', art: 'MBP2023', short: 'M3 Max', name: 'MacBook Pro M3 Max', year: '2023',
   cpu: 'Apple M3 Max, 14-core CPU / 30-core GPU', ram: '36 GB unified memory',
   display: '16.2-inch 3456x2234 Liquid Retina XDR', storage: '1 TB SSD',
   weight: '2.16 kg (4.8 lb)',
   blurb: 'My first Apple Silicon Mac, bought in 2023 to replace a 2018 13-inch i7. The current main machine.'},
  {id: 'neo', art: 'NEO', short: 'Neo', name: 'MacBook Neo', year: '2026',
   cpu: 'Apple A18 Pro, 6-core CPU / 5-core GPU', ram: '8 GB unified memory',
   display: '13-inch 2408x1506 Liquid Retina', storage: '256 or 512 GB SSD',
   weight: '1.23 kg (2.7 lb)',
   blurb: 'The A18 Pro comes to the Mac in 2026. Had to buy it.'},
];
// The Classic window is 420 px wide; its list remains above this breakpoint.
const NARROW = 320;
const COUNT = MACHINES.length;
const SPECS = [['CPU', 'cpu'], ['RAM', 'ram'], ['Display', 'display'], ['Storage', 'storage'], ['Weight', 'weight']];
function counterText(index) { return (index + 1) + ' of ' + COUNT + '  -  ' + MACHINES[index].year; }
function specText(m, spec) { return spec[0] + ': ' + m[spec[1]]; }
const TITLE = {size: 18, weight: 'bold'};
const LABEL = {size: 10};

card('first', class {
  constructor(c) {
    this.c = c;
    this.aboutShown = c.state(false);
    this.index = 0;
    const mono = c.machine.displayDepth !== null && c.machine.displayDepth < 8;
    this.pics = MACHINES.map(m => c.state.image(mono ? m.art + 'M' : m.art));
    this.thumbs = mono ? MACHINES.map(m => c.state.image(m.art + 'MS')) : this.pics;
    // The smirking compact Mac for About: 160x100 mono or 320x200 colour.
    this.logo = c.state.image(mono ? 'ICONMS' : 'ICON');
    // Named seats so a scenario can ask c.test.imageFacts('pb180c').
    MACHINES.forEach((m, i) => { this[m.id] = this.pics[i]; });
    // Page and presentation, as the three things that can be on screen.
    // Depth 9 is the JS tree limit, so the page is not a wrapper seat: each
    // pane has its own Show seat and the handlers keep them consistent.
    this.wide = false;
    this.browse = c.state(true);
    this.detail = c.state(false);
    this.carouselShown = c.state(false);
    this.listShown = c.state(true);
    this.viewport = c.state.viewport();
    this.narrow = c.state(false);
    this.wideList = c.state(true);
    c.flow(Flow().watch(this.viewport, v => v.width > 0 && v.width < NARROW)
      .onSuccess(narrow => {
        this.narrow.set(narrow);
        this.wideList.set(!narrow);
      }));
    this.toolbarTitle = c.state('SmirkyTracker');
    this.presentationLabel = c.state('Wide');
    // Seats materialize after the constructor, so the first machine's values
    // are initial values here; select() writes the same seats later.
    const first = MACHINES[0];
    this.counter = c.state(counterText(0));
    this.heading = c.state(first.name);
    this.detailShown = MACHINES.map((_, i) => c.state(i === 0));
    this.dName = c.state(first.name);
    this.dYear = c.state(first.year);
    this.dBlurb = c.state(first.blurb);
    this.dSpecs = SPECS.map(spec => c.state(specText(first, spec)));
  }

  select(i) {
    this.index = (i + COUNT) % COUNT;
    const m = MACHINES[this.index];
    this.counter.set(counterText(this.index));
    this.heading.set(m.name);
    this.detailShown.forEach((s, i) => s.set(i === this.index));
    this.dName.set(m.name);
    this.dYear.set(m.year);
    this.dBlurb.set(m.blurb);
    this.dSpecs.forEach((s, j) => s.set(specText(m, SPECS[j])));
  }

  open(i) {
    this.select(i);
    this.toolbarTitle.set(MACHINES[this.index].name);
    this.browse.set(false);
    this.carouselShown.set(false);
    this.listShown.set(false);
    this.detail.set(true);
  }

  back() {
    this.toolbarTitle.set('SmirkyTracker');
    this.detail.set(false);
    this.showBrowse();
  }

  showBrowse() {
    this.carouselShown.set(this.wide);
    this.listShown.set(!this.wide);
    this.browse.set(true);
  }

  togglePresentation() {
    this.wide = !this.wide;
    this.presentationLabel.set(this.wide ? 'Tall' : 'Wide');
    this.showBrowse();
  }

  // List and carousel cards have distinct ids throughout the tree. The click
  // target is a Button on both: a Cell fills whatever height its column has
  // left (the first list row took the whole viewport), and a horizontal
  // ScrollView measures its Row through Text, Button and ImageView widths,
  // which a Cell does not answer on any rail yet.
  machineCard(i, width, height, label, testId) {
    return Card(VStack(
      ImageView(this.thumbs[i], {width: width, height: height, fit: 'contain'}),
      Button(label, () => this.open(i)).TEST_ID(testId)));
  }

  toolbar() {
    // Three short buttons share the left half; the title keeps its half
    // of the 420 px Classic window. Main replaces the longer ./MAIN.JS label.
    return Box(Row(
      Row(
        Show(this.detail, Button('< Back', () => this.back()).TEST_ID('Laptop.Back')),
        Show(this.browse, Button(this.presentationLabel, () => this.togglePresentation()).TEST_ID('Laptop.Presentation')),
        Button('Main', () => this.c.open('./MAIN.JS')).TEST_ID('SmirkyCard.OpenMain'),
        Button('About', () => this.aboutShown.set(true)).TEST_ID('Laptop.About')),
      Text(this.toolbarTitle, TITLE).TEST_ID('Laptop.Title')), {height: BAR});
  }

  carousel() {
    return ZStack(VStack(
      Box(Text(''), {height: BAR}),
      Text(this.heading, TITLE, {align: 'center'}).TEST_ID('Laptop.Heading'),
      Text(this.counter, LABEL, {align: 'center'}).TEST_ID('Laptop.Counter'),
      // Arrows move the selection; the viewer scrolls the cards independently.
      Row(
        Button('<', () => this.select(this.index - 1)).TEST_ID('Laptop.Prev'),
        Button('>', () => this.select(this.index + 1)).TEST_ID('Laptop.Next')),
      ScrollView(Row(...MACHINES.map((m, i) =>
        this.machineCard(i, 110, 70, m.short, 'Laptop.Wide.' + m.id))).naturalWidth())), this.toolbar()).expBlurUnit();
  }

  list() {
    const rows = [];
    const label = j => MACHINES[j].name + ' (' + MACHINES[j].year + ')';
    for (let i = 0; i < COUNT; i += 2) {
      const cards = [this.machineCard(i, 150, 94, label(i), 'Laptop.Open.' + MACHINES[i].id)];
      // A Row shares its width equally, so an odd last card gets an empty
      // partner and keeps the same width as every other card.
      cards.push(i + 1 < COUNT ? this.machineCard(i + 1, 150, 94, label(i + 1), 'Laptop.Open.' + MACHINES[i + 1].id)
                               : Box(Text('')));
      rows.push(Show(this.wideList, Row(...cards)));
    }
    // Seven wide rows and seven narrow pairs: 14 children, leaves at depth 10.
    for (let i = 0; i < COUNT; i += 2) {
      const cards = [this.machineCard(i, 150, 94, label(i), 'Laptop.Narrow.' + MACHINES[i].id)];
      if (i + 1 < COUNT) cards.push(this.machineCard(i + 1, 150, 94, label(i + 1), 'Laptop.Narrow.' + MACHINES[i + 1].id));
      rows.push(Show(this.narrow, VStack(...cards)));
    }
    return ZStack(
      ScrollView(VStack(...rows).naturalHeight(), {viewport: this.viewport, insets: {top: BAR}}),
      this.toolbar()).expBlurUnit();
  }

  detailPage() {
    return ZStack(ScrollView(VStack(
      Row(...MACHINES.map((_, i) => Show(this.detailShown[i], ImageView(this.pics[i], {width: 320, height: 200, fit: 'contain'})))),
      Text(this.dName, TITLE).TEST_ID('Laptop.Detail.Name'),
      Text(this.dYear, LABEL).TEST_ID('Laptop.Detail.Year'),
      Text(this.dBlurb, undefined, {wrap: 'word'}).TEST_ID('Laptop.Detail.Blurb'),
      ...this.dSpecs.map((s, j) => Text(s).TEST_ID('Laptop.Detail.' + SPECS[j][1]))), {insets: {top: BAR}}),
      this.toolbar()).expBlurUnit();
  }

  about() {
    // A Card roughly centred on the 420x340 Classic window, over the
    // static-blur backdrop that fills the ZStack.
    return VStack(
      Box(Text(''), {height: 60}),
      Row(
        Box(Text(''), {width: 40}),
        Box(Card(VStack(
          ImageView(this.logo, {width: 160, height: 100, fit: 'contain'}).TEST_ID('Laptop.About.Logo'),
          Text('SmirkyTracker', TITLE).TEST_ID('Laptop.About.Name'),
          Text('(c) cubenoy22 w/ Cafe Loka Crew (Codex & Claude Code)', undefined, {wrap: 'word'})
            .TEST_ID('Laptop.About.Credit'),
          Button('OK!', () => this.aboutShown.set(false)).TEST_ID('Laptop.About.OK'))),
          {width: 310})).naturalWidth());
  }

  compose() {
    return ZStack(
      VStack(
        Show(this.carouselShown, this.carousel()),
        Show(this.listShown, this.list()),
        Show(this.detail, this.detailPage()),
        Text(this.c.error).TEST_ID('SmirkyCard.Status')),
      Show(this.aboutShown, this.about())).expStaticBlurUnit();
  }

});
