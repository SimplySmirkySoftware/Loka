#ifndef LOKA_TUTORIAL_APP_CONFIG_HPP
#define LOKA_TUTORIAL_APP_CONFIG_HPP

#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/Menu.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "DoItYourselfNode.hpp"
#include "Step1Node.hpp"
#include "Step2Node.hpp"
#include "Step3Node.hpp"
#include "Step4Node.hpp"
#include "Step5Node.hpp"
#include "Step6Node.hpp"

class TutorialAppConfig : public AppConfigurable
{
public:
  explicit TutorialAppConfig(PlatformContext *ctx)
      : AppConfigurable(ctx)
  {
  }

  virtual void compose(AppComposition &c)
  {
    typedef tutorial::DoItYourselfNode TutorialNode;
    // typedef tutorial::Step1Node TutorialNode;
    // typedef tutorial::Step2Node TutorialNode;
    // typedef tutorial::Step3Node TutorialNode;
    // typedef tutorial::Step4Node TutorialNode;
    // typedef tutorial::Step5Node TutorialNode;
    // typedef tutorial::Step6Node TutorialNode;

    c << WindowDef(this->productionWindowProps(
        loka::app::scene::Boundary<TutorialNode>()));
  }

  virtual void composeDefaultMenu(loka::app::MenuComposition &c)
  {
    tutorial::DeclareTutorialMenu(c);
  }

protected:
  /** Declares Tutorial's production window presentation around a supplied
      scene so non-production vehicles cannot drift its title or frame. */
  WindowProps productionWindowProps(const loka::app::scene::NodeDefinitionBase &scene) const
  {
    return WindowProps()
        .frame(60, 60, 360, 280)
        .scene(scene)
        .title("LokaTutorial")
        .visible(true);
  }
};

#endif // LOKA_TUTORIAL_APP_CONFIG_HPP
