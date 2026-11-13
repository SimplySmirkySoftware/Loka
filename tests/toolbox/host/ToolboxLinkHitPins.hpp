#ifndef LOKA_TOOLBOX_LINK_HIT_PINS_HPP
#define LOKA_TOOLBOX_LINK_HIT_PINS_HPP
#include "testing/scene/NodeObservedUsesTestAccess.hpp"
namespace
{
  typedef ToolboxAttributedTextContextAccess LinkAccess;
  const unsigned AllLinkPredicates = 4095;
  AttributedString LinkValue()
  {
    const LinkTarget target(loka::core::String("target"));
    return Linked("ab", Bold, target) + Linked("cd", Italic, target) + Styled("z", Bold);
  }
  void HitAnswerAt(const ToolboxAttributedTextTable &table,
                   const Rect &seat,
                   ToolboxScenePlatformController &controller,
                   const BlockStyle &block,
                   int x,
                   int y,
                   ToolboxHitAnswer expected,
                   std::size_t first = 0,
                   std::size_t last = 0)
  {
    const Point point = {static_cast<short>(y), static_cast<short>(x)};
    ToolboxLinkHit hit;
    const ToolboxHitAnswer actual = table.hitAt(seat, controller, block, point, hit);
    if (actual != expected)
      std::fprintf(stderr, "hit %d,%d actual=%d expected=%d width=%d\n", x, y, actual, expected, seat.right);
    LOKA_VERIFY(actual == expected);
    if (expected == TOOLBOX_HIT_LINK)
    {
      LOKA_VERIFY(hit.firstSegment == first && hit.lastSegment == last);
      LOKA_VERIFY(EqualRect(&hit.seat, &seat));
    }
  }
  void LinkHitPins()
  {
    Pin("E1GeometryE2DrawHitParity");
    ToolboxWindow window, other;
    HostPortScope port(other.window());
    ToolboxScenePlatformController controller(&window);
    const LinkTarget target(loka::core::String("target"));
    const AttributedString values[] = {Linked(loka::core::String(std::string(7000, 'a')), Bold, target),
                                       LinkValue(),
                                       Linked("abcdefgh\n", FontSize<18>(), target) + Linked("z", Bold, target),
                                       Linked("ab", Bold, target) + Linked("cdefgh", Italic, target)};
    for (unsigned mode = 0; mode < 4; ++mode)
      for (int align = 0; align < 3; ++align)
      {
        ToolboxAttributedTextTable table;
        const short width = mode == 0 ? 32767 : mode == 1 ? 13 : mode == 2 ? 5 : 22;
        const BlockStyle block = BlockStyle()
                                     .align(static_cast<TextAlign>(align))
                                     .wrap(mode == 1 ? TEXT_WRAP_CHAR : TEXT_WRAP_NONE)
                                     .truncation(mode > 1 ? TEXT_TRUNCATION_ELLIPSIS : TEXT_TRUNCATION_NONE);
        LOKA_VERIFY(table.build(values[mode], block, width, controller));
        const Rect seat = {11, 0, 200, width};
        toolbox_host::reset();
        LOKA_VERIFY(table.draw(seat.left, seat.top, controller, block, width));
        const std::vector<toolbox_host::Draw> draws = toolbox_host::draws;
        // Evaluate every accepted cell and its right-exclusive edge from the draw trace.
        for (std::size_t d = 0; d < draws.size(); ++d)
        {
          const toolbox_host::Draw &draw = draws[d];
          const int advance = draw.length * (draw.size / 3 + ((draw.face & bold) ? 1 : 0));
          const bool linked = draw.bytes != "..." && draw.bytes != "z" ? true : mode == 2 && draw.bytes == "z";
          const std::size_t segment = mode == 1   ? (draw.face == italic ? 1
                                                     : draw.bytes == "z" ? 2
                                                                         : 0)
                                      : mode == 2 ? 1
                                                  : 0;
          for (int edge = 0; edge < 2; ++edge)
          {
            const int x = draw.x + (edge ? advance - 1 : 0);
            if (x > SHRT_MAX)
              continue;
            HitAnswerAt(table,
                        seat,
                        controller,
                        block,
                        x,
                        draw.y - 1,
                        linked && x < seat.right ? TOOLBOX_HIT_LINK : TOOLBOX_HIT_NONE,
                        segment,
                        segment);
          }
        }
        if (mode == 1 && !draws.empty() && draws[0].x > seat.left)
          HitAnswerAt(table, seat, controller, block, draws[0].x - 1, draws[0].y - 1, TOOLBOX_HIT_NONE);
        HitAnswerAt(table, seat, controller, block, -1, 11, TOOLBOX_HIT_NONE);
        HitAnswerAt(table, seat, controller, block, width, 11, TOOLBOX_HIT_NONE);
        HitAnswerAt(table, seat, controller, block, 0, 200, TOOLBOX_HIT_NONE);
        if (mode == 1)
          HitAnswerAt(table, seat, controller, block, 0, 27, TOOLBOX_HIT_NONE); // leading
        if (mode == 2)
          HitAnswerAt(table, seat, controller, block, 0, 12, TOOLBOX_HIT_NONE); // omitted
        GrafPtr selected = 0;
        GetPort(&selected);
        LOKA_VERIFY(selected == other.window());
        LOKA_VERIFY(window.port.txFont == 3 && window.port.txFace == 0 && window.port.txSize == 12);
      }
    ToolboxAttributedTextTable table;
    const Rect seat = {0, 0, 100, 100};
    HitAnswerAt(table, seat, controller, BlockStyle(), 1, 1, TOOLBOX_HIT_REFUSED);
    LOKA_VERIFY(table.build(Linked("ab", Bold, target) + Linked("cd", Bold, target), BlockStyle(), 100, controller));
    HitAnswerAt(table, seat, controller, BlockStyle(), 19, 1, TOOLBOX_HIT_LINK, 0, 1);
    HitAnswerAt(table, seat, controller, BlockStyle(), 20, 1, TOOLBOX_HIT_NONE);
    // Existing table fault hook: a partial batch with an impossible endpoint refuses WidthSource.
    TextFragment &fragment = SpanAccess::fragment(table, 0);
    fragment.start = SpanAccess::bytes(table) + 1;
    fragment.end = fragment.start + 1;
    fragment.width = 200;
    const BlockStyle dots = BlockStyle().truncation(TEXT_TRUNCATION_ELLIPSIS);
    const Rect faultSeat = {0, 0, 100, 19};
    HitAnswerAt(table, faultSeat, controller, dots, 1, 1, TOOLBOX_HIT_REFUSED);
  }

