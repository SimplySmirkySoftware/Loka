#ifndef LOKA_TOOLBOX_TEXT_MEASUREMENT_PINS_HPP
#define LOKA_TOOLBOX_TEXT_MEASUREMENT_PINS_HPP
#include "context/ToolboxTextContext.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "support/LifecycleFactTestAccess.hpp"

namespace loka
{
  namespace testing
  {
    class ToolboxTextContextAccess
    {
    public:
      static Rect rect(const ToolboxTextContext &c)
      {
        return c.rect_;
      }
      static Rect paintRect(const ToolboxTextContext &c)
      {
        return c.paintRect_;
      }
      static short baseline(const ToolboxTextContext &c)
      {
        return c.textY_;
      }
    };
  } // namespace testing
} // namespace loka
namespace
{
  using namespace loka::core;
  class MeasurementBoundary : public BoundaryNodeFor<MeasurementBoundary>
  {
  public:
    MeasurementBoundary(const BoundaryPropsFor<MeasurementBoundary> &p = BoundaryPropsFor<MeasurementBoundary>())
        : BoundaryNodeFor<MeasurementBoundary>(p)
    {
    }
    virtual void composeNode(NodeComposition &) {}
  };
  int MeasurementCalls()
  {
    return toolbox_host::metrics + toolbox_host::widths + toolbox_host::measures;
  }
  Rect MeasurementRect(const ToolboxTextContext &c)
  {
    return loka::testing::ToolboxTextContextAccess::rect(c);
  }
  Rect MeasurementRect(const ToolboxAttributedTextContext &c)
  {
    return ToolboxAttributedTextContextAccess::rect(c);
  }
  Rect MeasurementClip(const ToolboxTextContext &c)
  {
    return loka::testing::ToolboxTextContextAccess::paintRect(c);
  }
  Rect MeasurementClip(const ToolboxAttributedTextContext &c)
  {
    return ToolboxAttributedTextContextAccess::paintRect(c);
  }

  void RepaintRefused(ToolboxTextContext &context, ToolboxScenePlatformController &)
  {
    context.repaint();
  }
  void RepaintRefused(ToolboxAttributedTextContext &context, ToolboxScenePlatformController &controller)
  {
    context.render(&controller);
  }

