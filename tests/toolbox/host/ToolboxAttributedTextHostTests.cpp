#include "context/ToolboxCardBandContext.hpp"
#include "context/ToolboxRibbonGroupEdgeContext.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "ToolboxHost.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "ToolboxDirtyReplay.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "context/ToolboxAttributedTextContext.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include <cstdio>
#include <map>
#include <cstdlib>
#include "platform/String.hpp"
#include "SmirkyMarkup.hpp"
#include <Script.h>

namespace loka
{
  namespace testing
  {
    class ToolboxAttributedTextContextAccess
    {
    public:
      static const ToolboxAttributedTextTable &table(const ToolboxAttributedTextContext &c)
      {
        return c.table_;
      }
      static Rect rect(const ToolboxAttributedTextContext &c)
      {
        return c.rect_;
      }
      static Rect paintRect(const ToolboxAttributedTextContext &c)
      {
        return c.paintRect_;
      }
      static bool known(const ToolboxAttributedTextContext &c)
      {
        return c.presented_.isKnown();
      }
    };
  } // namespace testing
} // namespace loka

using namespace loka::app;
using namespace loka::app::scene;
using loka::testing::ToolboxAttributedTextContextAccess;
namespace
{
  std::map<void *, std::size_t> allocations;
  std::size_t liveBytes = 0, peakBytes = 0;
  void *CountAlloc(std::size_t size, const loka::core::LokaAllocationSite &)
  {
    void *p = std::malloc(size);
    if (p)
    {
      allocations[p] = size;
      liveBytes += size;
      if (liveBytes > peakBytes) peakBytes = liveBytes;
    }
    return p;
  }
  void CountFree(void *p, const loka::core::LokaAllocationSite &)
  {
    LOKA_VERIFY(allocations.count(p) == 1);
    liveBytes -= allocations[p];
    allocations.erase(p);
    std::free(p);
  }
  void MemoryPin()
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    const AttributedString word = Styled(loka::core::String(std::string("a ") + std::string(9000, 'x')), TextStyle());
    loka::core::LokaAllocSetBackend(CountAlloc, CountFree);
    {
      ToolboxAttributedTextTable table;
      LOKA_VERIFY(table.build(word, BlockStyle().wrap(TEXT_WRAP_WORD), 40, controller));
      std::printf("9002 ASCII units: table sizeof=%lu gate held=%lu peak=%lu\n",
          static_cast<unsigned long>(sizeof(table)), static_cast<unsigned long>(liveBytes),
          static_cast<unsigned long>(peakBytes));
      // Host table budget: retaining a second document buffer or restoring the
      // synthetic decoder exceeds these independently measured limits.
      if (sizeof(std::size_t) == 8 && sizeof(TextBreakCharacter) == 32)
      {
        LOKA_VERIFY(liveBytes < 400000);
        LOKA_VERIFY(peakBytes < 420000);
      }
      else
        std::puts("[skip] x86_64 table-memory budget; sizes differ on this host");
    }
    LOKA_VERIFY(liveBytes == 0);
    loka::core::LokaAllocSetBackend(0, 0);
  }
  LayoutState Seat(short width)
  {
    LayoutState s;
    s.x = 10;
    s.y = 20;
    s.width = width;
    s.spacing = 0;
    return s;
  }
  void Repaint(ToolboxScenePlatformController &controller, ToolboxAttributedTextContext &context)
  {
    controller.rootNode_ = context.owner();
    ToolboxRenderDirtyInCompositionOrder(controller, ToolboxAttributedTextContextAccess::paintRect(context));
  }
  void Pin(const char *name)
  {
    std::printf("[pin] %s\n", name);
    std::fflush(stdout);
  }
  void InkPins()
  {
    Pin("AttributedSpanInkAndEllipsisRestore");
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    window.port.fgColor = greenColor;
    ToolboxAttributedTextTable table;
    const AttributedString value = Styled("Red", Red) + Styled("unset", TextStyle()) + Styled("Blue", Blue);
    toolbox_host::reset();
    LOKA_VERIFY(table.build(value, BlockStyle(), 200, controller));
    LOKA_VERIFY(table.draw(0, 0, controller, BlockStyle(), 200));
    LOKA_VERIFY(toolbox_host::draws.size() == 3);
    LOKA_VERIFY(toolbox_host::draws[0].ink == redColor);
    LOKA_VERIFY(toolbox_host::draws[1].ink == blackColor);
    LOKA_VERIFY(toolbox_host::draws[2].ink == blueColor);
    LOKA_VERIFY(window.port.fgColor == greenColor);
    toolbox_host::reset();
    const BlockStyle ellipsis = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    LOKA_VERIFY(table.build(value, ellipsis, 36, controller));
    LOKA_VERIFY(table.draw(0, 0, controller, ellipsis, 36));
    LOKA_VERIFY(!toolbox_host::draws.empty());
    LOKA_VERIFY(toolbox_host::draws.back().bytes == "...");
    LOKA_VERIFY(toolbox_host::draws.back().ink == blueColor);
    LOKA_VERIFY(window.port.fgColor == greenColor);
  }
  void EncodingPins()
  {
    Pin("AttributedNativeStyleWrapAndMalformedMarkup");
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
    ToolboxAttributedTextTable table;
    toolbox_host::systemScript = smRoman;
    const char markup[] = "<b>\xc3\xa9\xc0\xaf" "A</b><i>\n\x80" "B\r\n\t\0Z</i>";
    AttributedString value;
    LOKA_VERIFY(smirkycard::ParseSmirkyMarkup(markup, sizeof(markup) - 1, TextStyle(), value));
    toolbox_host::reset();
    const unsigned before = toolbox_host::scriptReads;
    LOKA_VERIFY(table.build(value, BlockStyle(), 80, controller));
    LOKA_VERIFY(toolbox_host::scriptReads == before + 1);
    LOKA_VERIFY(toolbox_host::measurePayloads.size() == 2);
    LOKA_VERIFY(toolbox_host::measurePayloads[0] == std::string("\x8e??A", 4));
    LOKA_VERIFY(toolbox_host::measurePayloads[1] == std::string("\n?B\r\n\t\0Z", 8));
    LOKA_VERIFY(table.lines().lineCount() == 3); // CRLF is one break; LF before malformed byte survives.
    LOKA_VERIFY(table.lines().fragment(0).start == 0 && table.lines().fragment(0).end == 4);
    LOKA_VERIFY(table.lines().line(0).width == 20);
    LOKA_VERIFY(table.draw(0, 0, controller, BlockStyle(), 80));
    LOKA_VERIFY(toolbox_host::draws.size() == 3);
    LOKA_VERIFY(toolbox_host::draws[0].bytes == std::string("\x8e??A", 4));
    LOKA_VERIFY(toolbox_host::draws[0].face == bold);
    LOKA_VERIFY(toolbox_host::draws[1].bytes == "?B" && toolbox_host::draws[1].face == italic);
    LOKA_VERIFY(toolbox_host::draws[2].bytes == std::string("\t\0Z", 3));
    LOKA_VERIFY(table.build(value, BlockStyle().wrap(TEXT_WRAP_CHAR), 5, controller));
    LOKA_VERIFY(table.draw(0, 0, controller, BlockStyle(), 5));
    LOKA_VERIFY(table.draw(0, 0, controller, BlockStyle(), 5));
    LOKA_VERIFY(toolbox_host::scriptReads == before + 1); // One projection, width and paint reuse.
    std::puts("projection codec entries: initial=1 width-only=0 repeated-paint=0 (system-script sample counter)");

    Pin("AttributedSegmentsNeverJoinMalformedUnits");
    const AttributedString split = Styled("\xc3", Bold) + Styled("\xa9", Bold) + Styled("\xc3\xa9", Italic);
    toolbox_host::reset();
    LOKA_VERIFY(table.build(split, BlockStyle(), 80, controller));
    LOKA_VERIFY(table.draw(0, 0, controller, BlockStyle(), 80));
    LOKA_VERIFY(toolbox_host::draws.size() == 2);
    LOKA_VERIFY(toolbox_host::draws[0].bytes == "??");
    LOKA_VERIFY(toolbox_host::draws[1].bytes == "\x8e");
    LOKA_VERIFY(table.lines().fragment(1).start == 2 && table.lines().fragment(1).end == 3);

    Pin("AttributedProjectionIdentityIncludesSegmentBoundaries");
    const AttributedString joined = Styled("\xc3\xa9", Bold) + Styled("\xc3\xa9", Italic);
    LOKA_VERIFY(joined == split); // Logical equality deliberately ignores equal-style boundaries.
    toolbox_host::reset();
    LOKA_VERIFY(table.build(joined, BlockStyle(), 80, controller));
    LOKA_VERIFY(table.draw(0, 0, controller, BlockStyle(), 80));
    LOKA_VERIFY(toolbox_host::draws[0].bytes == "\x8e");
    LOKA_VERIFY(toolbox_host::draws[1].bytes == "\x8e");

    Pin("AttributedNativeBatchBoundaryAndLargeOffsets");
    std::string utf8;
    for (int i = 0; i < 40000; ++i) utf8 += "\xc3\xa9";
    toolbox_host::reset();
    LOKA_VERIFY(table.build(Styled(loka::core::String(utf8), Bold), BlockStyle(), 0, controller));
    LOKA_VERIFY(table.draw(0, 0, controller, BlockStyle(), 0));
    LOKA_VERIFY(toolbox_host::draws.size() == 7);
    LOKA_VERIFY(toolbox_host::measurePayloads.size() == toolbox_host::draws.size());
    std::string painted;
    for (std::size_t i = 0; i < toolbox_host::draws.size(); ++i)
    {
      LOKA_VERIFY(toolbox_host::draws[i].bytes == toolbox_host::measurePayloads[i]);
      LOKA_VERIFY(toolbox_host::draws[i].length == (i == 6 ? 682 : 6553));
      painted += toolbox_host::draws[i].bytes;
    }
    LOKA_VERIFY(painted == std::string(40000, static_cast<char>(0x8e)));

    Pin("AttributedNative255WrapAndEllipsis");
    const BlockStyle wrap = BlockStyle().wrap(TEXT_WRAP_CHAR);
    const AttributedString edge = Styled(loka::core::String(std::string(254, 'a') + "\xc3\xa9" "z"), Bold);
    LOKA_VERIFY(table.build(edge, wrap, 1275, controller));
    LOKA_VERIFY(table.lines().lineCount() == 2);
    LOKA_VERIFY(table.lines().fragment(0).end == 255);
    toolbox_host::reset();
    LOKA_VERIFY(table.draw(0, 0, controller, wrap, 1275));
    LOKA_VERIFY(toolbox_host::draws[0].bytes == std::string(254, 'a') + "\x8e");
    LOKA_VERIFY(table.build(Styled(loka::core::String(utf8.substr(0, 510)), Bold), wrap, 1275, controller));
    LOKA_VERIFY(table.lines().lineCount() == 1 && table.lines().fragment(0).end == 255);
    toolbox_host::reset();
    LOKA_VERIFY(table.draw(0, 0, controller, wrap, 1275));
    LOKA_VERIFY(toolbox_host::draws[0].bytes == std::string(255, static_cast<char>(0x8e)));
    const BlockStyle dots = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    LOKA_VERIFY(table.build(Styled("\xc3\xa9\xc3\xa9\xc3\xa9", Bold), dots, 25, controller));
    toolbox_host::reset();
    LOKA_VERIFY(table.draw(0, 0, controller, dots, 25));
    LOKA_VERIFY(toolbox_host::draws.size() == 1); // Fits exactly; no truncation.
    LOKA_VERIFY(table.build(Styled("\xc3\xa9\xc3\xa9\xc3\xa9xxx", Bold), dots, 25, controller));
    toolbox_host::reset();
    LOKA_VERIFY(table.draw(0, 0, controller, dots, 25));
    LOKA_VERIFY(toolbox_host::draws[0].bytes == "\x8e\x8e" && toolbox_host::draws[1].bytes == "...");

    Pin("AttributedNonRomanAsciiFallback");
    toolbox_host::systemScript = 1;
    toolbox_host::reset();
    LOKA_VERIFY(table.build(Styled("\xc3\xa9" "A", Bold), BlockStyle(), 80, controller));
    LOKA_VERIFY(table.draw(0, 0, controller, BlockStyle(), 80));
    LOKA_VERIFY(toolbox_host::draws[0].bytes == "?A");
    toolbox_host::systemScript = smRoman;
  }

  void PaintRecoveryPins()
  {
    Pin("AttributedPaintOnlySourceChangeHistoryAndRefusalRecovery");
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
    AttributedTextNode *node = new AttributedTextNode(AttributedText(Styled("old", Bold)).props);
    LayoutState seat = Seat(80);
    ToolboxAttributedTextContext *context = static_cast<ToolboxAttributedTextContext *>(
        controller.nodeHandlerRegistry_.find(node)->ensureContext(node, &controller, seat));
    LOKA_VERIFY(context);
    context->layout(&controller, seat);
    context->render(&controller);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));
    Pin("AttributedHistorySurvivesMissedCompositionReplay");
    toolbox_host::reset();
    {
      const Rect miss = {200, 200, 220, 280};
      ToolboxPaintClip clip(miss);
      context->render(&controller);
    }
    LOKA_VERIFY(toolbox_host::draws.empty());
    const PaintQuery unchanged = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(context->queryPaintDamage(unchanged).kind == PAINT_ANSWER_EXACT);
    node->props = AttributedText(Styled("\xc3\xa9" "AB", Bold)).props;
    context->onPropsApplied();
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));
    seat = Seat(80);
    context->layout(&controller, seat);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));
    const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
    node->props = AttributedText(Styled("\xc3\xa9" "CD", Bold)).props;
    // A live-source update need not invoke onPropsApplied before paint.
    toolbox_host::reset();
    context->render(&controller); // No layout between logical update and paint.
    LOKA_VERIFY(toolbox_host::draws.size() == 1 && toolbox_host::draws[0].bytes == "\x8e" "CD");
    const unsigned encoded = toolbox_host::scriptReads;
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::scriptReads == encoded);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));
    const Rect placement = ToolboxAttributedTextContextAccess::rect(*context);
    node->props = AttributedText(Styled(loka::core::String(std::string(100, 'x')), Bold)).props;
    context->onPropsApplied();
    loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 1);
    controller.observation_.reset();
    toolbox_host::reset();
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::draws.empty());
    LOKA_VERIFY(controller.observation_.outsideRefusal().reason == TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(*context));
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::table(*context).valid());
    Rect refused = ToolboxAttributedTextContextAccess::rect(*context);
    LOKA_VERIFY(EqualRect(&placement, &refused));
    loka::core::testing::allowLokaAllocRaw();
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::draws.size() == 1 && toolbox_host::draws[0].bytes == std::string(100, 'x'));
    // One unbreakable word stays one line at the 80-pixel constraint, so the
    // rebuild matches the placement layout would give it and is certified.
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));
    Pin("AttributedPaintOnlyEqualValueRebuildUnderPartialClip");
    // Split malformed segments and the joined scalar compare equal but draw
    // "??" versus the e-acute byte: a paint-only rebuild under a partial clip
    // must not certify the unpainted remainder as current.
    node->props = AttributedText(Styled("\xc3", Bold) + Styled("\xa9", Bold)).props;
    context->render(&controller);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));
    node->props = AttributedText(Styled("\xc3\xa9", Bold)).props;
    {
      Rect partial = ToolboxAttributedTextContextAccess::paintRect(*context);
      partial.right = static_cast<short>(partial.left + (partial.right - partial.left) / 2);
      ToolboxPaintClip clip(partial);
      context->render(&controller);
    }
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(*context));
    Pin("AttributedIntrinsicPaintReusesGeometry");
    seat = Seat(0);
    context->layout(&controller, seat);
    const int measured = toolbox_host::measures;
    context->render(&controller);
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::measures == measured);
    Pin("AttributedPaintOnlyRebuildKeepsIntrinsicConstraint");
    // An intrinsic layout placed "a"; the live value grows before the next
    // layout. The rebuild stays unbounded and, outgrowing the placement, is
    // painted but not certified as presented.
    node->props = AttributedText(Styled("a", Bold)).props;
    context->onPropsApplied();
    seat = Seat(0);
    context->layout(&controller, seat);
    context->render(&controller);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));
    const Rect small = ToolboxAttributedTextContextAccess::rect(*context);
    node->props = AttributedText(Styled("abcdef", Bold)).props;
    controller.observation_.reset();
    context->render(&controller);
    LOKA_VERIFY(controller.observation_.outsideRefusal().reason == TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::table(*context).width() > small.right - small.left);
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(*context));
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    delete node;
    controller.retired.clear();
  }
} // namespace


