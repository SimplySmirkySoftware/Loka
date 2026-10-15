#ifndef LOKA_HELLOWORLD_MAIN_NODE_HPP
#define LOKA_HELLOWORLD_MAIN_NODE_HPP

#include "core/Frame.hpp"
#include "MainRightPanel.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/State.hpp"
#include "core/String.hpp"
#include "core/Vector.hpp"

namespace helloworld
{
  using loka::app::Button;

  class MainNode;
  namespace testing { class MainAccess; }

  struct MainTypeTag {};

  /** Completed startup input; the Scene owns the evolving shuffle sequence. */
  class MainProps : public loka::app::scene::NodePropsBase<MainProps>
  {
  public:
    typedef MainTypeTag TypeTag;
    typedef MainNode NodeType;
    explicit MainProps(unsigned long menuSeed = 0) : menuSeed_(menuSeed) {}
    unsigned long menuSeed() const { return this->menuSeed_; }
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() &&
             this->menuSeed_ < static_cast<const MainProps &>(rhs).menuSeed_;
    }
  private:
    unsigned long menuSeed_;
  };

  class MainNode : public loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>
  {
  public:
    MainNode(const MainProps &p);
    virtual void declareBindings(loka::app::scene::BindingToken &t);
    virtual void composeNode(loka::app::scene::NodeComposition &c);

  private:
#ifdef TEST_BUILD
    friend class testing::MainAccess;
#endif
    class MenuRandom
    {
    public:
      explicit MenuRandom(unsigned long seed)
          : state_(seed)
      {
      }

      int nextIndex(int upperBound)
      {
        // Fixed C++98 arithmetic keeps a seed's menu sequence independent
        // of platform C-library rand() implementations and other app code.
        this->state_ =
            (this->state_ * 1664525UL + 1013904223UL) & 0xFFFFFFFFUL;
        return static_cast<int>((this->state_ >> 16) %
                                static_cast<unsigned long>(upperBound));
      }

    private:
      unsigned long state_;
    };

    class ActionSummaryEvalFn;
    class FruitMessageEvalFn;
    class BmiResultEvalFn;

    ::Window *windowOrNull() const;
    loka::app::VStack mainLeftPanel();
    void refreshLayoutMode();
    void toggleMessage();
    void toggleActionEnabled();
    void handleActionProbe();
    void shuffleTitles();

    loka::app::scene::NodeState<loka::core::String> message_;
    loka::core::EmitterState toggleEvent_;
    loka::app::scene::NodeState<bool> actionEnabled_;
    loka::app::scene::NodeState<int> actionProbeCount_;
    loka::app::scene::DerivedNodeState<loka::core::String> actionSummary_;
    loka::app::scene::NodeState<loka::core::String> heightInput_;
    loka::app::scene::NodeState<loka::core::String> weightInput_;
    loka::app::scene::DerivedNodeState<loka::core::String> bmiResult_;
    loka::core::EmitterState toggleActionEnabledEvent_;
    loka::core::EmitterState actionProbeEvent_;
    loka::app::scene::NodeState<int> fruitIndex_;
    loka::app::scene::DerivedNodeState<loka::core::String> fruitMessage_;
    loka::app::scene::NodeState<loka::app::StackAxis> axis_;
    loka::app::scene::NodeState<loka::core::Frame> viewport_;
    loka::Vector<loka::core::String> fruits_;
    loka::app::scene::NodeState<loka::core::String> randomTitles_[6];
    loka::core::EmitterState shuffleEvent_;
    MenuRandom random_;
  };

} // namespace helloworld

#endif // LOKA_HELLOWORLD_MAIN_NODE_HPP