  template <class Context, class NodeType>
  void RepeatWidthPlacementAndRefusal(NodeType &node, Context &context, ToolboxScenePlatformController &controller)
  {
    LayoutState seat = Seat(40);
    seat.lineHeight = 14;
    toolbox_host::reset();
    node.layout(&controller, seat);
    LOKA_VERIFY(MeasurementCalls() > 0);
    const Rect original = MeasurementRect(context);
    const int first = MeasurementCalls();
    seat = Seat(40);
    seat.lineHeight = 14;
    seat.y = 70;
    seat.x = 17;
    seat.spacing = 7;
    SetRect(&controller.projectionClip, 19, 72, 35, 78);
    node.layout(&controller, seat);
    LOKA_VERIFY(MeasurementCalls() == first);
    const Rect moved = MeasurementRect(context);
    LOKA_VERIFY(moved.top == 70 && moved.left == 17);
    LOKA_VERIFY(moved.bottom - moved.top == original.bottom - original.top);
    LOKA_VERIFY(seat.y == moved.bottom);
    const Rect clipped = MeasurementClip(context);
    LOKA_VERIFY(clipped.left == 19 && clipped.top == 72 && clipped.right == 35 && clipped.bottom == 78);
    Pin("PlainClippedOutRenderSkipsScopeClipAndHit");
    {
      // S1 lane: a placement entirely outside the projection clip renders
      // nothing: no font selection (measure scope), no NewRgn, no hit row.
      SetRect(&controller.projectionClip, 200, 200, 220, 220);
      node.layout(&controller, seat);
      const Rect hidden = MeasurementClip(context);
      LOKA_VERIFY(EmptyRect(&hidden));
      const int fonts = toolbox_host::fonts;
      const int regions = toolbox_host::regions;
      const int hits = toolbox_host::textHits;
      context.render(&controller);
      LOKA_VERIFY(toolbox_host::fonts == fonts && toolbox_host::regions == regions && toolbox_host::textHits == hits);
    }
    SetRect(&controller.projectionClip, -30000, -30000, 30000, 30000);
    seat = Seat(25);
    seat.lineHeight = 14;
    node.layout(&controller, seat);
    LOKA_VERIFY(MeasurementCalls() > first);

    // Intrinsic output must not replace the zero/nonpositive input key.
    seat = Seat(0);
    seat.lineHeight = 14;
    const short intrinsic = node.layout(&controller, seat);
    LOKA_VERIFY(intrinsic > 0);
    int before = MeasurementCalls();
    seat = Seat(0);
    seat.lineHeight = 14;
    LOKA_VERIFY(node.layout(&controller, seat) == intrinsic);
    LOKA_VERIFY(MeasurementCalls() == before);
    seat = Seat(-1);
    seat.lineHeight = 14;
    node.layout(&controller, seat);
    LOKA_VERIFY(MeasurementCalls() > before);

    NotifySubtreeNodeDetached(&node);
    LifecycleFactTestAccess::DeliverFacts(&node);
    NotifySubtreeNodeAttached(&node);
    LifecycleFactTestAccess::DeliverFacts(&node);
    before = MeasurementCalls();
    seat = Seat(-1);
    seat.lineHeight = 14;
    node.layout(&controller, seat);
    LOKA_VERIFY(MeasurementCalls() > before);
    before = MeasurementCalls();
    seat = Seat(-1);
    seat.lineHeight = 14;
    node.layout(&controller, seat);
    LOKA_VERIFY(MeasurementCalls() == before);

    // Refuse after a completed result, at the SAME constraint. The input mark
    // must survive refusal along with a queued controller retry.
    NotifySubtreeNodeAttached(&node);
    LifecycleFactTestAccess::DeliverFacts(&node);
    ToolboxWindow *window = controller.window_;
    controller.window_ = 0;
    seat = Seat(-1);
    seat.lineHeight = 14;
    seat.x = 80;
    seat.y = 90;
    LOKA_VERIFY(node.layout(&controller, seat) == 0);
    LOKA_VERIFY(controller.relayoutRetries.pending());
    for (int retry = 0; retry != 3; ++retry)
    {
      controller.relayoutRetries.flush(controller, node, seat);
      LOKA_VERIFY(controller.relayoutRetries.pending());
    }
    controller.window_ = window;
    // AttributedText can recover its projection before retry layout, retaining
    // the last placement. Plain Text keeps its separate placement refusal.
    const int hits = toolbox_host::textHits;
    for (int regions = 0; regions <= 2; regions += 2)
    {
      toolbox_host::draws.clear();
      toolbox_host::failRegions = regions;
      context.render(&controller);
      RepaintRefused(context, controller);
      if (node.asAttributedTextNode())
        LOKA_VERIFY(!toolbox_host::draws.empty());
      else
      {
        LOKA_VERIFY(toolbox_host::draws.empty());
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        LOKA_VERIFY(context.queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
      }
      LOKA_VERIFY(toolbox_host::textHits == hits);
    }
    toolbox_host::failRegions = 0;
    before = MeasurementCalls();
    seat = Seat(-1);
    seat.lineHeight = 14;
    controller.relayoutRetries.flush(controller, node, seat);
    LOKA_VERIFY(!controller.relayoutRetries.pending());
    LOKA_VERIFY(MeasurementCalls() > before);
    before = MeasurementCalls();
    seat = Seat(-1);
    seat.lineHeight = 14;
    node.layout(&controller, seat);
    LOKA_VERIFY(MeasurementCalls() == before);
    std::printf("measurement pin: first=%d retry=%d hit=0\n", first, before);
  }

  void PlainWrappedPaintPins()
  {
    Pin("PlainWrappedPaintSharesMeasurement");
    ToolboxWindow window;
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
    for (int mode = 0; mode != 2; ++mode)
    {
      TextDefinitionWithAttr definition = Text("abcdefgh ijkl mnop")
          + BlockStyle().wrap(mode == 0 ? TEXT_WRAP_WORD : TEXT_WRAP_CHAR);
      TextNode *node = static_cast<TextNode *>(definition.create());
      LOKA_VERIFY(node);
      LayoutState seat = Seat(40);
      seat.lineHeight = 14;
      NodeContext *installed = controller.nodeHandlerRegistry_.find(node)->ensureContext(node, &controller, seat);
      LOKA_VERIFY(installed);
      ToolboxTextContext &context = *static_cast<ToolboxTextContext *>(installed);
      toolbox_host::reset();
      node->layout(&controller, seat);
      const Rect rect = MeasurementRect(context);
      LOKA_VERIFY(rect.bottom - rect.top >= 28);
      context.repaint();
      LOKA_VERIFY(!toolbox_host::draws.empty());
      std::printf("wrap paint probe: height=%d draws=%lu first=%s\n", rect.bottom - rect.top,
          static_cast<unsigned long>(toolbox_host::draws.size()), toolbox_host::draws[0].bytes.c_str());
      std::fflush(stdout);
      LOKA_VERIFY(toolbox_host::draws.size() >= 2);
      std::string joined;
      for (std::size_t i = 0; i < toolbox_host::draws.size(); ++i)
      {
        joined += toolbox_host::draws[i].bytes;
        LOKA_VERIFY(toolbox_host::draws[i].y == rect.top + 12 + static_cast<int>(i) * 14);
      }
      LOKA_VERIFY(joined == "abcdefgh ijkl mnop");
      LOKA_VERIFY(rect.bottom - rect.top == static_cast<int>(toolbox_host::draws.size()) * 14 + 4);
      const std::vector<toolbox_host::Draw> first = toolbox_host::draws;
      const int calls = MeasurementCalls();
      seat = Seat(40);
      seat.lineHeight = 14;
      node->layout(&controller, seat);
      toolbox_host::draws.clear();
      context.repaint();
      LOKA_VERIFY(MeasurementCalls() == calls);
      LOKA_VERIFY(toolbox_host::draws.size() == first.size());
      for (std::size_t i = 0; i < first.size(); ++i)
      {
        LOKA_VERIFY(toolbox_host::draws[i].bytes == first[i].bytes);
        LOKA_VERIFY(toolbox_host::draws[i].y == first[i].y);
      }
      seat = Seat(20);
      seat.lineHeight = 14;
      node->layout(&controller, seat);
      LOKA_VERIFY(MeasurementCalls() > calls);
      toolbox_host::draws.clear();
      context.repaint();
      LOKA_VERIFY(toolbox_host::draws.size() > first.size());
      joined.clear();
      for (std::size_t i = 0; i < toolbox_host::draws.size(); ++i)
        joined += toolbox_host::draws[i].bytes;
      LOKA_VERIFY(joined == "abcdefgh ijkl mnop");
      DestroyHeapNode(node);
    }
  }

  void PlainWordBoundaryPins()
  {
    Pin("PlainWordBoundariesAndCharacterFallback");
    ToolboxWindow window;
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
    const char *texts[] = {"ab cdef", "ab cdef", "abcdefgh", "ab\tcdef", "ab  cdef", "abcd ef"};
    const char *firstLines[] = {"ab ", "ab c", "abcd", "ab\t", "ab  ", "abcd"};
    const char *secondLines[] = {"cdef", "def", "efgh", "cdef", "cdef", " ef"};
    for (std::size_t i = 0; i < sizeof(texts) / sizeof(texts[0]); ++i)
    {
      TextDefinitionWithAttr definition = Text(texts[i]) + FontSize<12>()
          + BlockStyle().wrap(i == 1 ? TEXT_WRAP_CHAR : TEXT_WRAP_WORD);
      TextNode *node = static_cast<TextNode *>(definition.create());
      LOKA_VERIFY(node);
      LayoutState seat = Seat(16);
      NodeContext *installed = controller.nodeHandlerRegistry_.find(node)->ensureContext(node, &controller, seat);
      LOKA_VERIFY(installed);
      ToolboxTextContext &context = *static_cast<ToolboxTextContext *>(installed);
      toolbox_host::reset();
      node->layout(&controller, seat);
      const Rect rect = MeasurementRect(context);
      context.repaint();
      LOKA_VERIFY(toolbox_host::draws.size() == 2);
      LOKA_VERIFY(toolbox_host::draws[0].bytes == firstLines[i]);
      LOKA_VERIFY(toolbox_host::draws[1].bytes == secondLines[i]);
      const int pitch = toolbox_host::draws[1].y - toolbox_host::draws[0].y;
      LOKA_VERIFY(pitch > 0);
      LOKA_VERIFY(rect.bottom - rect.top == 2 * pitch);
      const int calls = MeasurementCalls();
      seat = Seat(16);
      node->layout(&controller, seat);
      toolbox_host::draws.clear();
      context.repaint();
      LOKA_VERIFY(MeasurementCalls() == calls);
      LOKA_VERIFY(toolbox_host::draws.size() == 2);
      LOKA_VERIFY(toolbox_host::draws[0].bytes == firstLines[i]);
      LOKA_VERIFY(toolbox_host::draws[1].bytes == secondLines[i]);
      DestroyHeapNode(node);
    }
  }

  void PlainWrappedEdgePins()
  {
    loka::core::testing::failLokaAllocRaw("ToolboxPlainText", "Lines", 0);
    {
      Pin("PlainWrappedBreaksStorageRefusalAndEllipsis");
      ToolboxWindow window;
      ToolboxScenePlatformController controller(&window);
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
      TextDefinitionWithAttr definition = Text("a\nb\r\n") + BlockStyle().wrap(TEXT_WRAP_WORD);
      TextNode *node = static_cast<TextNode *>(definition.create());
      LOKA_VERIFY(node);
      LayoutState seat = Seat(40);
      NodeContext *installed = controller.nodeHandlerRegistry_.find(node)->ensureContext(node, &controller, seat);
      LOKA_VERIFY(installed);
      ToolboxTextContext &context = *static_cast<ToolboxTextContext *>(installed);
      seat.lineHeight = 14;
      node->layout(&controller, seat);
      toolbox_host::draws.clear();
      context.repaint();
      LOKA_VERIFY(toolbox_host::draws.size() == 4);
      LOKA_VERIFY(toolbox_host::draws[0].bytes == "a" && toolbox_host::draws[1].bytes == "b");
      LOKA_VERIFY(toolbox_host::draws[2].bytes.empty() && toolbox_host::draws[3].bytes.empty());
      LOKA_VERIFY(MeasurementRect(context).bottom - MeasurementRect(context).top == 60);

      // Spill beyond the breaker's 32 inline rows, and cross the Pascal limit.
      const std::string longText(300, 'a');
      TextDefinitionWithAttr longDefinition = Text(loka::core::String(longText)) + BlockStyle().wrap(TEXT_WRAP_CHAR);
      LOKA_VERIFY(longDefinition.applyPropsToNode(node));
      for (int width = 4; width <= 2000; width += 1996)
      {
        seat = Seat(static_cast<short>(width));
        seat.lineHeight = 14;
        node->layout(&controller, seat);
        toolbox_host::draws.clear();
        context.repaint();
        LOKA_VERIFY(toolbox_host::draws.size() == (width == 4 ? 300u : 2u));
        std::string joined;
        for (std::size_t i = 0; i < toolbox_host::draws.size(); ++i)
          joined += toolbox_host::draws[i].bytes;
        LOKA_VERIFY(joined == longText);
      }
      const std::string utf8 = std::string(254, 'a') + "\xc3\xa9Z";
      TextDefinitionWithAttr utf8Definition = Text(loka::core::String(utf8)) + BlockStyle().wrap(TEXT_WRAP_CHAR);
      LOKA_VERIFY(utf8Definition.applyPropsToNode(node));
      seat = Seat(2000);
      seat.lineHeight = 14;
      node->layout(&controller, seat);
      toolbox_host::draws.clear();
      context.repaint();
      LOKA_VERIFY(toolbox_host::draws.size() == 2);
      LOKA_VERIFY(toolbox_host::draws[0].bytes == std::string(254, 'a') + "\x8E");
      LOKA_VERIFY(toolbox_host::draws[1].bytes == "Z");
      LOKA_VERIFY(longDefinition.applyPropsToNode(node));

      // Each new fallible allocation refuses atomically, including after a hit.
      const char *owners[] = {"ToolboxPlainText", "TextLineBreaker", "Managed"};
      const char *purposes[] = {"Lines", "Table", "ControlBlock"};
      for (int failure = 0; failure != 3; ++failure)
      {
        loka::core::testing::failLokaAllocRaw(owners[failure], purposes[failure], 1);
        seat = Seat(4);
        seat.lineHeight = static_cast<short>(15 + failure);
        LOKA_VERIFY(node->layout(&controller, seat) == 0);
        toolbox_host::draws.clear();
        context.repaint();
        LOKA_VERIFY(toolbox_host::draws.empty());
        const Rect refused = MeasurementRect(context);
        LOKA_VERIFY(EmptyRect(&refused));
        seat = Seat(4);
        seat.lineHeight = static_cast<short>(15 + failure);
        LOKA_VERIFY(node->layout(&controller, seat) == 4);
        toolbox_host::draws.clear();
        context.repaint();
        LOKA_VERIFY(toolbox_host::draws.size() == 300);
      }
      TextDefinitionWithAttr tooTall = Text(loka::core::String(std::string(2400, 'a')))
          + BlockStyle().wrap(TEXT_WRAP_CHAR);
      LOKA_VERIFY(tooTall.applyPropsToNode(node));
      seat = Seat(4);
      seat.lineHeight = 14;
      LOKA_VERIFY(node->layout(&controller, seat) == 0);
      toolbox_host::draws.clear();
      context.repaint();
      LOKA_VERIFY(toolbox_host::draws.empty());

      TextDefinitionWithAttr explicitSize = Text("abcdefgh ijkl mnop") + FontSize<12>()
          + BlockStyle().wrap(TEXT_WRAP_WORD).align(TEXT_ALIGN_RIGHT);
      LOKA_VERIFY(explicitSize.applyPropsToNode(node));
      seat = Seat(40);
      seat.lineHeight = 14;
      node->layout(&controller, seat);
      toolbox_host::draws.clear();
      context.repaint();
      LOKA_VERIFY(toolbox_host::draws.size() == 2);
      LOKA_VERIFY(toolbox_host::draws[0].y == 32 && toolbox_host::draws[1].y == 49);
      LOKA_VERIFY(toolbox_host::draws[0].x == 14 && toolbox_host::draws[1].x == 14);
      LOKA_VERIFY(MeasurementRect(context).bottom - MeasurementRect(context).top == 34);
      toolbox_host::draws.clear();
      toolbox_host::failRegions = 2;
      context.repaint();
      LOKA_VERIFY(toolbox_host::draws.size() == 2);
      toolbox_host::failRegions = 0;

      // Ellipsis keeps precedence over wrap and its legacy terminal baseline.
      TextDefinitionWithAttr ellipsis = Text("abcdefgh ijkl mnop")
          + BlockStyle().wrap(TEXT_WRAP_WORD).truncation(TEXT_TRUNCATION_ELLIPSIS);
      LOKA_VERIFY(ellipsis.applyPropsToNode(node));
      seat = Seat(40);
      seat.lineHeight = 14;
      node->layout(&controller, seat);
      const int before = MeasurementCalls();
      toolbox_host::draws.clear();
      context.repaint();
      LOKA_VERIFY(toolbox_host::draws.size() == 1 && toolbox_host::draws[0].bytes == "abcdefg...");
      LOKA_VERIFY(toolbox_host::draws[0].y == 46);
      LOKA_VERIFY(MeasurementCalls() > before);
      DestroyHeapNode(node);
    }
    loka::core::testing::allowLokaAllocRaw();
  }

  void MeasurementPins(bool plainPins, bool attributedPins)
  {
    if (plainPins)
    {
      PlainWordBoundaryPins();
      PlainWrappedPaintPins();
      PlainWrappedEdgePins();
    }
    ToolboxWindow window;
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
    MeasurementBoundary owner;
    MutableState<String> plain(String::Literal("abcdefgh ijkl"));
    MutableState<TextStyle> style((TextStyle()));
    MutableState<AttributedString> attributed(Styled("abcdefgh ijkl", FontSize<12>()));
    PushStateTracker tracker;
    tracker.addState(&plain);
    tracker.addState(&style);
    tracker.addState(&attributed);
    if (plainPins)
    {
      TextDefinitionWithAttr definition = Text(&plain) + &style;
      TextNode *node = static_cast<TextNode *>(definition.create());
      LOKA_VERIFY(node);
      LayoutState seat = Seat(40);
      NodeContext *installed = controller.nodeHandlerRegistry_.find(node)->ensureContext(node, &controller, seat);
      LOKA_VERIFY(installed);
      ToolboxTextContext &context = *static_cast<ToolboxTextContext *>(installed);
      Pin("PlainMeasurementReusePlacementWidthParkingRefusal");
      RepeatWidthPlacementAndRefusal(*node, context, controller);
      BoundaryNode::declareBoundaryDirtySources(node, &owner);
      for (int route = 0; route < 3; ++route)
      {
        seat = Seat(40);
        seat.lineHeight = 14;
        node->layout(&controller, seat);
        const int before = MeasurementCalls();
        if (route == 0)
        {
          StateTrackerGuard guard(&tracker);
          plain.set(String::Literal("new content"));
        }
        if (route == 1)
        {
          StateTrackerGuard guard(&tracker);
          style.set(FontSize<24>());
        }
        if (route == 2)
        {
          TextDefinitionWithAttr replacement = Text(&plain) + &style + BlockStyle().wrap(TEXT_WRAP_CHAR);
          LOKA_VERIFY(replacement.applyPropsToNode(node));
        }
        seat = Seat(40);
        seat.lineHeight = 14;
        node->layout(&controller, seat);
        LOKA_VERIFY(MeasurementCalls() > before);
      }
      Pin("PlainWrappedGeometryBothPolicies");
      for (int wrap = 0; wrap < 2; ++wrap)
      {
        TextDefinitionWithAttr wrapped =
            Text("abcdefgh") + FontSize<12>() + BlockStyle().wrap(wrap == 0 ? TEXT_WRAP_CHAR : TEXT_WRAP_WORD);
        LOKA_VERIFY(wrapped.applyPropsToNode(node));
        seat = Seat(20);
        seat.lineHeight = 14;
        node->layout(&controller, seat);
        const Rect rect = MeasurementRect(context);
        LOKA_VERIFY(rect.bottom - rect.top == 34);
        LOKA_VERIFY(loka::testing::ToolboxTextContextAccess::baseline(context) == 32);
        const int before = MeasurementCalls();
        seat = Seat(20);
        seat.lineHeight = 14;
        node->layout(&controller, seat);
        LOKA_VERIFY(MeasurementCalls() == before);
      }
      // No explicit size: lineHeight changes both height and baseline.
      Text unset = Text(&plain);
      LOKA_VERIFY(unset.applyPropsToNode(node));
      seat = Seat(40);
      seat.lineHeight = 14;
      node->layout(&controller, seat);
      const short baseline = loka::testing::ToolboxTextContextAccess::baseline(context);
      int before = MeasurementCalls();
      seat = Seat(40);
      seat.lineHeight = 20;
      node->layout(&controller, seat);
      LOKA_VERIFY(MeasurementCalls() > before);
      LOKA_VERIFY(loka::testing::ToolboxTextContextAccess::baseline(context) == baseline + 6);
      Pin("PlainMeasurementHitStillFitsEllipsis");
      TextDefinitionWithAttr ellipsis = Text("abcdefgh") + BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
      LOKA_VERIFY(ellipsis.applyPropsToNode(node));
      seat = Seat(25);
      node->layout(&controller, seat);
      before = MeasurementCalls();
      seat = Seat(25);
      node->layout(&controller, seat);
      LOKA_VERIFY(MeasurementCalls() == before);
      toolbox_host::draws.clear();
      context.render(&controller);
      LOKA_VERIFY(MeasurementCalls() > before);
      LOKA_VERIFY(toolbox_host::draws.size() == 1 && toolbox_host::draws[0].bytes == "abc...");
      DestroyHeapNode(node);
    }
    owner.clearObservedStateEntries();
    if (attributedPins)
    {
      AttributedText definition(&attributed);
      AttributedTextNode *node = static_cast<AttributedTextNode *>(definition.create());
      LOKA_VERIFY(node);
      LayoutState seat = Seat(40);
      NodeContext *installed = controller.nodeHandlerRegistry_.find(node)->ensureContext(node, &controller, seat);
      LOKA_VERIFY(installed);
      ToolboxAttributedTextContext &context = *static_cast<ToolboxAttributedTextContext *>(installed);
      Pin("AttributedMeasurementReusePlacementWidthParkingRefusal");
      RepeatWidthPlacementAndRefusal(*node, context, controller);
      BoundaryNode::declareBoundaryDirtySources(node, &owner);
      for (int route = 0; route < 3; ++route)
      {
        seat = Seat(40);
        node->layout(&controller, seat);
        const int before = MeasurementCalls();
        if (route < 2)
        {
          StateTrackerGuard guard(&tracker);
          attributed.set(route == 0 ? Styled("new content", FontSize<12>()) : Styled("new content", FontSize<24>()));
        }
        else
        {
          AttributedTextDefinitionWithAttr replacement =
              AttributedText(&attributed) + BlockStyle().wrap(TEXT_WRAP_CHAR);
          LOKA_VERIFY(replacement.applyPropsToNode(node));
        }
        seat = Seat(40);
        node->layout(&controller, seat);
        LOKA_VERIFY(MeasurementCalls() > before);
      }
      DestroyHeapNode(node);
    }
    owner.clearObservedStateEntries();
  }
} // namespace
#endif