#include "ToolboxTextMeasurementPins.hpp"
#include "ToolboxScrollViewPins.hpp"
#include "ToolboxToolbarPins.hpp"

namespace
{
  /** Two real sized measurements requested from a render's existing presenter door. */
  class BusyMeasurements : public ToolboxFullDrawPresenter
  {
    ToolboxScenePlatformController &controller_;

  public:
    explicit BusyMeasurements(ToolboxScenePlatformController &controller)
        : controller_(controller)
    {
    }
    virtual void eraseBeforePaint()
    {
      TextNode first((Text("First") + FontSize<18>()).props);
      TextNode second((Text("Second") + FontSize<18>()).props);
      ToolboxTextContext a(&first, &this->controller_);
      ToolboxTextContext b(&second, &this->controller_);
      LayoutState state = Seat(100);
      a.layout(&this->controller_, state);
      state = Seat(100);
      b.layout(&this->controller_, state);
    }
  };
  class NestedBusyRender : public ToolboxFullDrawPresenter
  {
    ToolboxScenePlatformController &inner_;
    BusyMeasurements &measurements_;

  public:
    NestedBusyRender(ToolboxScenePlatformController &inner, BusyMeasurements &measurements)
        : inner_(inner),
          measurements_(measurements)
    {
    }
    virtual void eraseBeforePaint()
    {
      ToolboxInputDoor::render(this->inner_, &this->measurements_);
      LOKA_VERIFY(this->inner_.cursorOwner()->depth == 1);
      LOKA_VERIFY(this->inner_.cursorOwner()->outerEntries == 1);
      LOKA_VERIFY(this->inner_.cursorOwner()->outerExits == 0);
    }
  };
  void BusyBatchPins(bool nested)
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    TextSize(12);
    CursorOwner owner;
    ToolboxScenePlatformController outer(&window, &owner), inner(&window, &owner);
    TextNode root(Text("Root").props);
    outer.rootNode_ = &root;
    inner.rootNode_ = &root;
    BusyMeasurements measurements(inner);
    if (nested)
    {
      Pin("ToolboxBusyBatchNestedInnerOnly");
      NestedBusyRender presenter(inner, measurements);
      ToolboxInputDoor::render(outer, &presenter);
    }
    else
    {
      Pin("ToolboxBusyBatchTwoMeasurements");
      ToolboxInputDoor::render(inner, &measurements);
    }
    LOKA_VERIFY(owner.entries >= 2);
    LOKA_VERIFY(owner.outerEntries == 1);
    LOKA_VERIFY(owner.outerExits == 1);
    LOKA_VERIFY(owner.depth == 0);
    outer.rootNode_ = inner.rootNode_ = 0;
  }
} // namespace