  struct LinkFixture
  {
    ToolboxWindow window;
    HostPortScope port;
    ToolboxScenePlatformController controller;
    loka::core::MutableState<AttributedString> value;
    loka::core::PushStateTracker tracker;
    MeasurementBoundary boundary;
    AttributedTextNode node;
    ToolboxAttributedTextContext *context;
    ToolboxLinkHit hit;
    explicit LinkFixture(bool painted = true)
        : port(window.window()),
          controller(&window),
          value(LinkValue()),
          node(AttributedTextProps(&value)),
          context(0)
    {
      this->node.setPropsTypeId(AttributedTextProps::staticTypeId());
      this->tracker.addState(&this->value);
      NotifySubtreeNodeAttached(&this->node);
      BoundaryNode::declareBoundaryDirtySources(&this->node, &this->boundary);
      this->context = new ToolboxAttributedTextContext(&this->node, &this->controller);
      this->node.setContext(this->context);
      this->context->readLifecycleFactOnAttach();
      this->layout();
      if (painted)
        this->context->render(&this->controller);
      const Point point = {21, 11};
      LOKA_VERIFY(this->context->containsPoint(point));
      LOKA_VERIFY(this->context->prepareLinkHit(point, this->hit) == TOOLBOX_HIT_LINK);
      LOKA_VERIFY(this->eligible() == painted);
    }
    ~LinkFixture()
    {
      LinkAccess::currentContext(this->node, this->context);
      LifecycleFactTestAccess::MarkSubtreeRetired(&this->node);
      LifecycleFactTestAccess::DeliverFacts(&this->node);
    }
    void layout()
    {
      LayoutState seat = Seat(80);
      this->node.layout(&this->controller, seat);
    }
    void write(const AttributedString &next, bool force = false)
    {
      loka::core::StateTrackerGuard guard(&this->tracker);
      this->value.set(next, force);
    }
    bool eligible() const
    {
      AttributedString::LinkRun run;
      return this->context->queryEligibleRun(this->hit, run);
    }
  };

