#ifndef LOKA_TESTS_TOOLBOX_OBSERVED_MAIN_DEFINITION_HPP
#define LOKA_TESTS_TOOLBOX_OBSERVED_MAIN_DEFINITION_HPP

#include "app/scene/boundary/Boundary.hpp"

namespace loka
{
  namespace scenario_tests
  {
#ifdef TEST_BUILD
    namespace testing
    {
      void failObservedMainDefinitionClones(int count);
      void allowObservedMainDefinitionClones();
      bool shouldCloneObservedMainDefinition();
      void refuseObservedMainCreation(bool refuse);
      bool observedMainCreationRefused();
    } // namespace testing
#endif

    /** Creates one typed main Boundary and optionally publishes a borrowed
        view while the App-owned Window and Scene remain alive. */
    template <class PropsT, class NodeT>
    class ObservedMainDefinition : public app::scene::BoundaryDefinition<PropsT, NodeT>
    {
    public:
      typedef app::scene::BoundaryDefinition<PropsT, NodeT> Base;

      ObservedMainDefinition(const PropsT &props, NodeT **observed)
          : Base(props),
            observed_(observed)
      {
      }

      typedef ObservedMainDefinition CloneType;
      virtual app::scene::NodeDefinitionBase *clone() const
      {
#ifdef TEST_BUILD
        if (!testing::shouldCloneObservedMainDefinition())
        {
          return 0;
        }
#endif
        return new ObservedMainDefinition(*this);
      }

      virtual app::scene::Node *create() const
      {
#ifdef TEST_BUILD
        if (testing::observedMainCreationRefused())
          return 0;
#endif
        app::scene::Node *node = Base::create();
        if (this->observed_)
        {
          *this->observed_ = node ? static_cast<NodeT *>(node) : 0;
        }
        return node;
      }

    private:
      NodeT **observed_;
    };
  } // namespace scenario_tests
} // namespace loka

#endif // LOKA_TESTS_TOOLBOX_OBSERVED_MAIN_DEFINITION_HPP
