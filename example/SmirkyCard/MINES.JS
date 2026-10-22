let random = (globalThis.SMIRKY_SEED === undefined ? Date.now() : globalThis.SMIRKY_SEED) >>> 0;
function nextIndex() {
  random = (Math.imul(random, 1664525) + 1013904223) >>> 0;
  return (random >>> 16) % 64;
}

function neighbors(index) {
  const result = [];
  const row = Math.floor(index / 8), col = index % 8;
  for (let dy = -1; dy <= 1; ++dy)
    for (let dx = -1; dx <= 1; ++dx) {
      const y = row + dy, x = col + dx;
      if ((dx || dy) && y >= 0 && y < 8 && x >= 0 && x < 8)
        result.push(y * 8 + x);
    }
  return result;
}

card('first', class {
  constructor(c) {
    this.c = c;
    const next = c.test ? () => Math.floor(c.test.random() * 64) : nextIndex;
    this.mines = Array(64).fill(false);
    for (let placed = 0; placed < 10;) {
      const index = next();
      if (!this.mines[index]) {
        this.mines[index] = true;
        ++placed;
      }
    }
    this.cells = Array.from({length: 64}, () => c.state('.'));
    this.mode = c.state('Flag mode: off');
    this.status = c.state('Mines left: 10');
  }

  click(index) {
    if (this.status.get() === 'Boom' || this.status.get() === 'You win') return;
    const label = this.cells[index].get();
    if (this.mode.get() === 'Flag mode: on') {
      if (label !== '.' && label !== 'F') return;
      this.cells[index].set(label === '.' ? 'F' : '.');
    } else {
      if (label !== '.') return;
      if (this.mines[index]) {
        this.cells.forEach((cell, i) => { if (this.mines[i]) cell.set('X'); });
        this.status.set('Boom');
        return;
      }
      const pending = [index];
      while (pending.length) {
        const i = pending.pop();
        if (this.cells[i].get() !== '.') continue;
        const nearby = neighbors(i);
        const count = nearby.filter(j => this.mines[j]).length;
        this.cells[i].set(count ? String(count) : '');
        if (!count) pending.push(...nearby);
      }
    }
    const won = this.cells.every((cell, i) => this.mines[i] || !['.', 'F'].includes(cell.get()));
    const flags = this.cells.filter(cell => cell.get() === 'F').length;
    this.status.set(won ? 'You win' : 'Mines left: ' + (10 - flags));
  }

  compose() {
    return Box(
        VStack(
            Text(this.status).TEST_ID('Mines.Status'),
            Text(this.c.error).TEST_ID('SmirkyCard.Status'),
            Row(Button(this.mode,
                       () => this.mode.set(this.mode.get() === 'Flag mode: off' ? 'Flag mode: on' : 'Flag mode: off'))
                    .TEST_ID('Mines.Flag'),
                Button('New Game', () => this.c.go('first')).TEST_ID('Mines.NewGame'),
                Button('./MAIN.JS', () => this.c.open('./MAIN.JS')).TEST_ID('SmirkyCard.OpenMain')),
            Grid(8, 8, this.cells.map((seat, i) => Cell(seat, () => this.click(i)).TEST_ID('Mines.Cell.' + i)))
                .TEST_ID('Mines.Board'))
            .TEST_ID('Mines.Content'),
        {padding : 'standard'});
  }
});
