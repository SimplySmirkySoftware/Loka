#ifndef LOKA_TUTORIAL_STEP5_NODE_HPP
#define LOKA_TUTORIAL_STEP5_NODE_HPP

#include "TutorialShared.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/Text.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/State.hpp"
#include "core/String.hpp"

namespace tutorial
{
  // Standalone lessons deliberately repeat Step4Node's list; keep both capacities at 100.
  class Step5Node : public loka::app::scene::BoundaryNodeFor<Step5Node>
  {
  public:
    typedef loka::app::scene::BoundaryPropsFor<Step5Node> PropsType;

    Step5Node(const PropsType &p)
        : loka::app::scene::BoundaryNodeFor<Step5Node>(p),
          itemCount_(),
          itemSummary_(),
          showSummary_(),
          canAdd_(),
          viewport_(),
          addItemEvent_(),
          toggleSummaryEvent_()
    {
      this->state(this->itemCount_, 0);
      this->state(this->itemSummary_, loka::core::String());
      this->state(this->showSummary_, true);
      this->state(this->canAdd_, false);
      this->state(this->viewport_, loka::core::Frame(0, 0, 0, 0));
    }

    virtual void declareBindings(loka::app::scene::BindingToken &t)
    {
      t.action(this->addItemEvent_, this, &Step5Node::addItem);
      t.action(this->toggleSummaryEvent_, this, &Step5Node::toggleSummary);
      t.watch(*this->itemCount_.state(), this, &Step5Node::projectCount, true);
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(VStack()                   //
                << TutorialTitle("Step 5") //
                << Button("Add item", &this->addItemEvent_)
                       .enabled(this->canAdd_.state())
                       .TEST_ID("Tutorial.Step5.AddItemButton") //
                << Button("Show/Hide map", &this->toggleSummaryEvent_).TEST_ID("Tutorial.Step5.ToggleSummaryButton")
                << TutorialHint("ScrollView lets the list grow past the window.")
                << (Show(*this->showSummary_.state()) //
                    << Text(this->itemSummary_.state()).TEST_ID("Tutorial.Step5.Summary")
                    << (ScrollView(this->viewport_).TEST_ID("Tutorial.Step5.ScrollView")
                        << Keyed(*this->itemCount_.state(), this, &Step5Node::declareItems, ItemSeats()))));
    }

  private:
    enum
    {
      kMaxItems = 100
    };

    typedef loka::app::reservation::SeatNodes<loka::app::reservation::Nodes<
        loka::app::FragmentNode,
        1,
        loka::app::reservation::Nodes<loka::app::BoundarySectionNode,
                                      kMaxItems,
                                      loka::app::reservation::Nodes<loka::app::TextNode, kMaxItems> > > >
        ItemSeats;

    void projectCount()
    {
      const int count = this->itemCount_.get();
      this->itemSummary_.set(loka::core::String::Literal("Items: ") + loka::core::String::FromInt(count)
                             + loka::core::String::Literal(count >= kMaxItems ? " (full)" : ""));
      this->canAdd_.set(count < kMaxItems);
    }

    /** Numbered item value, completed before For consumes the array. */
    struct Item
    {
      int number;
    };

    /** Each numbered item becomes one owned Text value in its Section. */
    struct ItemFactory
    {
      loka::app::TextDefinition operator()(const Item &item, std::size_t) const
      {
        return loka::app::Text(loka::core::String::Literal("Item ") + loka::core::String::FromInt(item.number));
      }
    };

    void declareItems(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      Item items[kMaxItems];
      for (int i = 0; i < kMaxItems; ++i)
      {
        items[i].number = i + 1;
      }
      c.declare(Fragment() << For(1, items, ItemFactory()).window(0, this->itemCount_.get()));
    }

    void addItem()
    {
      if (this->itemCount_.get() < kMaxItems)
      {
        this->itemCount_.set(this->itemCount_.get() + 1);
      }
    }

    void toggleSummary()
    {
      this->showSummary_.set(!this->showSummary_.get(), true);
    }

    loka::app::scene::NodeState<int> itemCount_;
    loka::app::scene::NodeState<loka::core::String> itemSummary_;
    loka::app::scene::NodeState<bool> showSummary_;
    loka::app::scene::NodeState<bool> canAdd_;
    loka::app::scene::NodeState<loka::core::Frame> viewport_;
    loka::core::EmitterState addItemEvent_;
    loka::core::EmitterState toggleSummaryEvent_;
  };
} // namespace tutorial

#endif // LOKA_TUTORIAL_STEP5_NODE_HPP
