// LAPTOP.JS - LaptopTracker: an illustrated guide to Apple's portable Macs.
// Different eras. Different CPUs. Same declarative app.
//
// One card owns everything: five pictures loaded by name at construction,
// the selected index, the browse/detail page and the wide/tall presentation.
// Nothing reloads when a page or presentation switches.

const MACHINES = [
  {id: 'macport', art: 'MACPORT', short: 'Portable', name: 'Macintosh Portable', year: '1989',
   cpu: 'Motorola 68000, 16 MHz', ram: '1 MB (up to 8 MB)',
   display: '9.8-inch 640x400 active-matrix, 1-bit', storage: '40 MB SCSI HDD, 1.44 MB floppy',
   weight: '7.2 kg (15.8 lb)',
   blurb: "Apple's first battery-powered Mac: a desktop-class active-matrix screen in a 7 kg slab, with the trackball beside the keyboard."},
  {id: 'pb100', art: 'PB100', short: 'PB 100', name: 'PowerBook 100', year: '1991',
   cpu: 'Motorola 68HC000, 16 MHz', ram: '2 MB (up to 8 MB)',
   display: '9-inch 640x400 passive-matrix, 1-bit', storage: '20 MB HDD, external floppy',
   weight: '2.3 kg (5.1 lb)',
   blurb: 'A Sony-built shrink of the Portable: the trackball moved to a palm rest in front of the keyboard, the shape every laptop since has copied.'},
  {id: 'pb180c', art: 'PB180C', short: 'PB 180c', name: 'PowerBook 180c', year: '1993',
   cpu: 'Motorola 68030, 33 MHz + 68882 FPU', ram: '4 MB (up to 14 MB)',
   display: '8.4-inch 640x480 active-matrix, 256 colors', storage: '80 or 160 MB HDD',
   weight: '3.2 kg (7.1 lb)',
   blurb: 'The first PowerBook with a 640x480 active-matrix colour panel; the top of the 100-series line.'},
  {id: 'pb550c', art: 'PB550C', short: 'PB 550c', name: 'PowerBook 550c', year: '1995',
   cpu: 'Motorola 68040, 33 MHz (FPU built in)', ram: '8 MB (up to 36 MB)',
   display: '10.4-inch 640x480 active-matrix, 256 colors', storage: '500 or 750 MB HDD',
   weight: '3.3 kg (7.3 lb)',
   blurb: 'Sold only in Japan: a black 500-series body with a full 68040 and the largest 68k PowerBook screen.'},
  {id: 'pb5300c', art: 'PB5300C', short: 'PB 5300c', name: 'PowerBook 5300c/100', year: '1995',
   cpu: 'PowerPC 603e, 100 MHz', ram: '8 or 16 MB (up to 64 MB)',
   display: '10.4-inch 640x480 active-matrix, thousands of colors', storage: '500 or 750 MB HDD',
   weight: '2.8 kg (6.2 lb)',
   blurb: 'The first PowerPC PowerBook: a trackpad instead of a trackball, and a hot-swappable expansion bay.'},
];
const COUNT = MACHINES.length;
const SPECS = [['CPU', 'cpu'], ['RAM', 'ram'], ['Display', 'display'], ['Storage', 'storage'], ['Weight', 'weight']];
function counterText(index) { return (index + 1) + ' of ' + COUNT + '  -  ' + MACHINES[index].year; }
function specText(m, spec) { return spec[0] + ': ' + m[spec[1]]; }
const TITLE = {size: 18, weight: 'bold'};
const LABEL = {size: 10};

card('first', class {
  constructor(c) {
    this.c = c;
    this.index = 0;
    this.pics = MACHINES.map(m => c.state.image(m.art));
    // Named seats so a scenario can ask c.test.imageFacts('pb100').
    MACHINES.forEach((m, i) => { this[m.id] = this.pics[i]; });
    // Page and presentation, as the three things that can be on screen.
    // Depth 8 is the JS tree limit, so the page is not a wrapper seat: each
    // pane has its own Show seat and the handlers keep them consistent.
    this.wide = false;
    this.browse = c.state(true);
    this.detail = c.state(false);
    this.carouselShown = c.state(false);
    this.listShown = c.state(true);
    this.toolbarTitle = c.state('LaptopTracker');
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
    this.toolbarTitle.set('LaptopTracker');
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

  // List and carousel cells have distinct ids throughout the tree. The
  // carousel's cards use a Button: a horizontal ScrollView measures its Row
  // through Text, Button and ImageView widths, and a Cell has no natural width
  // on any rail yet, so a Cell there would collapse the Row to the viewport.
  machineCard(i, width, height, label, testId, asButton) {
    const open = () => this.open(i);
    return Card(VStack(
      ImageView(this.pics[i], {width: width, height: height, fit: 'contain'}),
      (asButton ? Button(label, open) : Cell(label, open)).TEST_ID(testId)));
  }

  toolbar() {
    return Row(
      Show(this.detail, Button('< Back', () => this.back()).TEST_ID('Laptop.Back')),
      Show(this.browse, Button(this.presentationLabel, () => this.togglePresentation()).TEST_ID('Laptop.Presentation')),
      Text(this.toolbarTitle, TITLE).TEST_ID('Laptop.Title'));
  }

  carousel() {
    return VStack(
      Text(this.heading, TITLE, {align: 'center'}).TEST_ID('Laptop.Heading'),
      Text(this.counter, LABEL, {align: 'center'}).TEST_ID('Laptop.Counter'),
      // Arrows move the selection; the viewer scrolls the cards independently.
      Row(
        Button('<', () => this.select(this.index - 1)).TEST_ID('Laptop.Prev'),
        Button('>', () => this.select(this.index + 1)).TEST_ID('Laptop.Next')),
      ScrollView(Row(...MACHINES.map((m, i) =>
        this.machineCard(i, 110, 70, m.short, 'Laptop.Wide.' + m.id, true))).naturalWidth()));
  }

  list() {
    const rows = [];
    for (let i = 0; i < COUNT; i += 2) {
      const label = j => MACHINES[j].name + ' (' + MACHINES[j].year + ')';
      const cards = [this.machineCard(i, 150, 94, label(i), 'Laptop.Open.' + MACHINES[i].id)];
      if (i + 1 < COUNT) cards.push(this.machineCard(i + 1, 150, 94, label(i + 1), 'Laptop.Open.' + MACHINES[i + 1].id));
      rows.push(Row(...cards));
    }
    return ScrollView(VStack(...rows));
  }

  detailPage() {
    return ScrollView(VStack(
      Row(...MACHINES.map((_, i) => Show(this.detailShown[i], ImageView(this.pics[i], {width: 320, height: 200, fit: 'contain'})))),
      Text(this.dName, TITLE).TEST_ID('Laptop.Detail.Name'),
      Text(this.dYear, LABEL).TEST_ID('Laptop.Detail.Year'),
      Text(this.dBlurb, undefined, {wrap: 'word'}).TEST_ID('Laptop.Detail.Blurb'),
      ...this.dSpecs.map((s, j) => Text(s).TEST_ID('Laptop.Detail.' + SPECS[j][1]))));
  }

  compose() {
    return VStack(
      this.toolbar(),
      Show(this.carouselShown, this.carousel()),
      Show(this.listShown, this.list()),
      Show(this.detail, this.detailPage()),
      Text(this.c.error).TEST_ID('SmirkyCard.Status'));
  }
});
