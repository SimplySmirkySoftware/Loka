#ifndef LOKA_TOOLBOX_BMI_TAB_SCENARIO_HPP
#define LOKA_TOOLBOX_BMI_TAB_SCENARIO_HPP

#include "HelloWorldScenarios.hpp"
#include "ToolboxInputDoor.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "platform/StringUTF8.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace loka
{
  namespace scenario_tests
  {
    /** Observes real ADB input supplied by Lua; never injects traversal keys. */
    class ToolboxBmiTabScenario
    {
    public:
      ToolboxBmiTabScenario()
          : elapsed_(0)
      {
      }

      ScenarioAdvance advance(double elapsed,
                              app::scene::Scene *scene,
                              ToolboxScenePlatformController &controller,
                              dsl::SnapRecord &out)
      {
        this->elapsed_ += elapsed;
        app::EditTextNode *height = 0;
        app::EditTextNode *weight = 0;
        dsl::FlowError error;
        using namespace dsl::testing;
        bool ok = ResolveSelector<app::EditTextNode>(
                      scene, Within("HelloWorld.Bmi").descendant<app::EditTextNode>(1), height, error)
                      == dsl::FLOW_STEP_SUCCEEDED
                  && ResolveSelector<app::EditTextNode>(
                         scene, Within("HelloWorld.Bmi").descendant<app::EditTextNode>(2), weight, error)
                         == dsl::FLOW_STEP_SUCCEEDED;
        std::string heightText, weightText;
        app::scene::NodeContext *focused = 0;
        if (ok)
          ok = platform::CollectUtf8(height->props.text_.state()->get(), heightText)
               && platform::CollectUtf8(weight->props.text_.state()->get(), weightText)
               && controller.readNativeFocus(focused);
        const bool arrived = ok && heightText == "3" && weightText == "2" && focused == height->getContext();
        // Wait past Lua's mouse move so the capture is quiet; time out on main.
        if (this->elapsed_ < 20 || (!arrived && this->elapsed_ < 60))
          return SCENARIO_ADVANCE_PENDING;
        dsl::BuildSnapV1RecordAdapter(
            "HelloWorld", "bmi-tab", "HelloWorld.Bmi", 1, 1, arrived ? dsl::SnapStatusOk() : dsl::SnapStatusError())
            .run(0, out, error);
        out.set("height.text", heightText.c_str());
        out.set("weight.text", weightText.c_str());
        out.set("focused.field",
                height && focused == height->getContext()   ? "height"
                : weight && focused == weight->getContext() ? "weight"
                                                            : "none");
        // Record focus first, then drop it to stop caret blinking during settle.
        Point outside = {0, 0};
        (void)ToolboxInputDoor::mouseDown(controller, outside);
        return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
      }

    private:
      double elapsed_;
    };
  } // namespace scenario_tests
} // namespace loka
#endif
