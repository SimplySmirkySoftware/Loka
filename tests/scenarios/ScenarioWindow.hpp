#ifndef LOKA_TESTS_TOOLBOX_SCENARIO_WINDOW_HPP
#define LOKA_TESTS_TOOLBOX_SCENARIO_WINDOW_HPP

#include "ObservedMainDefinition.hpp"
#include "app/core/WindowDefinition.hpp"

namespace loka
{
  namespace scenario_tests
  {
    /** Builds the mechanically shared scenario Window around one
        example-owned main Boundary definition. */
    template <class PropsT, class NodeT>
    WindowProps MakeScenarioWindowProps(const PropsT &props,
                                        NodeT **observed,
                                        int width,
                                        int height,
                                        const char *title,
                                        const app::IdlePolicy &idlePolicy,
                                        WindowProps::OnIdleFn onIdle,
                                        void *userData,
                                        core::State<core::String> *displayTitleState = 0)
    {
      ObservedMainDefinition<PropsT, NodeT> mainDefinition(props, observed);
      WindowProps windowProps;
      windowProps.frame(40, 40, width, height)
          .scene(mainDefinition)
          .title(title)
          .visible(true)
          .idlePolicy(idlePolicy)
          .onIdle(onIdle, userData);
      if (displayTitleState)
      {
        windowProps.displayTitleState(displayTitleState);
      }
      return windowProps;
    }

    template <class PropsT, class NodeT>
    WindowDefinition<WindowProps> MakeScenarioWindow(const PropsT &props,
                                                     NodeT **observed,
                                                     int width,
                                                     int height,
                                                     const char *title,
                                                     const app::IdlePolicy &idlePolicy,
                                                     WindowProps::OnIdleFn onIdle,
                                                     void *userData,
                                                     core::State<core::String> *displayTitleState = 0)
    {
      return WindowDef(MakeScenarioWindowProps(
          props, observed, width, height, title, idlePolicy, onIdle, userData, displayTitleState));
    }
  } // namespace scenario_tests
} // namespace loka

#endif // LOKA_TESTS_TOOLBOX_SCENARIO_WINDOW_HPP
