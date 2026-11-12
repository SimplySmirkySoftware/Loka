#ifndef LOKA_TOOLBOX_CLIP_EDGE_TRACKING_SCENARIO_HPP
#define LOKA_TOOLBOX_CLIP_EDGE_TRACKING_SCENARIO_HPP

#include "ToolboxTextEditorScrollScenario.hpp"
#include "ToolboxDrawMeasurement.hpp"
#include "context/ToolboxEditTextContext.hpp"
#include "context/ToolboxClipBoxContext.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace loka
{
  namespace scenario_tests
  {
    /** Window-local geometry paired with the clip-edge-tracking Lua block.
        OPEN refuses drift before accepting gestures. Only the edit gesture
        uses a fixed point; Lua obtains bar/button rectangles from live controls. */
    struct ClipEdgeGeometry
    {
      enum
      {
        LEFT = 12,
        TOP = 12,
        EDIT_WIDTH = 200,
        EDIT_CLIP = 80,
        EDIT_HEIGHT = 24,
        BAR_TOP = 60,
        BAR_WIDTH = 160,
        BAR_HEIGHT = 120
      };
      static Rect rect(short left, short top, short right, short bottom)
      {
        Rect r = {top, left, bottom, right};
        return r;
      }
      static Rect editOutside()
      {
        return rect(LEFT + EDIT_CLIP, TOP, LEFT + EDIT_WIDTH, TOP + EDIT_HEIGHT);
      }
      static Rect barOutside()
      {
        return rect(LEFT + BAR_WIDTH - 8, BAR_TOP, LEFT + BAR_WIDTH, BAR_TOP + BAR_HEIGHT);
      }
      static Rect bar()
      {
        return rect(LEFT + BAR_WIDTH - 16, BAR_TOP, LEFT + BAR_WIDTH, BAR_TOP + BAR_HEIGHT);
      }
    };

    class ClipEdgeNode;
    typedef app::scene::BoundaryPropsFor<ClipEdgeNode> ClipEdgeProps;
    /** Scene owns the sole live layout input and the gesture acknowledgement. */
    class ClipEdgeNode : public app::scene::StdCompositionBoundaryNodeBase<ClipEdgeProps>
    {
    public:
      typedef ClipEdgeProps::TypeTag TypeTag;
      explicit ClipEdgeNode(const ClipEdgeProps &props)
          : app::scene::StdCompositionBoundaryNodeBase<ClipEdgeProps>(props)
      {
        this->state(this->acknowledgements_, 0u);
        this->state(this->text_, core::String::Literal("Clip edge tracking text"));
        this->state(this->clipWidth_, int(ClipEdgeGeometry::BAR_WIDTH - 8));
      }
      virtual void declareBindings(app::scene::BindingToken &token)
      {
        token.action(this->next_, this, &ClipEdgeNode::acknowledge);
      }
      virtual void composeNode(app::scene::NodeComposition &composition)
      {
        using namespace app;
        composition.declare(
            Column().gap(0)
            << (ClipBox().size(ClipEdgeGeometry::EDIT_CLIP, ClipEdgeGeometry::EDIT_HEIGHT).TEST_ID("ClipEdge.EditClip")
                << (Box().width(ClipEdgeGeometry::EDIT_WIDTH) << EditText(this->text_).TEST_ID("ClipEdge.Edit")))
            << Box().height(24)
            << (ClipBox()
                    .width(this->clipWidth_.state())
                    .height(ClipEdgeGeometry::BAR_HEIGHT)
                    .TEST_ID("ClipEdge.BarClip")
                << (Box().size(ClipEdgeGeometry::BAR_WIDTH, ClipEdgeGeometry::BAR_HEIGHT)
                    << (ScrollView() << Box().size(100, 600))))
            << Box().height(24) << Button("Next", &this->next_));
      }
      unsigned acknowledgements() const
      {
        return this->acknowledgements_.get();
      }
      void expose()
      {
        core::StateTrackerGuard guard(this->tracker());
        this->clipWidth_.set(ClipEdgeGeometry::BAR_WIDTH);
      }

    private:
      void acknowledge()
      {
        core::StateTrackerGuard guard(this->tracker());
        this->acknowledgements_.set(this->acknowledgements_.get() + 1);
      }
      app::scene::NodeState<core::String> text_;
      app::scene::NodeState<int> clipWidth_;
      core::EmitterState next_;
      app::scene::NodeState<unsigned> acknowledgements_;
    };

    /** A captured all-white rectangle needs only its bounds: every baseline
        pixel is known. A non-white baseline refuses OPEN rather than hiding ink. */
    class ClipEdgePixels
    {
    public:
      ClipEdgePixels()
          : rect_(),
            peak_(0)
      {
      }
      bool capture(const Rect &rect)
      {
        this->rect_ = rect;
        this->peak_ = 0;
        return this->differences() == 0;
      }
      int differences() const
      {
        // Native tracking can return with the cursor over the sampled half.
        HideCursor();
        int changed = 0;
        for (short y = this->rect_.top; y < this->rect_.bottom; ++y)
          for (short x = this->rect_.left; x < this->rect_.right; ++x)
            if (GetPixel(x, y))
              ++changed;
        ShowCursor();
        return changed;
      }

      int observe()
      {
        const int current = this->differences();
        if (current > this->peak_)
          this->peak_ = current;
        return this->peak_;
      }

    private:
      Rect rect_;
      int peak_;
    };

    /** Idle checkpoints and the existing pre-erase receiver observe persistent
        residue; Lua separately logs transient XOR drawing. */
    class ToolboxClipEdgeTrackingScenario : public loka::testing::WholeDrawReceiver
    {
    public:
      ToolboxClipEdgeTrackingScenario()
          : phase_(OPEN),
            elapsed_(0),
            arrowValue_(0)
      {
      }
      virtual unsigned long now() const
      {
        return TickCount();
      }
      virtual void accept(const loka::testing::WholeDrawSample &) {}
      virtual void expectDraw()
      {
        // The existing full-draw bracket calls this with the window port
        // selected, before layout or erase. Preserve post-tracking residue
        // that a viewport-input repaint would otherwise remove before idle.
        // This fixture has one window; stop before intentional exposure and
        // before the driver's completion marker can open another one.
        if (this->phase_ >= EDIT && this->phase_ <= THUMB)
        {
          this->editPixels_.observe();
          this->barPixels_.observe();
        }
      }
      ScenarioAdvance advance(double elapsed,
                              app::scene::Scene *scene,
                              ToolboxWindow &window,
                              ToolboxScenePlatformController &controller,
                              dsl::testing::ScenarioAuditSink &audit,
                              dsl::SnapRecord &out)
      {
        typedef char WindowControlOffset[(offsetof(WindowRecord, controlList) == 140) ? 1 : -1];
        typedef char WindowNextOffset[(offsetof(WindowRecord, nextWindow) == 144) ? 1 : -1];
        typedef char ControlRectOffset[(offsetof(ControlRecord, contrlRect) == 8) ? 1 : -1];
        typedef char ControlTitleOffset[(offsetof(ControlRecord, contrlTitle) == 40) ? 1 : -1];
        (void)sizeof(WindowControlOffset);
        (void)sizeof(WindowNextOffset);
        (void)sizeof(ControlRectOffset);
        (void)sizeof(ControlTitleOffset);
        this->elapsed_ += elapsed;
        ToolboxPortScope port(window.window());
        app::scene::Node *edit = 0;
        dsl::FlowError error;
        app::scene::BoundaryNode *root = dsl::testing::SceneTestAccess::rootBoundary(*scene);
        if (!root || root->propsTypeId() != ClipEdgeProps::staticTypeId()
            || dsl::testing::LookupNodeById<app::scene::Node>(scene, "ClipEdge.Edit", edit, error)
                   != dsl::FLOW_STEP_SUCCEEDED
            || !edit || !edit->getContext())
          return this->fail(out, "missing scene/edit");
        ClipEdgeNode &owner = *static_cast<ClipEdgeNode *>(root);
        ToolboxScenePlatformController::EditTextGeometry geometry;
        if (!controller.queryEditTextGeometryForTesting(static_cast<ToolboxEditTextContext *>(edit->getContext()),
                                                        geometry))
          return this->fail(out, "missing native edit geometry");
        ControlHandle bar = 0;
        for (ControlHandle c =
                 reinterpret_cast<ControlHandle>(reinterpret_cast<WindowPeek>(window.window())->controlList);
             c;
             c = (**c).nextControl)
          if ((**c).contrlRect.right - (**c).contrlRect.left == 16 && GetControlMaximum(c) > 0)
          {
            if (bar)
              return this->fail(out, "ambiguous vertical bar");
            bar = c;
          }
        if (!bar)
          return this->fail(out, "missing active bar");
        const short value = GetControlValue(bar);
        dsl::SnapRecord record = SmirkBenchEditorRecord("clip-edge-tracking", this->phase_, true);
        if (this->elapsed_ > 60)
        {
          record.setInt("edit.selection.end", geometry.selectionEnd);
          record.setInt("bar.value", value);
          return this->fail(out, "gesture timeout", &record);
        }
        switch (this->phase_)
        {
        case OPEN:
        {
          Rect editClip, barClip;
          const Rect expectedEditClip = ClipEdgeGeometry::rect(12, 12, 92, 36);
          const Rect expectedBarClip = ClipEdgeGeometry::rect(12, 60, 164, 180);
          if (!queryClip(scene, "ClipEdge.EditClip", editClip) || !queryClip(scene, "ClipEdge.BarClip", barClip))
            return this->fail(out, "OPEN clip placement absent");
          recordRect(record, "edit.clip", editClip);
          recordRect(record, "bar.clip", barClip);
          recordRect(record, "edit.full", geometry.placement.full());
          recordRect(record, "edit.visible", geometry.placement.visible());
          recordRect(record, "bar.full", (**bar).contrlRect);
          if (!EqualRect(&editClip, &expectedEditClip) || !EqualRect(&barClip, &expectedBarClip))
            return this->fail(out, "OPEN clip rectangles differ from Lua pair", &record);
          const Rect full = ClipEdgeGeometry::rect(13, 14, 211, 31);
          const Rect visible = ClipEdgeGeometry::rect(13, 14, 92, 31);
          const Rect expectedBar = ClipEdgeGeometry::bar();
          if (!EqualRect(&geometry.placement.full(), &full) || !EqualRect(&geometry.placement.visible(), &visible)
              || !EqualRect(&geometry.view, &full) || !EqualRect(&(**bar).contrlRect, &expectedBar) || value != 0)
            return this->fail(out, "OPEN layout/value differs from ClipEdgeGeometry/Lua pair", &record);
          if (!this->editPixels_.capture(ClipEdgeGeometry::editOutside())
              || !this->barPixels_.capture(ClipEdgeGeometry::barOutside()))
            return this->fail(out, "OPEN outside is not all white ground");
          record.set("checkpoint", "ready");
          break;
        }
        case EDIT:
        {
          // Reaching the entire 23-byte line proves the drag passed the 80px cut.
          if (geometry.selectionEnd < 23)
            return SCENARIO_ADVANCE_PENDING;
          record.setInt("edit.selection.end", geometry.selectionEnd);
          const int changed = this->editPixels_.observe();
          record.setInt("edit.outside.changed", changed);
          if (changed)
            return this->fail(out, "edit.outside.changed > 0", &record);
          break;
        }
        case CLIPPED:
          if (owner.acknowledgements() < 1)
            return SCENARIO_ADVANCE_PENDING;
          record.setInt("bar.clipped.click", value);
          if (value != 0)
            return this->fail(out, "bar.clipped.click changed value", &record);
          break;
        case ARROW:
        {
          if (owner.acknowledgements() < 2)
            return SCENARIO_ADVANCE_PENDING;
          record.set("bar.track.value>0", value > 0 ? "yes" : "no");
          const int changed = this->barPixels_.observe();
          record.setInt("bar.outside.changed", changed);
          if (value <= 0 || changed)
            return this->fail(out, "arrow value/outside", &record);
          this->arrowValue_ = value;
          break;
        }
        case THUMB:
        {
          if (owner.acknowledgements() < 3)
            return SCENARIO_ADVANCE_PENDING;
          record.set("bar.thumb.moved", value > this->arrowValue_ ? "yes" : "no");
          const int changed = this->barPixels_.observe();
          record.setInt("bar.outside.changed", changed);
          if (value <= this->arrowValue_ || changed)
            return this->fail(out, "thumb value/outside", &record);
          owner.expose();
          break;
        }
        case EXPOSED:
        {
          const int ink = this->barPixels_.differences();
          record.setInt("bar.exposed.ink", ink);
          Rect clip;
          const Rect expected = ClipEdgeGeometry::rect(12, 60, 172, 180);
          const Rect full = ClipEdgeGeometry::bar();
          if (!queryClip(scene, "ClipEdge.BarClip", clip) || !EqualRect(&clip, &expected)
              || !EqualRect(&(**bar).contrlRect, &full))
            return this->fail(out, "clip-only change moved/resized the control or did not expose it", &record);
          if (!ink)
            return this->fail(out, "bar.exposed.ink = 0", &record);
          out = record;
          this->phase_ = DONE;
          return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
        }
        case DONE:
          return this->fail(out, "advanced after completion");
        }
        if (!audit.recordVerdict(record))
          return this->fail(out, "audit write failed");
        this->phase_ = static_cast<Phase>(this->phase_ + 1);
        this->elapsed_ = 0;
        return SCENARIO_ADVANCE_PENDING;
      }

    private:
      static void recordRect(dsl::SnapRecord &record, const char *name, const Rect &rect)
      {
        const std::string prefix(name);
        record.setInt((prefix + ".left").c_str(), rect.left);
        record.setInt((prefix + ".top").c_str(), rect.top);
        record.setInt((prefix + ".right").c_str(), rect.right);
        record.setInt((prefix + ".bottom").c_str(), rect.bottom);
      }
      static bool queryClip(app::scene::Scene *scene, const char *id, Rect &out)
      {
        app::scene::Node *node = 0;
        dsl::FlowError error;
        if (dsl::testing::LookupNodeById<app::scene::Node>(scene, id, node, error) != dsl::FLOW_STEP_SUCCEEDED || !node
            || !node->getContext() || !node->getContext()->asClipBoxContext())
          return false;
        return static_cast<ToolboxClipBoxContext *>(node->getContext())->placement().query(out);
      }
      enum Phase
      {
        OPEN,
        EDIT,
        CLIPPED,
        ARROW,
        THUMB,
        EXPOSED,
        DONE
      };
      ScenarioAdvance fail(dsl::SnapRecord &out, const char *why, const dsl::SnapRecord *metrics = 0)
      {
        out = metrics ? *metrics : SmirkBenchEditorRecord("clip-edge-tracking", this->phase_, false);
        out.set("status", dsl::SnapStatusError());
        out.set("reason", why);
        this->phase_ = DONE;
        return SCENARIO_ADVANCE_DRIVER_COMPLETION_READY;
      }
      Phase phase_;
      double elapsed_;
      short arrowValue_;
      ClipEdgePixels editPixels_, barPixels_;
    };
  } // namespace scenario_tests
} // namespace loka
#endif
