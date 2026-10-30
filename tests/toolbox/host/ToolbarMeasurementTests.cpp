#include "ToolbarMeasurementRun.hpp"
#include "ToolbarRowMeasurementScene.hpp"
#include "ToolboxHost.hpp"
#include <cassert>
#include <cstring>
using namespace loka::testing;
namespace
{
  struct FakeClock
  {
    unsigned long ticks;
    FakeClock()
        : ticks(0)
    {
    }
    unsigned long now()
    {
      return ++ticks;
    }
  };

  class Receiver : public WholeDrawReceiver
  {
  public:
    Receiver()
        : clock(0)
    {
    }
    virtual unsigned long now() const
    {
      return clock;
    }
    virtual void expectDraw()
    {
      run.expectDraw();
    }
    virtual void accept(const WholeDrawSample &sample)
    {
      run.accept(sample);
    }
    unsigned long clock;
    ToolbarMeasurementRun run;
  };
  void draw(Receiver &receiver, unsigned long paint)
  {
    WholeDrawBracket bracket;
    bracket.beginLayout();
    receiver.clock += 50000; // Layout cannot make a 17 ms paint fail.
    bracket.endLayout();
    bracket.beginErase();
    receiver.clock += paint;
    bracket.finish();
  }
  void ten(Receiver &receiver, unsigned long paint)
  {
    for (int i = 0; i < 10; ++i)
    {
      receiver.run.begin("operation", i);
      draw(receiver, paint);
      receiver.run.close();
    }
  }
} // namespace
int main()
{
  {
    FakeClock clock;
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController platform(&window);
    loka::app::scene::Scene scene((loka::app::scene::Boundary<ToolbarRows>()));
    assert(scene.mount(&platform));
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    std::FILE *out = std::tmpfile();
    assert(out);
    assert(RunToolbarRows(scene, platform, clock, out));
    assert(clock.ticks == 6 * 31 * 2);
    std::rewind(out);
    char line[512];
    unsigned rows = 0, deltas = 0;
    while (std::fgets(line, sizeof(line), out))
    {
      if (std::strstr(line, "toolbar_row case="))
      {
        ++rows;
        assert(std::strstr(line, "repetitions=31 median_us=1 maximum_us=1"));
      }
      if (std::strstr(line, "toolbar_row_delta"))
        ++deltas;
    }
    assert(rows == 6 && deltas == 2);
    std::fclose(out);
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }

  Receiver receiver;
  {
    WholeDrawReceiverScope scope(receiver);
    ten(receiver, 17000);
    assert(receiver.run.passed() && "inclusive threshold and separate layout diagnostics");
    receiver.run.begin("two-draws", 11);
    draw(receiver, 1);
    draw(receiver, 17001);
    receiver.run.close();
    assert(!receiver.run.passed() && "maximum over every draw, not mean or median");
  }
  assert(WholeDrawReceiverSlot() == 0 && "receiver unhooked before window destruction");
  {
    Receiver missing;
    WholeDrawReceiverScope scope(missing);
    ten(missing, 1);
    missing.run.begin("missing-end", 11);
    draw(missing, 1);
    WholeDrawBracket incomplete;
    incomplete.beginLayout();
    incomplete.endLayout();
    incomplete.beginErase();
    missing.run.close();
    assert(!missing.run.passed() && "one missing draw fails even beside a completed draw");
  }
  {
    Receiver missing;
    WholeDrawReceiverScope scope(missing);
    ten(missing, 1);
    missing.run.begin("missing-start", 11);
    WholeDrawBracket incomplete;
    incomplete.beginLayout();
    incomplete.endLayout();
    incomplete.finish();
    missing.run.close();
    assert(!missing.run.passed() && "missing start cannot be interpreted as a fast sample");
  }
  {
    Receiver empty;
    WholeDrawReceiverScope scope(empty);
    ten(empty, 0); // Empty bracket path: two clock reads with no intervening work.
    assert(empty.run.passed() && "empty bracket cost path accepts zero clock delta");
    empty.run.begin("no-draw", 11);
    empty.run.close();
    assert(!empty.run.passed() && "value change without any draw is missing evidence");
  }
  {
    Receiver full;
    WholeDrawReceiverScope scope(full);
    ten(full, 1);
    full.run.begin("overflow", 11);
    for (int i = 0; i < ToolbarMeasurementRun::capacity; ++i)
      draw(full, 1);
    full.run.close();
    assert(!full.run.passed() && "bounded journal overflow refuses the run");
    std::FILE *file = std::tmpfile();
    assert(file && "temporary report opened");
    full.run.write(file);
    std::rewind(file);
    char line[512];
    bool summary = false;
    while (std::fgets(line, sizeof(line), file))
      if (std::strstr(line, "toolbar_summary verdict=FAIL") && std::strstr(line, "overflow=1"))
        summary = true;
    std::fclose(file);
    assert(summary && "machine summary reports failure and overflow");
  }
  return 0;
}
