#include "MainNode.hpp"
#include "app/Menu.hpp"

#include "app/nodes/Text.hpp"
#include "app/core/Window.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/StringUTF8.hpp"

#include <new>
#include <cstdio>
#include <cstdlib>

namespace helloworld
{
  using namespace loka::core;
  enum
  {
    kMainPanelsScrollTag = 1,
    kDecorationTag = 2
  };
  // The panels keep a gap from the scroll bar; ScrollView offers its content
  // the full width up to the bar on every rail (#1040).
  enum
  {
    kContentInset = 6
  };

  namespace
  {
    // HelloWorld starts at 420px wide, so 400 preserves its golden wide path;
    // SmirkBench's 480px breakpoint is an app-specific choice, not a default.
    static const int kNarrowBreakpoint = 400;
  } // namespace

  class MainNode::ActionSummaryEvalFn : public DerivedState<String>::EvalFn
  {
  public:
    ActionSummaryEvalFn(const loka::app::scene::NodeState<bool> &enabled,
                        const loka::app::scene::NodeState<int> &count)
        : enabled_(enabled), count_(count) {}

    virtual String operator()()
    {
      const String enabledText = this->enabled_.get() ? String::Literal("yes") : String::Literal("no");
      return String::Literal("Enabled: ") + enabledText + String::Literal(" / clicks: ")
             + String::FromInt(this->count_.get());
    }

  private:
    const loka::app::scene::NodeState<bool> &enabled_;
    const loka::app::scene::NodeState<int> &count_;
  };

  class MainNode::FruitMessageEvalFn : public DerivedState<String>::EvalFn
  {
  public:
    FruitMessageEvalFn(const loka::app::scene::NodeState<int> &index,
                       const loka::Vector<String> &fruits)
        : index_(index), fruits_(fruits) {}

    virtual String operator()()
    {
      if (this->fruits_.empty())
        return String::Literal("You chose .");
      int index = this->index_.get();
      if (index < 0 || static_cast<std::size_t>(index) >= this->fruits_.size())
        index = 0;
      return String::Literal("You chose ") + this->fruits_[static_cast<std::size_t>(index)]
             + String::Literal(".");
    }

  private:
    const loka::app::scene::NodeState<int> &index_;
    const loka::Vector<String> &fruits_;
  };

  class MainNode::BmiResultEvalFn : public DerivedState<String>::EvalFn
  {
  public:
    BmiResultEvalFn(const loka::app::scene::NodeState<String> &height,
                    const loka::app::scene::NodeState<String> &weight)
        : height_(height), weight_(weight) {}

    virtual String operator()()
    {
      const double heightCm = this->parseBmiValue(this->height_.get());
      const double weightKg = this->parseBmiValue(this->weight_.get());
      if (heightCm <= 0.0 || weightKg <= 0.0)
        return String::Literal("BMI: --");
      const double heightM = heightCm / 100.0;
      if (heightM <= 0.0)
        return String::Literal("BMI: --");
      const double bmi = weightKg / (heightM * heightM);
      if (bmi <= 0.0)
        return String::Literal("BMI: --");
      char buf[64];
      std::snprintf(buf, sizeof(buf), "BMI: %.2f", bmi);
      return String(std::string(buf));
    }

  private:
    static double parseBmiValue(const String &value)
    {
      std::string utf8;
      if (!loka::platform::CollectUtf8(value, utf8))
        return 0.0;
      const char *start = utf8.c_str();
      char *endPtr = 0;
      const double parsed = std::strtod(start, &endPtr);
      return endPtr == start ? 0.0 : parsed;
    }

    const loka::app::scene::NodeState<String> &height_;
    const loka::app::scene::NodeState<String> &weight_;
  };

  MainNode::MainNode(const MainProps &p)
      : loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>(p),
        message_(),
        toggleEvent_(),
        actionEnabled_(),
        actionProbeCount_(),
        actionSummary_(),
        heightInput_(),
        weightInput_(),
        bmiResult_(),
        toggleActionEnabledEvent_(),
        actionProbeEvent_(),
        fruitIndex_(),
        fruitMessage_(),
        axis_(),
        viewport_(),
        fruits_(),
        random_(p.menuSeed())
  {
    for (int i = 0; i < 6; ++i)
      this->state(this->randomTitles_[i], String::Literal("Random ") + String::FromInt(i + 1));
    this->state(this->message_, String::Literal("Hello, Loka!"));
    this->state(this->actionEnabled_, true);
    this->state(this->actionProbeCount_, 0);
    this->state(this->heightInput_, String::Literal("170.0"));
    this->state(this->weightInput_, String::Literal("60.0"));
    this->state(this->fruitIndex_, 0);
    this->state(this->axis_, loka::app::STACK_AXIS_ROW);
    this->state(this->viewport_, loka::core::Frame(0, 0, 0, 0));
    const String fruitItems[] = {
        String::Literal("Apple"),
        String::Literal("Banana"),
        String::Literal("Cherry"),
        String::Literal("Grape"),
    };
    this->fruits_.assign(fruitItems, sizeof(fruitItems) / sizeof(fruitItems[0]));
    this->derived(this->actionSummary_, this->actionEnabled_, this->actionProbeCount_,
                  new (std::nothrow) ActionSummaryEvalFn(this->actionEnabled_, this->actionProbeCount_));
    this->derived(this->fruitMessage_, this->fruitIndex_,
                  new (std::nothrow) FruitMessageEvalFn(this->fruitIndex_, this->fruits_));
    this->derived(this->bmiResult_, this->heightInput_, this->weightInput_,
                  new (std::nothrow) BmiResultEvalFn(this->heightInput_, this->weightInput_));
  }

