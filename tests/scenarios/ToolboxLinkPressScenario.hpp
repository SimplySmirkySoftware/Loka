#ifndef LOKA_TOOLBOX_LINK_PRESS_SCENARIO_HPP
#define LOKA_TOOLBOX_LINK_PRESS_SCENARIO_HPP

#include "SmirkBenchAttributedScenario.hpp"
#include "context/ToolboxAttributedTextContext.hpp"

#include "core/util/StateTrackerGuard.hpp"
#include <Events.h>
#include <cstdio>

namespace loka
{
  namespace scenario_tests
  {
    /** Fixed leaf geometry for this gesture vehicle. Normal rail contexts own
        paint and input; a transparent placement adds no ZStack shield. */
    template <class Base, class Props, short X, short WIDTH> class LinkPressPlacedNode : public Base
    {
    public:
      explicit LinkPressPlacedNode(const Props &props)
          : Base(props)
      {
      }
      virtual short layoutProjected(app::scene::IPlatformController *controller, app::scene::LayoutState &state)
      {
        app::scene::LayoutState placed = state;
        placed.x = X;
        placed.y = 12;
        placed.width = WIDTH;
        Base::layoutProjected(controller, placed);
        return 0;
      }
    };
    typedef app::scene::NodeDefinition<app::TextProps, LinkPressPlacedNode<app::TextNode, app::TextProps, 92, 50> >
        LinkPressLabel;
    typedef app::scene::NodeDefinition<app::ButtonProps,
                                       LinkPressPlacedNode<app::ButtonNode, app::ButtonProps, 192, 70> >
        LinkPressButton;

    class LinkPressNode;
    typedef app::scene::BoundaryPropsFor<LinkPressNode> LinkPressProps;
    /** Owns the result, displayed count and gesture acknowledgements. The Busy
        action is a scenario-local queue gate, never part of the input rail. */
    class LinkPressNode : public app::scene::StdCompositionBoundaryNodeBase<LinkPressProps>
    {
    public:
      typedef LinkPressProps::TypeTag TypeTag;
      explicit LinkPressNode(const LinkPressProps &props)
          : app::scene::StdCompositionBoundaryNodeBase<LinkPressProps>(props)
      {
        this->state(this->activated_, app::LinkTarget());
        this->state(this->count_, 0);
        this->state(this->ack_, 0);
        this->state(this->display_, core::String("target: none; count: 0"));
      }
      virtual void declareBindings(app::scene::BindingToken &token)
      {
        token.watch(*this->activated_.state(), this, &LinkPressNode::activated);
        token.action(this->next_, this, &LinkPressNode::next);
        token.action(this->busy_, this, &LinkPressNode::busy);
      }
      virtual void composeNode(app::scene::NodeComposition &composition)
      {
        using namespace app;
        composition.declare(Column().gap(0)
                            << AttributedText(
                                   Linked("MMMMMMMMMMMMMMMMMMMMMMMM", TextStyle(), LinkTarget(core::String("one")))
                                   + Styled("  ", TextStyle())
                                   + Linked("two", TextStyle(), LinkTarget(core::String("two"))))
                                   .activatedLink(this->activated_)
                                   .TEST_ID("LinkPress.Text")
                            << LinkPressLabel(TextProps("Label")) << LinkPressButton(ButtonProps().text("Native"))
                            << Box().height(24) << Text(this->display_.state())
                            << (Row() << app::Button("Next", &this->next_) << app::Button("Busy", &this->busy_)));
      }
      int count() const
      {
        return this->count_.get();
      }
      int acknowledgements() const
      {
        return this->ack_.get();
      }
      app::LinkTarget target() const
      {
        return this->activated_.get();
      }

