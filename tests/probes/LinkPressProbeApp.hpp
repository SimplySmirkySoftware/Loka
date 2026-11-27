#ifndef LOKA_LINK_PRESS_PROBE_APP_HPP
#define LOKA_LINK_PRESS_PROBE_APP_HPP

#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/StringUTF8.hpp"
#include <cstdio>

namespace link_press_probe
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using loka::core::String;

  class MainNode : public BoundaryNodeFor<MainNode>
  {
  public:
    explicit MainNode(const PropsType &props)
        : BoundaryNodeFor<MainNode>(props),
          count_(0)
    {
      this->state(this->activated_, LinkTarget());
      this->state(this->display_, String("last: none  count: 0"));
    }

    virtual void declareBindings(BindingToken &token)
    {
      token.watch(*this->activated_.state(), this, &MainNode::activated);
    }

    virtual void composeNode(NodeComposition &composition)
    {
      const AttributedString links = Linked("alpha", TextStyle(), LinkTarget(String("alpha")))
                                     + Linked("beta", TextStyle(), LinkTarget(String("beta")))
                                     + Styled("  ", TextStyle())
                                     + Linked("Follow this long link onto its second line and click there.",
                                              TextStyle(),
                                              LinkTarget(String("wrapped")));
      composition.declare(Column().gap(8)
                          << (AttributedText(links).activatedLink(this->activated_) + BlockStyle().wrap(TEXT_WRAP_WORD))
                          << Text(this->display_.state()));
    }

  private:
    void activated()
    {
      const LinkTarget target = this->activated_.get();
      if (target.isNone())
        return;
      ++this->count_;
      std::string name;
      loka::platform::CollectUtf8(target.text(), name);
      std::printf("activated %s count=%u\n", name.c_str(), this->count_);
      std::fflush(stdout);
      char count[32];
      std::sprintf(count, "%u", this->count_);
      loka::core::StateTrackerGuard guard(this->tracker());
      this->display_.set(String("last: ") + target.text() + "  count: " + std::string(count));
    }

    NodeState<LinkTarget> activated_;
    NodeState<String> display_;
    unsigned count_;
  };

  class ProbeApp : public AppConfigurable
  {
  public:
    explicit ProbeApp(PlatformContext *context)
        : AppConfigurable(context)
    {
    }
    virtual void compose(AppComposition &composition)
    {
      composition << WindowDef(WindowProps()
                                   .frame(60, 60, 260, 160)
                                   .title("Link press probe")
                                   .scene(Boundary<MainNode>())
                                   .idlePolicy(this->idlePolicy())
                                   .visible(true));
    }
  };
} // namespace link_press_probe

#endif
