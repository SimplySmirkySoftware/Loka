card('first', class {
  constructor(c) {
    this.c = c;
    this.shown = c.state(false);
    this.chosen = c.state.file();
    this.picture = c.state.image();
    c.flow(Flow()
      .watch(this.chosen, r => {
        this.shown.set(false);
        if (r.isError) throw new Error('File selection failed');
        return r.isFile ? r.file : Flow.SKIP;
      })
      .step(f => c.native.loadImage(f))
      .step(img => this.picture.set(img)));
  }
  compose() {
    return Box(VStack(
      Button('Open...', () => this.shown.set(true)).TEST_ID('Viewer.Open'),
      Show(this.shown, OpenFileDialog(this.chosen)),
      ImageView(this.picture),
      Button('./MAIN.JS', () => this.c.open('./MAIN.JS')).TEST_ID('SmirkyCard.OpenMain')), {padding: 'standard'});
  }
});
