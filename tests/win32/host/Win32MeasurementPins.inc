#include "app/nodes/boundary/StdComposition.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;

  class MeasurementBoundary : public BoundaryNodeFor<MeasurementBoundary>
  {
  public:
    explicit MeasurementBoundary(
        const BoundaryPropsFor<MeasurementBoundary> &props = BoundaryPropsFor<MeasurementBoundary>())
        : BoundaryNodeFor<MeasurementBoundary>(props)
    {
    }
    virtual void composeNode(NodeComposition &) {}
  };

  int measurementCalls()
  {
    return win32_host::measures + win32_host::metrics + win32_host::drawMeasures;
  }

  void pinMeasurement(bool attributed)
  {
    std::printf("context pins: %s\n", attributed ? "AttributedText" : "Text");
    Win32ScenePlatformController controller;
    HostWindow root;
    controller.rootHwnd_ = &root;
    MutableState<String> text(String::Literal("sample words for wrapping"));
    MutableState<AttributedString> rich(Styled("sample words for wrapping", Bold));
    MutableState<TextStyle> style((FontSize<12>()));
    PushStateTracker tracker;
    tracker.addState(&text);
    tracker.addState(&rich);
    tracker.addState(&style);
    MeasurementBoundary boundary;
    TextProps plainProps(&text);
    plainProps.textStyleState_ = &style;
    plainProps.blockStyle_.wrap(TEXT_WRAP_WORD);
    TextNode plain(plainProps);
    AttributedTextNode decorated((AttributedText(&rich) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
    Node &node = attributed ? static_cast<Node &>(decorated) : static_cast<Node &>(plain);
    node.setPropsTypeId(attributed ? AttributedTextProps::staticTypeId() : TextProps::staticTypeId());
    if (attributed)
      node.setContext(new Win32AttributedTextContext(&controller, &root, 0, 0, 101, 20, &decorated));
    else
      node.setContext(new Win32TextContext(&controller, &root, 0, 0, 101, 20, &plain));
    HWND window = win32_host::windows.back();
    BoundaryNode::declareBoundaryDirtySources(&node, &boundary);
    LayoutState state;
    state.width = 101;
    const int before = measurementCalls();
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > before && state.height > 0);
    int calls = measurementCalls(), selections = win32_host::selections;
    const int height = state.height;
    state.x = 8;
    state.y = 23;
    state.height = 1;
    const int advance = node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() == calls && win32_host::selections == selections);
    LOKA_VERIFY(state.height == height && advance == 23 + height);
    LOKA_VERIFY(window->rect.left == 8 && window->rect.top == 23);
    std::printf("  repeat: measurement=0 SelectObject=0; placement=(8,23)\n");
    state.width = 103;
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > calls);
    calls = measurementCalls();
    {
      StateTrackerGuard guard(&tracker);
      text.set(String::Literal("changed words"));
      rich.set(Styled("changed words", Italic));
    }
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > calls);
    calls = measurementCalls();
    if (!attributed)
    {
      {
        StateTrackerGuard guard(&tracker);
        style.set(Italic);
      }
      node.layout(&controller, state);
      LOKA_VERIFY(measurementCalls() > calls);
      calls = measurementCalls();
    }
    if (attributed)
      LOKA_VERIFY(
          (AttributedText(Styled("applied", Italic)) + BlockStyle().align(TEXT_ALIGN_RIGHT)).applyPropsToNode(&node));
    else
      LOKA_VERIFY((Text("applied") + FontSize<24>() + BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_RIGHT))
                      .applyPropsToNode(&node));
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > calls);
    calls = measurementCalls();
    // A retained block-only apply reaches the #965 mark even at the same width.
    if (attributed)
      LOKA_VERIFY(
          (AttributedText(Styled("applied", Italic)) + BlockStyle().align(TEXT_ALIGN_CENTER)).applyPropsToNode(&node));
    else
      LOKA_VERIFY((Text("applied") + FontSize<24>() + BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_CENTER))
                      .applyPropsToNode(&node));
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > calls);
    calls = measurementCalls();
    NotifySubtreeNodeDetached(&node);
    LifecycleFactTestAccess::DeliverFacts(&node);
    NotifySubtreeNodeAttached(&node);
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > calls);
    calls = measurementCalls();
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() == calls);

    controller.updateDisplayScale(loka::win32::Win32DisplayScale(144));
    LOKA_VERIFY(IsRectEmpty(&window->rect));
    state.x = 0;
    state.width = 101;
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > calls);
    calls = measurementCalls();
    state.x = 1;
    node.layout(&controller, state);
    // Both rails key on the placement's native width (#1008): the odd origin
    // rounds the 144 dpi width from 152 to 151 px and rebuilds the lines.
    LOKA_VERIFY(measurementCalls() > calls);
    std::printf("  x-phase 144dpi: rebuild 152 -> 151px\n");
    if (!attributed)
    {
      // Text that generates no lines keys on no width: moving it across the
      // rounding phase neither measures nor selects a font again.
      LOKA_VERIFY((Text("label") + FontSize<24>()).applyPropsToNode(&node));
      state.x = 0;
      node.layout(&controller, state);
      calls = measurementCalls();
      selections = win32_host::selections;
      state.x = 1;
      node.layout(&controller, state);
      LOKA_VERIFY(measurementCalls() == calls && win32_host::selections == selections);
      std::printf("  x-phase 144dpi, no generated lines: hit\n");
      LOKA_VERIFY((Text("applied") + FontSize<24>() + BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_CENTER))
                      .applyPropsToNode(&node));
      node.layout(&controller, state);
      calls = measurementCalls();
    }

    // Keep the effective width equal across scales: no key miss can mask a
    // missing assignment delivery. Real CreateFontIndirectW refuses here.
    state.width = attributed ? 0 : 1;
    node.layout(&controller, state);
    calls = measurementCalls();
    win32_host::failFontAllocations = 1;
    const int allocations = win32_host::fontAllocations;
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(120));
    LOKA_VERIFY(win32_host::fontAllocations == allocations + 1);
    LOKA_VERIFY(IsRectEmpty(&window->rect));
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > calls);
    calls = measurementCalls();
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() == calls);
    // Retry succeeds without another scale assignment. It must revoke the
    // result measured under the preserved old font before the old table dies.
    controller.ensureDisplayFont();
    LOKA_VERIFY(IsRectEmpty(&window->rect));
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > calls);
    calls = measurementCalls();
    // Same-scale assignment still revokes: it does not allocate or broadcast.
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(120));
    LOKA_VERIFY(IsRectEmpty(&window->rect));
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() > calls);

    state.width = 104;
    win32_host::failMeasure = true;
    const int refusedAdvance = node.layout(&controller, state);
    // A refused leaf keeps its seat but advances by nothing: no trailing spacing (#950).
    LOKA_VERIFY(refusedAdvance == state.y && state.height == 0 && IsRectEmpty(&window->rect));
    PaintQuery query;
    query.placement = PLACEMENT_ELIGIBLE;
    query.scope = Win32RetirableContext::paintScope();
    LOKA_VERIFY(static_cast<Win32RetirableContext *>(node.getContext())->queryPaintDamage(query).kind
                == PAINT_ANSWER_REFUSED);
    calls = measurementCalls();
    LOKA_VERIFY(controller.relayoutRetries.pending());
    controller.relayoutRetries.flush(controller, node, state);
    LOKA_VERIFY(controller.relayoutRetries.pending());
    LOKA_VERIFY(state.height == 0);
    win32_host::failMeasure = false;
    controller.relayoutRetries.flush(controller, node, state);
    LOKA_VERIFY(!controller.relayoutRetries.pending());
    LOKA_VERIFY(measurementCalls() > calls && state.height > 0);
    calls = measurementCalls();
    node.layout(&controller, state);
    LOKA_VERIFY(measurementCalls() == calls);
    if (!attributed)
    {
      // Corrupt native presentation only: a hit must still reconcile it.
      window->style = SS_LEFT;
      window->dc.font = controller.displayFont();
      const int styleReads = win32_host::styleReads, fontReads = win32_host::fontReads;
      node.layout(&controller, state);
      LOKA_VERIFY(measurementCalls() == calls);
      LOKA_VERIFY(win32_host::styleReads > styleReads && win32_host::fontReads > fontReads);
      LOKA_VERIFY((window->style & SS_TYPEMASK) == SS_CENTER);
      LOKA_VERIFY(window->dc.font == controller.textFont(plain.props.resolvedTextStyle()));
      for (int refusal = 0; refusal < 3; ++refusal)
      {
        ++state.width;
        win32_host::failDC = refusal == 0;
        win32_host::failMetrics = refusal == 1;
        if (refusal == 2)
          loka::core::testing::failLokaAllocRaw("Win32AttributedText", "Break", 1);
        node.layout(&controller, state);
        LOKA_VERIFY(state.height == 0 && IsRectEmpty(&window->rect));
        win32_host::failDC = win32_host::failMetrics = win32_host::failConversion = false;
        calls = measurementCalls();
        LOKA_VERIFY(controller.relayoutRetries.pending());
        controller.relayoutRetries.flush(controller, node, state);
        LOKA_VERIFY(!controller.relayoutRetries.pending());
        LOKA_VERIFY(measurementCalls() > calls && state.height > 0);
      }
    }
    std::printf("  marks, parked reattach, font success/refusal/retry, refusal/recovery: pass\n");
  }
} // namespace