  void MainNode::declareBindings(loka::app::scene::BindingToken &t)
  {
    t.action(this->shuffleEvent_, this, &MainNode::shuffleTitles);
    t.action(this->toggleEvent_, this, &MainNode::toggleMessage);
    t.action(this->toggleActionEnabledEvent_, this, &MainNode::toggleActionEnabled);
    t.action(this->actionProbeEvent_, this, &MainNode::handleActionProbe);
    ::Window *window = this->windowOrNull();
    if (window)
    {
      t.watch(window->nativeFrame(), this, &MainNode::refreshLayoutMode, true);
    }
  }

  ::Window *MainNode::windowOrNull() const
  {
    const AttachedContext *ctx = this->attachedContext();
    return ctx ? ctx->window() : 0;
  }

  void MainNode::refreshLayoutMode()
  {
    ::Window *window = this->windowOrNull();
    if (!window || !this->axis_.isValid())
    {
      return;
    }
    const Frame frame = window->nativeFrame().get();
    const bool isNarrow = frame.hasSize() && frame.width > 0 && frame.width < kNarrowBreakpoint;
    const loka::app::StackAxis axis = isNarrow ? loka::app::STACK_AXIS_COLUMN : loka::app::STACK_AXIS_ROW;
    if (this->axis_.get() == axis)
    {
      return;
    }
    this->axis_.set(axis);
  }

  loka::app::VStack MainNode::mainLeftPanel()
  {
    using namespace loka::app;
    return VStack().TEST_ID("HelloWorld.LeftPanel")
           << Text("Loka Sample").TEST_ID("HelloWorld.LeftPanel.Title")
           << Text(this->message_.state()).TEST_ID("HelloWorld.LeftPanel.Message")
           << Button("Add +", &this->toggleEvent_).TEST_ID("HelloWorld.LeftPanel.AddButton")
           << Text(this->actionSummary_.state()).TEST_ID("HelloWorld.LeftPanel.ActionSummary")
           << Button("Probe Button", &this->actionProbeEvent_)
                  .enabled(this->actionEnabled_.state())
                  .TEST_ID("HelloWorld.LeftPanel.ProbeButton")
           << Button("Toggle Button Enabled", &this->toggleActionEnabledEvent_)
                  .TEST_ID("HelloWorld.LeftPanel.ToggleEnabledButton");
  }

  void MainNode::toggleMessage()
  {
    if (!this->message_.isValid())
    {
      return;
    }
    ::Window *window = this->windowOrNull();
    this->message_.set(this->message_.get() + " +Loka");
    if (!window)
    {
      return;
    }
    {
      StateTrackerGuard _(window->getTracker());
      const String title = window->titleState().get();
      if (title.equals(String::Literal("LokaSample")))
      {
        window->titleState().set(String::Literal("LokaSample*"));
      }
      else
      {
        window->titleState().set(String::Literal("LokaSample"));
      }
    }
  }

  void MainNode::toggleActionEnabled()
  {
    if (!this->actionEnabled_.isValid())
    {
      return;
    }
    this->actionEnabled_.set(!this->actionEnabled_.get());
  }

  void MainNode::handleActionProbe()
  {
    if (!this->actionProbeCount_.isValid())
    {
      return;
    }
    this->actionProbeCount_.set(this->actionProbeCount_.get() + 1);
  }

  void MainNode::shuffleTitles()
  {
    String titles[6];
    for (int i = 0; i < 6; ++i)
      titles[i] = this->randomTitles_[i].get();
    for (int i = 5; i > 0; --i)
    {
      const int j = this->random_.nextIndex(i + 1);
      const String tmp = titles[i];
      titles[i] = titles[j];
      titles[j] = tmp;
    }
    StateTrackerGuard guard(this->tracker());
    for (int i = 0; i < 6; ++i)
      this->randomTitles_[i].set(titles[i]);
  }

  void MainNode::composeNode(loka::app::scene::NodeComposition &c)
  {
    using namespace loka::app;
    MenuDefinition randomMenu("Random");
    randomMenu << MenuItem("Shuffle").onClick(&this->shuffleEvent_) << MenuSeparator();
    for (int i = 0; i < 6; ++i)
      randomMenu << MenuItem(String::Literal("Random ") + String::FromInt(i + 1))
                        .text(this->randomTitles_[i].state());
    c.menuBar(MenuBarDefinition()
              << (AppMenu()
                  << MenuItem("About").actionType(MENU_ACTION_ABOUT_APP)
                  << MenuSeparator()
                  << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP))
              << (Menu("View")
                  << MenuItem("Color Picker").actionType(MENU_ACTION_SHOW_COLOR_PICKER))
              << (Menu("File") << MenuItem("Quit").actionType(MENU_ACTION_QUIT_APP))
              << (Menu("Special")
                  << (MenuItem("Item") << MenuItem("Sub Item"))
                  << MenuItem("Item 2"))
              << randomMenu);
    ScrollView mainPanels = ScrollView(this->viewport_)
                                .TEST_ID("HelloWorld.MainPanelsScroll")
                            << (Box().padding(kContentInset).TEST_ID("HelloWorld.MainPanelsInset")
                                << (Stack(this->axis_.state())
                                        .TEST_ID("HelloWorld.MainPanels")
                                    << this->mainLeftPanel()
                                    << MainRightPanel(&this->fruits_,
                                                      this->fruitIndex_,
                                                      this->fruitMessage_.state(),
                                                      this->heightInput_,
                                                      this->weightInput_,
                                                      this->bmiResult_.state())));
    mainPanels.tag(kMainPanelsScrollTag);
    TextDefinition decoration = Text("*").TEST_ID("HelloWorld.Decoration");
    decoration.tag(kDecorationTag);
    c.declare(ZStack().TEST_ID("HelloWorld.Root") << mainPanels << decoration);
  }


} // namespace helloworld
