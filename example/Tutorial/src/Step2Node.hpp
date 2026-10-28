#ifndef LOKA_TUTORIAL_STEP2_NODE_HPP
#define LOKA_TUTORIAL_STEP2_NODE_HPP

#include "TutorialShared.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/Text.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/State.hpp"
#include "core/String.hpp"

namespace tutorial
{
  class Step2Node : public loka::app::scene::BoundaryNodeFor<Step2Node>
  {
  public:
    typedef loka::app::scene::BoundaryPropsFor<Step2Node> PropsType;

    Step2Node(const PropsType &p)
        : loka::app::scene::BoundaryNodeFor<Step2Node>(p),
          count_(),
          countText_(),
          incrementEvent_()
    {
      this->state(this->count_, 0);
      this->state(this->countText_, loka::core::String::Literal("Count: 0"));
    }

    virtual void declareBindings(loka::app::scene::BindingToken &t)
    {
      t.action(this->incrementEvent_, this, &Step2Node::increment);
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(VStack()                                                          //
                << TutorialTitle("Step 2")                                        //
                << Text(this->countText_.state()).TEST_ID("Tutorial.Step2.Count") //
                << Button("Increment", &this->incrementEvent_).TEST_ID("Tutorial.Step2.Increment")
                << TutorialHint("A button can update state, and bound text follows it."));
    }

  private:
    void increment()
    {
      const int next = this->count_.get() + 1;
      this->count_.set(next);
      using loka::core::String;
      this->countText_.set(String::Literal("Count: ") + String::FromInt(next));
    }

    loka::app::scene::NodeState<int> count_;
    loka::app::scene::NodeState<loka::core::String> countText_;
    loka::core::EmitterState incrementEvent_;
  };
} // namespace tutorial

#endif // LOKA_TUTORIAL_STEP2_NODE_HPP
