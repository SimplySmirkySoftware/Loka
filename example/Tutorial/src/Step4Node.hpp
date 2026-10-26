#ifndef LOKA_TUTORIAL_STEP4_NODE_HPP
#define LOKA_TUTORIAL_STEP4_NODE_HPP

#include "TutorialShared.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/Text.hpp"
#include "app/scene/state/FlowSlot.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/State.hpp"
#include "core/String.hpp"
#include "dsl/stream/StateStream.hpp"

namespace tutorial
{
  class Step4Node : public loka::app::scene::BoundaryNodeFor<Step4Node>
  {
  public:
    typedef loka::app::scene::BoundaryPropsFor<Step4Node> PropsType;

    Step4Node(const PropsType &p)
        : loka::app::scene::BoundaryNodeFor<Step4Node>(p),
          itemCount_(),
          itemSummary_(),
          showSummary_(),
          canAdd_(),
          itemSummaryFlow_(*this),
          canAddFlow_(*this),
          addItemEvent_(),
          toggleSummaryEvent_()
    {
      this->state(this->itemCount_, 0);
      this->state(this->itemSummary_, loka::core::String::Literal("Items: 0"));
      this->state(this->showSummary_, true);
      this->state(this->canAdd_, true);
    }

    virtual void declareBindings(loka::app::scene::BindingToken &t)
    {
      t.action(this->addItemEvent_, this, &Step4Node::addItem);
      t.action(this->toggleSummaryEvent_, this, &Step4Node::toggleSummary);
    }

    virtual void attachNode(loka::app::scene::NodeComposition &c)
    {
      (void)c;
      loka::dsl::StateStream<int> itemCountStream = this->itemCount_.stream();
      this->itemSummaryFlow_.set(itemCountStream.map(ItemSummary())).bindTo(this->itemSummary_);
      this->canAddFlow_.set(itemCountStream.map(itemCountStream.slot.value() < loka::dsl::Const(kMaxItems)))
          .bindTo(this->canAdd_);
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      c.declare(VStack()                   //
                << TutorialTitle("Step 4") //
                << Button("Add item", &this->addItemEvent_)
                       .enabled(this->canAdd_.state())
                       .TEST_ID("Tutorial.Step4.AddItemButton") //
                << Button("Show/Hide map", &this->toggleSummaryEvent_).TEST_ID("Tutorial.Step4.ToggleSummaryButton")
                << (Show(*this->showSummary_.state()) //
                    << Text(this->itemSummary_.state()).TEST_ID("Tutorial.Step4.Summary")
                    << Keyed(*this->itemCount_.state(), this, &Step4Node::declareItems, ItemSeats()))
                // The hint text changes with the DoItYourself text and the golden
                // re-bake in the last Tutorial PR, so this PR keeps the captures.
                << TutorialHint("StdComposition can still reveal predeclared children while state changes update "
                                "derived display text."));
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

    /** Display text derived from the item count, including the capacity limit. */
    struct ItemSummary
    {
      typedef loka::core::String Result;
      Result operator()(int count) const
      {
        return loka::core::String::Literal("Items: ") + loka::core::String::FromInt(count)
               + loka::core::String::Literal(count >= kMaxItems ? " (full)" : "");
      }
    };

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
    loka::app::scene::FlowSlot<loka::dsl::StateStream<loka::core::String> > itemSummaryFlow_;
    loka::app::scene::FlowSlot<loka::dsl::StateStream<bool> > canAddFlow_;
    loka::core::EmitterState addItemEvent_;
    loka::core::EmitterState toggleSummaryEvent_;
  };
} // namespace tutorial

#endif // LOKA_TUTORIAL_STEP4_NODE_HPP