  void LinkEligibilityPins()
  {
    Pin("E4IsolatedPredicates");
    const unsigned broken[] = {1, 1, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    for (unsigned i = 0; i < sizeof(broken) / sizeof(broken[0]); ++i)
    {
      LinkFixture f;
      LOKA_VERIFY(LinkAccess::predicates(*f.context, f.hit) == AllLinkPredicates);
      switch (i)
      {
      case 0:
        NotifySubtreeNodeDetached(&f.node);
        LifecycleFactTestAccess::DeliverFacts(&f.node);
        LOKA_VERIFY(!f.eligible()); // production detach also clears placement/presentation
        LinkAccess::placement(*f.context, true);
        LinkAccess::presentation(*f.context, f.value.get());
        break;
      case 1:
        LinkAccess::currentContext(f.node, 0);
        break;
      case 2:
        f.node.props.text_ = 0;
        break;
      case 3:
        LinkAccess::placement(*f.context, false);
        break;
      case 4:
        LinkAccess::clearTable(*f.context);
        break;
      case 5:
        LinkAccess::presentation(*f.context, f.value.get(), false);
        break;
      case 6:
        LinkAccess::presentation(*f.context, f.value.get(), true, true);
        break;
      case 7:
        LinkAccess::presentation(*f.context, LinkValue() + Styled("", Bold));
        break;
      case 8:
        f.write(Styled("different", Bold));
        f.write(LinkValue() + Styled("", Bold));
        break;
      case 9:
        LinkAccess::move(*f.context);
        break;
      // The reversed synthetic range keeps the later bound non-refusing if
      // the queryLinkRun refusal guard is removed; no failed output is read.
      case 10:
        f.hit.firstSegment = 2;
        f.hit.lastSegment = 0;
        break;
      case 11:
        f.hit.lastSegment = 2;
        break;
      }
      LOKA_VERIFY(LinkAccess::predicates(*f.context, f.hit) == (AllLinkPredicates & ~broken[i]));
      std::printf("E4 condition %u\n", i);
      LOKA_VERIFY(!f.eligible());
    }
    Pin("E5ValueAndPropsPaths");
    {
      LinkFixture f;
      const AttributedString a = f.value.get(), b = Linked("other", Bold, LinkTarget(loka::core::String("b")));
      f.write(b);
      LOKA_VERIFY(!f.eligible());
      f.write(a, true);
      LOKA_VERIFY(f.eligible());
      f.write(b);
      f.write(a + Styled("", Bold));
      LOKA_VERIFY(!f.eligible());
      f.write(b);
      f.write(a);
      LOKA_VERIFY(f.eligible());
      f.layout();
      LOKA_VERIFY(!f.eligible());
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
      f.layout();
      LOKA_VERIFY(f.eligible());
      f.write(a, true);
      LOKA_VERIFY(f.eligible());
      LOKA_VERIFY(loka::app::testing::NodeObservedUsesTestAccess::mark(f.node)
                  == (NODE_DIRTY_PROPS | NODE_DIRTY_LAYOUT));
      f.layout();
      LOKA_VERIFY(!f.eligible());
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
      const AttributedTextDefinition same(&f.value);
      LOKA_VERIFY(same.applyPropsToNode(&f.node));
      LOKA_VERIFY(!f.eligible());
      f.layout();
      LOKA_VERIFY(!f.eligible());
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
      loka::core::MutableState<AttributedString> other(a);
      const AttributedTextDefinition rebound(&other);
      LOKA_VERIFY(rebound.applyPropsToNode(&f.node));
      LOKA_VERIFY(!f.eligible());
      f.layout();
      LOKA_VERIFY(!f.eligible());
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
      LOKA_VERIFY(same.applyPropsToNode(&f.node));
    }
    Pin("E6PresentationPaths");
    {
      LinkFixture neverPainted(false);
      LOKA_VERIFY(!neverPainted.eligible());
    }
    {
      LinkFixture f;
      const Rect whole = LinkAccess::rect(*f.context), partial = {20, 10, 24, 14}, miss = {200, 200, 220, 220};
      ClipRect(&partial);
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
      ClipRect(&miss);
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
      ClipRect(&partial);
      loka::testing::failToolboxAttributedDraws(1);
      f.context->render(&f.controller);
      LOKA_VERIFY(!f.eligible());
      ClipRect(&whole);
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
      f.write(Linked("new", Bold, LinkTarget(loka::core::String("new"))));
      f.layout();
      ClipRect(&partial);
      f.context->render(&f.controller);
      LOKA_VERIFY(!f.eligible());
      ClipRect(&whole);
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
      f.write(Linked("new\nline", Bold, LinkTarget(loka::core::String("new"))));
      f.context->render(&f.controller);
      LOKA_VERIFY(!f.eligible()); // non-fitting paint-only rebuild
      f.layout();
      const Rect full = LinkAccess::rect(*f.context);
      ClipRect(&full);
      const Point point = {21, 11};
      LOKA_VERIFY(f.context->prepareLinkHit(point, f.hit) == TOOLBOX_HIT_LINK);
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
    }
    Pin("E7RetainedReattach");
    {
      LinkFixture f;
      NotifySubtreeNodeDetached(&f.node);
      LifecycleFactTestAccess::DeliverFacts(&f.node);
      LOKA_VERIFY(!f.eligible());
      NotifySubtreeNodeAttached(&f.node);
      LifecycleFactTestAccess::DeliverFacts(&f.node);
      LOKA_VERIFY(!f.eligible());
      f.layout();
      LOKA_VERIFY(!f.eligible());
      f.context->render(&f.controller);
      LOKA_VERIFY(f.eligible());
    }
  }
} // namespace
#endif
