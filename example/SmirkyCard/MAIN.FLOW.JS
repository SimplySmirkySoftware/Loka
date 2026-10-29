// The measurement vehicle enters the incrementing expression before starting this companion.
// Product MAIN.JS composition; each RunScript publishes a different Result.
scenario('first', function (c) {
  let flow = Flow();
  for (let i = 1; i <= 10; ++i) {
    flow = flow.step(function () {
      c.test.click('SmirkyCard.RunScript');
      const actual = c.test.text('SmirkyCard.Result');
      if (actual !== String(i + 100)) throw Error('Result did not change: ' + actual);
    }).named('run-script-' + i);
  }
  c.run(flow);
});
