#include "ToolboxScenePlatformContractTests.hpp"

#include "support/TestVerify.hpp"

#include "../apple/toolbox/src/ToolboxEnabledChangeDispatch.hpp"
#include "../apple/toolbox/src/ToolboxPropsRefresh.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/Text.hpp"
#include "core/State.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace
{
  class ToolboxEnabledChangeProbe
  {
  public:
    ToolboxEnabledChangeProbe()
        : bindingPath_(this)
    {
      for (int i = 0; i < TOOLBOX_ENABLED_CONTROL_KIND_COUNT; ++i)
      {
        for (int row = 0; row < 3; ++row)
        {
          this->bindings_[i][row].enabled = 0;
          this->bindings_[i][row].projectedEnabled = true;
        }
      }
    }

    void bind(loka::core::State<bool> *enabled)
    {
      this->bindingPath_.bind(enabled);
    }

    bool enabledChangeDispatchReady() const
    {
      return true;
    }

    bool applyEnabledChangeForKind(ToolboxEnabledControlKind kind,
                                   loka::core::State<bool> *enabled)
    {
      return ApplyToolboxEnabledChangeToBindings(
          this->bindings_[kind], this->bindings_[kind] + 3, enabled,
          *this, &ToolboxEnabledChangeProbe::apply);
    }

    struct Binding
    {
      loka::core::State<bool> *enabled;
      bool projectedEnabled;
    };

    void apply(Binding &binding)
    {
      binding.projectedEnabled = binding.enabled->get();
    }

    Binding bindings_[TOOLBOX_ENABLED_CONTROL_KIND_COUNT][3];

  private:
    ToolboxEnabledStateBindingPath<ToolboxEnabledChangeProbe,
                                   loka::core::State<bool> > bindingPath_;
  };
} // namespace

void testToolboxEnabledChangeUpdatesEveryMatchingControlKind()
{
  loka::core::PushStateTracker tracker;
  loka::core::MutableState<bool> enabled(true);
  loka::core::MutableState<bool> unrelated(false);
  ToolboxEnabledChangeProbe probe;
  for (int kind = 0; kind < TOOLBOX_ENABLED_CONTROL_KIND_COUNT; ++kind)
  {
    probe.bindings_[kind][0].enabled = &enabled;
    probe.bindings_[kind][1].enabled = &unrelated;
    probe.bindings_[kind][2].enabled = &enabled;
  }
  probe.bind(&enabled);
  {
    loka::core::StateTrackerGuard transaction(&tracker);
    enabled.set(false);
  }

  for (int kind = 0; kind < TOOLBOX_ENABLED_CONTROL_KIND_COUNT; ++kind)
  {
    LOKA_VERIFY(!probe.bindings_[kind][0].projectedEnabled);
    LOKA_VERIFY(!probe.bindings_[kind][2].projectedEnabled);
    LOKA_VERIFY(probe.bindings_[kind][1].projectedEnabled);
    LOKA_VERIFY(probe.applyEnabledChangeForKind(
        static_cast<ToolboxEnabledControlKind>(kind), &enabled));
    LOKA_VERIFY(!probe.applyEnabledChangeForKind(
        static_cast<ToolboxEnabledControlKind>(kind), 0));
    LOKA_VERIFY(!ApplyToolboxEnabledChangeToBindings(
        probe.bindings_[kind], probe.bindings_[kind],
        static_cast<loka::core::State<bool> *>(&enabled),
        probe, &ToolboxEnabledChangeProbe::apply));
  }
}

namespace
{
  class RetainedButtonSink
  {
  public:
    explicit RetainedButtonSink(const loka::app::ButtonProps &props)
        : label_(props.text_->get()), nativeTitle_(label_)
    {
    }

    bool apply(const loka::app::ButtonProps &props)
    {
      const loka::core::String label = props.text_->get();
      const bool changed = ToolboxButtonProjectionChanged(
          this->label_, label, props.enabled_, props.enabled_, props.onClick_, props.onClick_);
      this->label_ = label;
      if (changed)
        this->nativeTitle_ = label;
      return changed;
    }

    loka::core::String label_;
    loka::core::String nativeTitle_;
  };

  class TextSubscriptionSink
  {
  public:
    TextSubscriptionSink() : projected_(0), notifications_(0), lastBound_(0) {}

    void bindTextState(loka::core::State<loka::core::String> *text)
    {
      if (text)
      {
        this->lastBound_ = text;
        text->bind(&TextSubscriptionSink::notify, this, false);
      }
    }
    bool hasLiveBinding(loka::core::State<loka::core::String> *text) const
    {
      return this->projected_ == text;
    }
    void unbindTextState(loka::core::State<loka::core::String> *text)
    {
      text->unbind(&TextSubscriptionSink::notify, this);
    }
    static void notify(void *data)
    {
      ++static_cast<TextSubscriptionSink *>(data)->notifications_;
    }
    loka::core::State<loka::core::String> *projected_;
    int notifications_;
    loka::core::State<loka::core::String> *lastBound_;
  };
}

