// MAIN.JS - the built-in cards. Each card is a class: the constructor
// declares its seats with c.state(initial); compose() returns the card's
// tree using the Loka Node DSL names.
card('first', class {
  constructor(c) {
    this.c = c;
    this.script = c.state('1+1');
    this.result = c.state('Ready');
  }
  compose() {
    return Box(
        VStack(Markup('<size=18><b>Card One</b></size>', undefined, {align : 'center'}).TEST_ID('SmirkyCard.Title'),
               Markup('This Scene is defined in JavaScript. Edit the expression below and run it to explore.',
                      undefined,
                      {wrap : 'word'}),
               Markup('<i>A little code, a new idea.</i>', undefined, {align : 'right'}),
               EditText(this.script).TEST_ID('SmirkyCard.Script'),
               Button('Run',
                      () => {
                        try
                        {
                          this.result.set(String((0, eval)(this.script.get())));
                        }
                        catch (e)
                        {
                          this.result.set('Error: ' + e);
                        }
                      })
                   .TEST_ID('SmirkyCard.RunScript'),
               Text(this.result).TEST_ID('SmirkyCard.Result'),
               Button('Run JavaScript', () => this.c.go('second')).TEST_ID('SmirkyCard.Run'),
               Button('./MINES.JS', () => this.c.open('./MINES.JS')).TEST_ID('SmirkyCard.OpenMines'),
               Button('./VIEWER.JS', () => this.c.open('./VIEWER.JS')).TEST_ID('SmirkyCard.OpenViewer'),
               Button('./LAPTOP.JS', () => this.c.open('./LAPTOP.JS')).TEST_ID('SmirkyCard.OpenLaptop'),
               Button('Reload MAIN.JS', () => this.c.reload()).TEST_ID('SmirkyCard.Reload'),
               Text(this.c.error).TEST_ID('SmirkyCard.Status')),
        {padding : 'standard'});
  }
});
card('second', class {
  constructor(c) {
    this.c = c;
    this.script = c.state('2+2');
    this.result = c.state('Ready');
  }
  compose() {
    return Box(VStack(Markup('<size=18><b>Card Two</b></size>').TEST_ID('SmirkyCard.Title'),
                      Text('This Scene is also defined in JavaScript.'),
                      EditText(this.script).TEST_ID('SmirkyCard.Script'),
                      Row(Button('Run',
                                 () => {
                                   try
                                   {
                                     this.result.set(String((0, eval)(this.script.get())));
                                   }
                                   catch (e)
                                   {
                                     this.result.set('Error: ' + e);
                                   }
                                 })
                              .TEST_ID('SmirkyCard.RunScript'),
                          Text(this.result).TEST_ID('SmirkyCard.Result'),
                          Button('Run JavaScript', () => this.c.go('first')).TEST_ID('SmirkyCard.Run')),
                      Text(this.c.error).TEST_ID('SmirkyCard.Status')),
               {padding : 'standard'});
  }
});