int main(int argc, char **argv)
{
  if (argc == 2 && std::string(argv[1]) == "refusal-partial")
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    AttributedTextNode node(AttributedText(Styled("paint", Bold)).props);
    ToolboxAttributedTextContext context(&node, &controller);
    LayoutState placement = Seat(80);
    context.layout(&controller, placement);
    controller.observation_.reset();
    loka::testing::failToolboxAttributedDraws(1);
    context.render(&controller);
    LOKA_VERIFY(controller.observation_.outsideRefusal().reason == TOOLBOX_PAINT_REFUSAL_PARTIAL_DRAW);
    context.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    return 0;
  }
  if (argc == 2 && (std::string(argv[1]) == "refusal-card" || std::string(argv[1]) == "refusal-ribbon"))
  {
    ToolboxWindow window;
    HostPortScope port(window.window());
    ToolboxScenePlatformController controller(&window);
    CardBandNode card((CardBandProps()));
    RibbonGroupEdgeNode ribbon((RibbonGroupEdgeProps()));
    ToolboxCardBandContext cardContext(&card, &controller);
    ToolboxRibbonGroupEdgeContext ribbonContext(&ribbon, &controller);
    NodeContext &context = std::string(argv[1]) == "refusal-card" ? static_cast<NodeContext &>(cardContext)
                                                                  : static_cast<NodeContext &>(ribbonContext);
    LayoutState placement = Seat(80);
    placement.height = 30;
    context.layout(&controller, placement);
    controller.observation_.reset();
    toolbox_host::failRegions = 2;
    context.render(&controller);
    toolbox_host::failRegions = 0;
    LOKA_VERIFY(controller.observation_.outsideRefusal().reason == TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "busy-batch")
  {
    BusyBatchPins(false);
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "busy-batch-nested")
  {
    BusyBatchPins(true);
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "toolbar")
  {
    ToolbarPaintPins();
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "plain-ellipsis")
  {
    PlainEllipsisPins(false);
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "plain-ellipsis-refused")
  {
    PlainEllipsisPins(true);
    return 0;
  }
  const ToolboxTextFontDescriptor smallLabel(SmallLabel);
  LOKA_VERIFY(smallLabel.font(0) == 3 && smallLabel.font(4) == 3);
  LOKA_VERIFY(smallLabel.size(12) == 9 && smallLabel.size(24) == 9);
  LOKA_VERIFY(smallLabel.face(bold | italic) == (bold | italic));
  LOKA_VERIFY(ToolboxTextFontDescriptor(TextStyle(TextSizeValue::points(11))).size(12) == 10);
  LOKA_VERIFY(ToolboxTextFontDescriptor().size(17) == 17);
  LOKA_VERIFY(ToolboxTextFontDescriptor().font(4) == 4);
  LOKA_VERIFY(ToolboxTextFontDescriptor(SmallLabel + FontSize<12>()).font(4) == 4);
  if (argc == 1 || (argc > 1 && std::string(argv[1]) == "ink"))
  {
    InkPins();
    if (argc > 1)
      return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "encoding") { EncodingPins(); return 0; }
  if (argc > 1 && std::string(argv[1]) == "paint-recovery") { PaintRecoveryPins(); return 0; }
  if (argc == 1) { EncodingPins(); PaintRecoveryPins(); MemoryPin(); }
  if (argc > 1 && std::string(argv[1]) == "memory") { MemoryPin(); return 0; }
  if (argc > 1 && std::string(argv[1]) == "scroll-x")
  {
    ScrollViewXPins();
    return 0;
  }
  if (argc > 1 && std::string(argv[1]).find("suppression-") == 0)
  {
    ScrollViewSuppressionPin(argv[1]);
    return 0;
  }
  if (argc > 1 && std::string(argv[1]) == "scroll-band") { ScrollViewPins(); return 0; }
  ScrollViewPins();
  if (argc > 1 && std::string(argv[1]) == "measurement-plain") { MeasurementPins(true, false); return 0; }
  if (argc > 1 && std::string(argv[1]) == "measurement-attributed") { MeasurementPins(false, true); return 0; }
  MeasurementPins(true, true);
  const loka::core::Managed<loka::platform::String> utf8 = loka::platform::CreatePlatformStringFromUtf8("a\0\xff", 3);
  loka::platform::Utf8View view = {0, 0};
  LOKA_VERIFY(utf8->queryUtf8(view) && view.length == 3 && std::string(view.bytes, view.length) == std::string("a\0\xff", 3));

  ToolboxWindow window;
  HostPortScope port(window.window());
  ToolboxScenePlatformController controller(&window);
  LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));

  if (argc == 1)
  {
    const AttributedString value = Styled("a ab", FontSize<12>() + Bold) + Styled("cd", FontSize<24>() + Italic);
    const AttributedString updated = Styled("var x = ", Bold) + Styled("1;", Italic);
    AttributedTextNode &node =
        *new AttributedTextNode((AttributedText(value) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
    LayoutState seat = Seat(30);
    Pin("ToolboxAttributedTextInstalledAcrossSegmentWord");
    IPlatformNodeHandler *handler = controller.nodeHandlerRegistry_.find(&node);
    LOKA_VERIFY(handler);
    loka::core::testing::failLokaAllocRaw("ToolboxAttributedText", "Context", 1);
    NodeContext *refused = handler->ensureContext(&node, &controller, seat);
    LOKA_VERIFY(!refused && !node.getContext());
    NodeContext *installed = handler->ensureContext(&node, &controller, seat);
    LOKA_VERIFY(installed != 0);
    LOKA_VERIFY(controller.compositionReplay.required());
    ToolboxAttributedTextContext *context = static_cast<ToolboxAttributedTextContext *>(installed);
    controller.renderContext = context;
    loka::core::testing::failLokaAllocRaw("ToolboxAttributedText", "Break", 0);
    toolbox_host::reset();
    context->layout(&controller, seat);
    const ToolboxAttributedTextTable &table = ToolboxAttributedTextContextAccess::table(*context);
    LOKA_VERIFY(table.valid());
    LOKA_VERIFY(table.lines().lineCount() == 2);
    LOKA_VERIFY(table.lines().line(1).fragmentCount == 2);
    LOKA_VERIFY(table.height() == 46);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::rect(*context).bottom == 66);
    Pin("ToolboxAttributedTextLargestSpanBusy");
    LOKA_VERIFY(controller.cursor.entries == 1);
    LOKA_VERIFY(controller.cursor.depth == 0);
    Pin("ToolboxAttributedTextLinearNativeMeasurement");
    LOKA_VERIFY(toolbox_host::metrics == 2);
    LOKA_VERIFY(toolbox_host::fonts == 3);
    LOKA_VERIFY(toolbox_host::measures == 2);
    LOKA_VERIFY(toolbox_host::widths == 0);
    std::printf("measure: font selections=%d TextWidth=%d MeasureText=%d GetFontInfo=%d scope acquisitions=1\n",
                toolbox_host::fonts,
                toolbox_host::widths,
                toolbox_host::measures,
                toolbox_host::metrics);
    Pin("ToolboxAttributedTextTransparentCompositionAndEraseOnReplay");
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::erases == 0);
    toolbox_host::draws.clear();
    Repaint(controller, *context);
    Pin("ToolboxAttributedTextDrawsUnderCallerClipWhenRegionAllocationFails");
    {
      // Classic memory pressure: NewRgn refuses, the clip is inactive, and the
      // text must still draw under the caller's clip (Text's fallback) while
      // the presented history stays unknown.
      const std::size_t before = toolbox_host::draws.size();
      const int erasesBefore = toolbox_host::erases;
      toolbox_host::failRegions = 2;
      context->render(&controller);
      LOKA_VERIFY(toolbox_host::draws.size() == before + 3);
      LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(*context));
      toolbox_host::failRegions = 0;
      toolbox_host::draws.clear();
      Repaint(controller, *context);
      toolbox_host::erases = erasesBefore;
    }
    Pin("ToolboxAttributedTextSpanFacesAndBaseline");
    LOKA_VERIFY(toolbox_host::draws.size() == 3);
    LOKA_VERIFY(toolbox_host::draws[0].face == bold);
    LOKA_VERIFY(toolbox_host::draws[1].face == bold);
    LOKA_VERIFY(toolbox_host::draws[2].face == italic);
    LOKA_VERIFY(toolbox_host::draws[0].y == 32);
    LOKA_VERIFY(toolbox_host::draws[1].y == 61);
    LOKA_VERIFY(toolbox_host::draws[2].y == 61);
    LOKA_VERIFY(toolbox_host::draws[2].x == 20);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));
    LOKA_VERIFY(window.port.txSize == 12 && window.port.txFace == 0);
    const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);

    Pin("ToolboxAttributedTextWidthInvalidates");
    seat = Seat(60);
    context->layout(&controller, seat);
    LOKA_VERIFY(table.lines().lineCount() == 1);
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(*context));
    Repaint(controller, *context);
    LOKA_VERIFY(toolbox_host::erases == 2);

    Pin("ToolboxAttributedTextFailedRebuildDegrades");
    controller.observation_.reset();
    loka::core::testing::failLokaAllocRaw("ToolboxAttributedText", "Break", 1);
    seat = Seat(30);
    context->layout(&controller, seat);
    LOKA_VERIFY(!table.valid());
    LOKA_VERIFY(controller.observation_.outsideRefusal().reason == TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET);
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(*context));
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
    context->render(&controller);
    LOKA_VERIFY(toolbox_host::erases == 2);
    LOKA_VERIFY(table.valid()); // Paint-only retry rebuilt after refusal.
    seat = Seat(30);
    context->layout(&controller, seat);
    LOKA_VERIFY(table.valid());

    Pin("ToolboxAttributedTextHiddenUpdateAndPartialPaint");
    SetRect(&controller.projectionClip, 200, 200, 220, 220);
    node.props = AttributedTextProps(updated);
    context->onPropsApplied();
    seat = Seat(80);
    context->layout(&controller, seat);
    LOKA_VERIFY(table.valid());
    Rect hiddenPaint = ToolboxAttributedTextContextAccess::paintRect(*context);
    LOKA_VERIFY(EmptyRect(&hiddenPaint));
    Pin("ToolboxAttributedTextClippedOutRenderAllocatesNoRegion");
    {
      // S1 lane: a leaf clipped out by layout leaves render before the port
      // switch and the clip; no NewRgn, no draw, history stays unknown.
      const int regionsBefore = toolbox_host::regions;
      const std::size_t drawsBefore = toolbox_host::draws.size();
      context->render(&controller);
      LOKA_VERIFY(toolbox_host::regions == regionsBefore);
      LOKA_VERIFY(toolbox_host::draws.size() == drawsBefore);
    }
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(*context));
    SetRect(&controller.projectionClip, -30000, -30000, 30000, 30000);
    seat = Seat(80);
    context->layout(&controller, seat);
    RgnHandle savedClip = NewRgn();
    RgnHandle partial = NewRgn();
    GetClip(savedClip);
    const Rect strip = {20, 10, 25, 25};
    RectRgn(partial, &strip);
    SetClip(partial);
    context->render(&controller);
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(*context));
    SetClip(savedClip);
    DisposeRgn(partial);
    DisposeRgn(savedClip);
    const int beforeUpdateErase = toolbox_host::erases;
    Repaint(controller, *context);
    LOKA_VERIFY(toolbox_host::erases == beforeUpdateErase + 1);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));

    Pin("ToolboxAttributedTextRetainedAndRetired");
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(controller.compositionReplay.required());
    LOKA_VERIFY(table.valid());
    LOKA_VERIFY(!ToolboxAttributedTextContextAccess::known(*context));
    context->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
    seat = Seat(30);
    context->layout(&controller, seat);
    context->render(&controller);
    LOKA_VERIFY(ToolboxAttributedTextContextAccess::known(*context));
    controller.renderContext = 0;
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    LOKA_VERIFY(!controller.compositionReplay.required());
    LOKA_VERIFY(!table.valid());
    delete &node; // Node retirement must clear the table before the context destructor asserts.
    LOKA_VERIFY(controller.retired.size() == 1);
    LOKA_VERIFY(!controller.compositionReplay.required());
    controller.retired.clear();
    loka::core::testing::allowLokaAllocRaw();
  }
  Pin("ToolboxAttributedTextUnsetUsesOriginalPort");
  window.port.txFace = italic;
  const AttributedString inherited = Styled("a", Bold + TextStyle().italic(false)) + Styled("b", TextStyle());
  {
    ToolboxAttributedTextTable projected;
    LOKA_VERIFY(projected.build(inherited, BlockStyle(), 100, controller));
    toolbox_host::reset();
    projected.draw(0, 0, controller, BlockStyle(), 0);
    LOKA_VERIFY(toolbox_host::draws.size() == 2);
    LOKA_VERIFY(toolbox_host::draws[0].face == bold);
    LOKA_VERIFY(toolbox_host::draws[1].face == italic);
    LOKA_VERIFY(window.port.txFace == italic);
  }
  Pin("ToolboxAttributedTextLongNativeBufferAndRanges");
  window.port.txFace = 0;
  {
    ToolboxAttributedTextTable projected;
    const AttributedString longText = Styled(loka::core::String(std::string(160, 'a')), Bold)
                                      + Styled(loka::core::String(std::string(160, 'b')), Bold);
    LOKA_VERIFY(projected.build(longText, BlockStyle(), 0, controller));
    toolbox_host::reset();
    projected.draw(0, 0, controller, BlockStyle(), 0);
    LOKA_VERIFY(toolbox_host::draws.size() == 1);
    LOKA_VERIFY(toolbox_host::draws[0].length == 320);
    LOKA_VERIFY(toolbox_host::draws[0].bytes == std::string(160, 'a') + std::string(160, 'b'));
    const AttributedString unicode = Styled("\xC3\xA9", Bold) + Styled("\xE3\x81\x82", Italic);
    LOKA_VERIFY(projected.build(unicode, BlockStyle().wrap(TEXT_WRAP_CHAR), 5, controller));
    LOKA_VERIFY(projected.lines().lineCount() == 2);
    toolbox_host::reset();
    projected.draw(0, 0, controller, BlockStyle(), 0);
    LOKA_VERIFY(toolbox_host::draws.size() == 2);
    LOKA_VERIFY(toolbox_host::draws[0].bytes == "\x8E");
    LOKA_VERIFY(toolbox_host::draws[1].bytes == "?");
    LOKA_VERIFY(projected.build(unicode, BlockStyle().wrap(TEXT_WRAP_NONE), 10, controller));
    LOKA_VERIFY(projected.lines().lineCount() == 1);
  }

  Pin("ToolboxAttributedTextLongWordLookaheadAndEllipsis");
  {
    ToolboxAttributedTextTable projected;
    const AttributedString word = Styled(loka::core::String(std::string("a ") + std::string(9000, 'x')), TextStyle());
    LOKA_VERIFY(projected.build(word, BlockStyle().wrap(TEXT_WRAP_WORD), 40, controller));
    LOKA_VERIFY(projected.lines().lineCount() > 800);
    const AttributedString mixed = Styled("ab", Bold) + Styled("cdefgh", Italic);
    const BlockStyle ellipsis = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    LOKA_VERIFY(projected.build(mixed, ellipsis, 25, controller));
    toolbox_host::reset();
    LOKA_VERIFY(projected.draw(0, 0, controller, ellipsis, 25));
    LOKA_VERIFY(toolbox_host::draws.size() == 2);
    LOKA_VERIFY(toolbox_host::draws[0].bytes == "ab");
    LOKA_VERIFY(toolbox_host::draws[1].bytes == "...");
    LOKA_VERIFY(toolbox_host::draws[1].face == italic);
    LOKA_VERIFY(toolbox_host::draws[1].x == 10);
  }
  Pin("ToolboxAttributedTextAlignsPaintedLines");
  {
    ToolboxAttributedTextTable projected;
    const TextAlign alignments[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
    const int offsets[] = {0, 8, 17};
    for (int a = 0; a < 3; ++a)
    {
      const BlockStyle block = BlockStyle().align(alignments[a]);
      LOKA_VERIFY(projected.build(Styled("ab\nx", Bold), block, 27, controller));
      toolbox_host::reset();
      LOKA_VERIFY(projected.draw(3, 0, controller, block, 27));
      LOKA_VERIFY(toolbox_host::draws.size() == 2);
      LOKA_VERIFY(toolbox_host::draws[0].x == 3 + offsets[a]);
      LOKA_VERIFY(toolbox_host::draws[1].x == 3 + (a == 0 ? 0 : a == 1 ? 11 : 22));
      LOKA_VERIFY(toolbox_host::measures == 0);
      const BlockStyle dots = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS).align(alignments[a]);
      LOKA_VERIFY(projected.build(Styled("abcdefgh", Bold), dots, 27, controller));
      toolbox_host::reset();
      LOKA_VERIFY(projected.draw(3, 0, controller, dots, 27));
      LOKA_VERIFY(toolbox_host::draws[0].bytes == "ab");
      LOKA_VERIFY(toolbox_host::draws[0].x == 3 + a);
      LOKA_VERIFY(toolbox_host::draws[1].x == 13 + a);
    }
  }
  Pin("ToolboxAttributedTextDistinctResolvedMetricsAndEmptyLines");
  loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 0);
  {
    ToolboxAttributedTextTable projected;
    const AttributedString equivalent = Styled("a", TextStyle()) + Styled("b", TextStyle().italic(false));
    toolbox_host::reset();
    LOKA_VERIFY(projected.build(equivalent, BlockStyle(), 40, controller));
    LOKA_VERIFY(toolbox_host::fonts == 3);
    LOKA_VERIFY(toolbox_host::measures == 2);
    LOKA_VERIFY(toolbox_host::metrics == 1);
    toolbox_host::reset();
    LOKA_VERIFY(projected.build(AttributedString(), BlockStyle(), 40, controller));
    LOKA_VERIFY(projected.height() == 17);
    LOKA_VERIFY(toolbox_host::metrics == 1);
    const AttributedString newline = Styled("x\n", FontSize<24>());
    LOKA_VERIFY(projected.build(newline, BlockStyle(), 40, controller));
    LOKA_VERIFY(projected.lines().lineCount() == 2);
    LOKA_VERIFY(projected.height() == 58);
    const AttributedString longText = Styled(loka::core::String(std::string(100, 'x')), Bold);
    LOKA_VERIFY(projected.build(longText, BlockStyle(), 40, controller));
    loka::core::testing::failLokaAllocRaw("TextLineBreaker", "Table", 1);
    LOKA_VERIFY(!projected.build(longText, BlockStyle(), 40, controller));
    LOKA_VERIFY(!projected.valid());
  }
  loka::core::testing::allowLokaAllocRaw();
  Pin("ToolboxAttributedTextCompositionReplayMembership");
  {
    ToolboxCompositionReplay ledger;
    ToolboxCompositionReplay::Registration first, second;
    LOKA_VERIFY(!ledger.required());
    first.attach(ledger);
    second.attach(ledger);
    first.attach(ledger); // Idempotent reattach, no duplicate edge.
    first.clear(); // Non-head removal must leave the other demand registered.
    LOKA_VERIFY(ledger.required());
    second.clear();
    LOKA_VERIFY(!ledger.required());
    { ToolboxCompositionReplay::Registration scoped; scoped.attach(ledger); }
    LOKA_VERIFY(!ledger.required());
  }
  std::printf("sizeof table=%lu context=%lu\n",
              static_cast<unsigned long>(sizeof(ToolboxAttributedTextTable)),
              static_cast<unsigned long>(sizeof(ToolboxAttributedTextContext)));
  std::puts("Toolbox AttributedText host pins passed (QuickDraw substitute; no native pixel claim)");
  return 0;
}