void testToolboxButtonLiteralRefreshUsesLabelValue()
{
  loka::app::ButtonProps props;
  props.text("A");
  loka::core::State<loka::core::String> *const text = props.text_;
  RetainedButtonSink sink(props);
  props = loka::app::ButtonProps().text("B");
  LOKA_VERIFY(props.text_ == text);
  LOKA_VERIFY(sink.apply(props));
  const bool titleIsB = sink.nativeTitle_.equals(loka::core::String::Literal("B"));
  LOKA_VERIFY(titleIsB);
  LOKA_VERIFY(!sink.apply(props));

  loka::core::PushStateTracker tracker;
  loka::core::MutableState<loka::core::String> live(loka::core::String::Literal("B"));
  props.text(&live);
  LOKA_VERIFY(!sink.apply(props));
  {
    loka::core::StateTrackerGuard transaction(&tracker);
    live.set(loka::core::String::Literal("C"));
  }
  LOKA_VERIFY(sink.apply(props));
  const bool titleIsC = sink.nativeTitle_.equals(loka::core::String::Literal("C"));
  LOKA_VERIFY(titleIsC);
}

void testToolboxUnchangedProjectionsSkipRefresh()
{
  loka::core::MutableState<loka::core::String> text(loka::core::String::Literal("text"));
  loka::core::MutableState<bool> enabled(true);
  loka::core::EmitterState emitter;
  loka::core::MutableState<int> selected(0);
  loka::Vector<loka::core::String> items;
  // Text, Cell and EditText deliberately share this production decision.
  LOKA_VERIFY(!ToolboxTextProjectionChanged(&text, &text));
  LOKA_VERIFY(!ToolboxTextProjectionChanged(0, 0));
  LOKA_VERIFY(ToolboxTextProjectionChanged(&text, 0));
  LOKA_VERIFY(ToolboxTextProjectionChanged(0, &text));
  const loka::core::String label = text.get();
  LOKA_VERIFY(!ToolboxButtonProjectionChanged(label, label, &enabled, &enabled, &emitter, &emitter));
  LOKA_VERIFY(ToolboxButtonProjectionChanged(label, label, &enabled, 0, &emitter, &emitter));
  LOKA_VERIFY(ToolboxButtonProjectionChanged(label, label, &enabled, &enabled, &emitter, 0));
  LOKA_VERIFY(!ToolboxPopupProjectionChanged(&items, &items, &selected, &selected, &enabled, &enabled));
  LOKA_VERIFY(ToolboxPopupProjectionChanged(&items, 0, &selected, &selected, &enabled, &enabled));
  LOKA_VERIFY(ToolboxPopupProjectionChanged(&items, &items, &selected, 0, &enabled, &enabled));
  LOKA_VERIFY(ToolboxPopupProjectionChanged(&items, &items, &selected, &selected, &enabled, 0));
}

void testToolboxTextSubscriptionMovesLiveOwnedLive()
{
  loka::core::PushStateTracker tracker;
  loka::core::MutableState<loka::core::String> oldText(loka::core::String::Literal("old"));
  loka::core::MutableState<loka::core::String> newText(loka::core::String::Literal("new"));
  loka::app::TextProps props(&oldText);
  TextSubscriptionSink sink;
  sink.projected_ = props.text_;
  sink.bindTextState(&oldText);
  {
    loka::core::StateTrackerGuard transaction(&tracker);
    oldText.set(loka::core::String::Literal("positive control"));
  }
  LOKA_VERIFY(sink.notifications_ == 1);
  props = loka::app::TextProps("owned");
  LOKA_VERIFY(ToolboxTextProjectionChanged(sink.projected_, props.text_));
  loka::core::State<loka::core::String> *previous = sink.projected_;
  sink.projected_ = props.text_;
  ReconcileToolboxTextSubscription(sink, previous, ToolboxLiveTextSource(props.text_, props.ownsText()));
  LOKA_VERIFY(sink.lastBound_ == &oldText);
  {
    loka::core::StateTrackerGuard transaction(&tracker);
    oldText.set(loka::core::String::Literal("ignored"));
    props.ownedText.set(loka::core::String::Literal("never observed"));
  }
  LOKA_VERIFY(sink.notifications_ == 1);
  previous = sink.projected_;
  props = loka::app::TextProps(&newText);
  LOKA_VERIFY(ToolboxTextProjectionChanged(previous, props.text_));
  sink.projected_ = props.text_;
  ReconcileToolboxTextSubscription(sink, previous, ToolboxLiveTextSource(props.text_, props.ownsText()));
  LOKA_VERIFY(sink.lastBound_ == &newText);
  {
    loka::core::StateTrackerGuard transaction(&tracker);
    newText.set(loka::core::String::Literal("observed"));
  }
  LOKA_VERIFY(sink.notifications_ == 2);
  sink.projected_ = 0;
  ReconcileToolboxTextSubscription(sink, &newText, static_cast<loka::core::State<loka::core::String> *>(0));
}
