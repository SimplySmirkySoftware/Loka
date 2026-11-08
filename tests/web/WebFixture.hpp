#ifndef LOKA_WEB_FIXTURE_HPP
#define LOKA_WEB_FIXTURE_HPP
#include "app/core/AppComposition.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/scene/state/NodeState.hpp"
#include <vector>
namespace webtest
{
  class Counter;
  struct Fixture
  {
    Fixture()
        : roots(1),
          destroy(false),
          seat(0),
          depth(0),
          bodyCalls(0),
          idleCalls(0),
          idleElapsed(0),
          onInput(0),
          policy()
    {
    }
    unsigned roots;
    bool destroy;
    unsigned seat, depth;
    unsigned bodyCalls, idleCalls;
    double idleElapsed;
    void (*onInput)(Counter &);
    loka::app::IdlePolicy policy;
    std::vector<Counter *> nodes;
  };
  extern Fixture *fixture;
  /** Test-only composition vehicle; all mutable application state belongs to this Boundary. */
  class Counter : public loka::app::scene::BoundaryNodeFor<Counter>
  {
  public:
    typedef loka::app::scene::BoundaryPropsFor<Counter> PropsType;
    explicit Counter(const PropsType &props)
        : loka::app::scene::BoundaryNodeFor<Counter>(props)
    {
      this->state(this->count_, 0);
      this->state(this->key_, 0);
      this->state(this->text_, loka::core::String::Literal("Count: 0"));
      this->state(this->shown_, true);
      this->state(this->enabled_, true);
      fixture->nodes.push_back(this);
    }
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.action(this->increment_, this, &Counter::increment);
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      if (fixture->depth)
      {
        VStack column;
        for (unsigned i = 0; i < fixture->depth; ++i)
          column << Text("padding");
        c.declare(column << Button("Increment", &this->increment_));
      }
      else if (fixture->seat == 1)
      {
        c.declare(Match(*this->shown_.state())
                      .arm(true,
                           VStack() << Text(this->text_.state())
                                    << Button("Increment", &this->increment_).enabled(this->enabled_.state())));
      }
      else if (fixture->seat == 2)
      {
        c.declare(
            Keyed(*this->key_.state(),
                  this,
                  &Counter::declareArm,
                  reservation::SeatNodes<reservation::Nodes<
                      StackNode,
                      1,
                      reservation::Nodes<TextNode, 1, reservation::Nodes<ButtonNode, 1, reservation::End> > > >()));
      }
      else if (fixture->destroy)
        c.declare(
            VStack() << (Show(*this->shown_.state()).destroyOnDetach()
                         << (VStack() << Text(this->text_.state())
                                      << Button("Increment", &this->increment_).enabled(this->enabled_.state()))));
      else
        c.declare(
            VStack() << (Show(*this->shown_.state())
                         << (VStack() << Text(this->text_.state())
                                      << Button("Increment", &this->increment_).enabled(this->enabled_.state()))));
    }
    void declareArm(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(VStack() << Text(this->text_.state()) << Button("Increment", &this->increment_));
    }
    void key(int value)
    {
      this->key_.set(value);
    }
    void show(bool value)
    {
      this->shown_.set(value);
    }
    void enable(bool value)
    {
      this->enabled_.set(value);
    }
    int count() const
    {
      return this->count_.get();
    }

  private:
    void increment()
    {
      ++fixture->bodyCalls;
      this->count_.set(this->count_.get() + 1);
      this->text_.set(loka::core::String::Literal("Count: ") + loka::core::String::FromInt(this->count_.get()));
      if (fixture->onInput)
        fixture->onInput(*this);
    }
    loka::app::scene::NodeState<int> count_, key_;
    loka::app::scene::NodeState<loka::core::String> text_;
    loka::app::scene::NodeState<bool> shown_, enabled_;
    loka::core::EmitterState increment_;
  };
  class CounterConfig : public AppConfigurable
  {
  public:
    explicit CounterConfig(PlatformContext *platform)
        : AppConfigurable(platform)
    {
    }
    virtual void compose(AppComposition &c)
    {
      for (unsigned i = 0; i < fixture->roots; ++i)
        c << WindowDef(WindowProps().size(320, 200).scene(loka::app::scene::Boundary<Counter>()));
    }
    virtual loka::app::IdlePolicy idlePolicy() const
    {
      return fixture->policy;
    }
    virtual void onIdle(double elapsed)
    {
      ++fixture->idleCalls;
      fixture->idleElapsed = elapsed;
    }
  };
  inline AppConfigurable *CreateCounter(PlatformContext *platform)
  {
    return new CounterConfig(platform);
  }
} // namespace webtest
#endif