    private:
      void activated()
      {
        if (this->activated_.get().isNone())
          return;
        core::StateTrackerGuard guard(this->tracker());
        this->count_.set(this->count_.get() + 1);
        char text[80];
        std::sprintf(text,
                     "target: %s; count: %d",
                     this->activated_.get() == app::LinkTarget(core::String("one")) ? "one" : "two",
                     this->count_.get());
        this->display_.set(core::String(text));
      }
      void next()
      {
        core::StateTrackerGuard guard(this->tracker());
        this->ack_.set(this->ack_.get() + 1);
      }
      void busy()
      {
        const unsigned long start = TickCount();
        while (TickCount() - start < 240)
        {
        }
      }
      app::scene::NodeState<app::LinkTarget> activated_;
      app::scene::NodeState<int> count_, ack_;
      app::scene::NodeState<core::String> display_;
      core::EmitterState next_, busy_;
    };

    /** Outcomes only: a refused gesture's entry into tracking is W2 evidence,
        not a claim made by this audit or its screenshot. */
    class ToolboxLinkPressScenario
    {
    public:
      ToolboxLinkPressScenario()
          : step_(0),
            elapsed_(0)
      {
      }
      ScenarioAdvance advance(double elapsed,
                              app::scene::Scene *scene,
                              ToolboxWindow &window,
                              ToolboxScenePlatformController &,
                              dsl::testing::ScenarioAuditSink &audit,
                              dsl::SnapRecord &out)
      {
        this->elapsed_ += elapsed;
        app::scene::BoundaryNode *root = dsl::testing::SceneTestAccess::rootBoundary(*scene);
        if (!root || root->propsTypeId() != LinkPressProps::staticTypeId())
          return this->fail(out, "missing link scene");
        LinkPressNode &owner = *static_cast<LinkPressNode *>(root);
        if (this->elapsed_ > 90)
          return this->fail(out, "gesture timeout");
        if (owner.acknowledgements() < this->step_)
          return SCENARIO_ADVANCE_PENDING;
        static const int counts[] = {0, 1, 2, 2, 2, 3, 4, 6};
        static const char *const gestures[] = {
            "ready", "click", "off-and-back", "empty", "native", "label", "fast", "queued-two"};
        dsl::SnapRecord record = SmirkBenchEditorRecord("link-press", this->step_, true);
        record.set("gesture", gestures[this->step_]);
        record.setInt("activation.count", owner.count());
        record.set("activation.target", owner.target().isNone() ? "none" : "one");
        if (owner.count() != counts[this->step_]
            || (this->step_ && owner.target() != app::LinkTarget(core::String("one"))))
          return this->fail(out, "activation outcome differs", &record);
        if (!this->step_)
        {
          // Paired with Lua's fixed link/label points. Refuse geometry drift
          // before accepting gestures; the native destination is discovered.
          app::scene::Node *node = 0;
          dsl::FlowError error;
          ToolboxPortScope port(window.window());
          if (dsl::testing::LookupNodeById<app::scene::Node>(scene, "LinkPress.Text", node, error)
                  != dsl::FLOW_STEP_SUCCEEDED
              || !node || !node->getContext())
            return this->fail(out, "missing linked text");
          ToolboxAttributedTextContext *context = static_cast<ToolboxAttributedTextContext *>(node->getContext());
          const Point points[] = {{16, 16}, {16, 96}};
          for (unsigned i = 0; i < 2; ++i)
          {
            ToolboxLinkHit hit;
            app::AttributedString::LinkRun run;
            if (context->prepareLinkHit(points[i], hit) != TOOLBOX_HIT_LINK || !context->queryEligibleRun(hit, run)
                || run.first != 0)
              return this->fail(out, "OPEN link/label geometry differs from Lua pair");
          }
        }
        if (this->step_ == 7)
        {
          out = record;
          return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
        }
        if (!audit.recordVerdict(record))
          return this->fail(out, "audit write failed");
        ++this->step_;
        this->elapsed_ = 0;
        return SCENARIO_ADVANCE_PENDING;
      }

    private:
      ScenarioAdvance fail(dsl::SnapRecord &out, const char *why, const dsl::SnapRecord *record = 0)
      {
        out = record ? *record : SmirkBenchEditorRecord("link-press", this->step_, false);
        out.set("status", dsl::SnapStatusError());
        out.set("reason", why);
        return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
      }
      int step_;
      double elapsed_;
    };
  } // namespace scenario_tests
} // namespace loka
#endif
