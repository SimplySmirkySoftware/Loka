#include "ToolboxNodeDispatch.hpp"
#include "ToolboxInputDoor.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/Text.hpp"
#include <cstdio>
#include <cstring>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  // Native drawing is outside these pins; Text/Button keep their real layout
  // classification and natural measurement while recording offered seats.
  short placeLeaf(IPlatformController *platform, LayoutState &state, LayoutState &offer)
  {
    ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(platform);
    if (!controller->projectLayoutState(state))
      return 0;
    offer = state;
    state.y = static_cast<short>(state.y + 7);
    return state.width;
  }

  class GapText : public TextNode
  {
  public:
    LayoutState offer;
    unsigned calls;
    GapText()
        : TextNode(Text("x").props),
          calls(0)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      ++this->calls;
      return placeLeaf(platform, state, this->offer);
    }
  };
  class GapButton : public ButtonNode
  {
  public:
    LayoutState offer;
    GapButton()
        : ButtonNode(Button("B").props)
    {
    }
    virtual short layoutProjected(IPlatformController *platform, LayoutState &state)
    {
      return placeLeaf(platform, state, this->offer);
    }
  };

  struct Fixture
  {
    ToolboxWindow window;
    ToolboxScenePlatformController controller;
    Fixture()
        : controller(&window)
    {
      const bool registered = RegisterToolboxPlatformLayoutHandlers(this->controller.gapLayoutRegistry);
      LOKA_VERIFY(registered);
    }
    LayoutState seat(short spacing = 31) const
    {
      LayoutState state;
      state.width = 200;
      state.height = 100;
      state.lineHeight = 12;
      state.spacing = spacing;
      return state;
    }
    void layout(Node &node, short spacing = 31)
    {
      LayoutState state = this->seat(spacing);
      LayoutNode(&node, state, &this->controller, 0);
      LOKA_VERIFY(state.spacing == spacing);
    }
  };

  FragmentNode *pair(GapText *&a, GapText *&b)
  {
    FragmentNode *fragment = new FragmentNode(FragmentProps());
    a = new GapText();
    b = new GapText();
    fragment->addChild(a);
    fragment->addChild(b);
    return fragment;
  }

  void columnPin()
  {
    Fixture f;
    StackNode column(Column().gap(16).props);
    GapText *a = new GapText(), *b, *c;
    GapButton *d = new GapButton();
    column.addChild(a);
    column.addChild(pair(b, c));
    column.addChild(d);
    f.layout(column);
    LOKA_VERIFY(a->offer.y == 0 && b->offer.y == 23 && c->offer.y == 46 && d->offer.y == 69);
  }

  void siblingsPin()
  {
    // Exercise both Column traversal and the shared-state generic LayoutChildren loop.
    for (int generic = 0; generic != 2; ++generic)
    {
      Fixture f;
      StackNode column(Column().gap(20).props);
      FragmentNode *group = new FragmentNode(FragmentProps());
      NestableNode *parent = generic ? static_cast<NestableNode *>(group) : &column;
      GapText *a, *b, *c, *d;
      BoxNode *box = new BoxNode(BoxProps());
      box->addChild(pair(a, b));
      parent->addChild(box);
      parent->addChild(pair(c, d));
      if (generic)
        column.addChild(group);
      else
        delete group;
      f.layout(column);
      LOKA_VERIFY(b->offer.y - a->offer.y == 15);
      LOKA_VERIFY(c->offer.y - b->offer.y == 27 && d->offer.y - c->offer.y == 27);
    }
    Fixture f;
    ScrollViewNode scroll((ScrollViewProps()));
    GapText *a, *b, *c, *d;
    BoxNode *box = new BoxNode(Box().gap(20).props);
    box->addChild(pair(a, b));
    scroll.addChild(box);
    scroll.addChild(pair(c, d));
    f.layout(scroll, 23);
    LOKA_VERIFY(b->offer.y - a->offer.y == 27);
    LOKA_VERIFY(c->offer.y - b->offer.y == 15 && d->offer.y - c->offer.y == 15);
    f.controller.releaseNodeContexts(&scroll);
    // Projected early return resets its own input, then restores its caller.
    GapText projected;
    f.layout(projected, 29);
    LOKA_VERIFY(projected.offer.spacing == 8);
  }

  void resetsPin()
  {
    // Independent unspecified pins for every class-(e) Column/Box/Row fixture.
    for (int kind = 0; kind != 6; ++kind)
    {
      Fixture f;
      StackNode outer(Column().gap(20).props);
      NestableNode *container = 0;
      if (kind == 0)
        container = new StackNode(Column().props);
      if (kind == 1)
        container = new BoxNode(BoxProps());
      if (kind == 2)
        container = new ZStackNode(ZStackProps());
      if (kind == 3)
        container = new GridNode(GridProps(1, 1));
      if (kind == 4)
        container = new CardNode(CardProps());
      if (kind == 5)
        container = new ScrollViewNode(ScrollViewProps());
      GapText *a, *b;
      container->addChild(pair(a, b));
      outer.addChild(container);
      f.layout(outer);
      LOKA_VERIFY(b->offer.y - a->offer.y == 15);
      f.controller.releaseNodeContexts(&outer);
    }
    Fixture f;
    StackNode row(Row().props);
    GapText *a, *b;
    row.addChild(pair(a, b));
    f.layout(row, 10000);
    LOKA_VERIFY(b->offer.y - a->offer.y == 15);
  }

  void rowPin()
  {
    Fixture f;
    StackNode row(Row().gap(20).props);
    GapText *a, *b;
    row.addChild(pair(a, b));
    f.layout(row);
    LOKA_VERIFY(a->offer.x == b->offer.x && b->offer.y - a->offer.y == 27);
  }

  void measurementPin()
  {
    for (int mode = 0; mode != 3; ++mode)
    {
      Fixture f;
      StackProps outerProps = Row().props, innerProps = Row().props;
      outerProps.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
      innerProps.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
      if (mode == 0)
        innerProps.gap(20);
      if (mode == 2)
        outerProps.gap(20);
      StackNode outer(outerProps);
      StackNode *inner = new StackNode(innerProps);
      GapButton *a = new GapButton(), *b = new GapButton(), *c = new GapButton();
      outer.addChild(a);
      outer.addChild(inner);
      inner->addChild(b);
      inner->addChild(c);
      short buttonWidth = 0;
      LOKA_VERIFY(f.controller.queryNaturalWidth(a, buttonWidth));
      loka::app::layout::RowWidthConsultation widths(outer.childrenHead(),
                                                     2,
                                                     200,
                                                     mode == 2 ? 20 : 8,
                                                     loka::app::layout::RailSpacing(12, 8, 9),
                                                     ROW_UNDECLARED_WIDTH_NATURAL,
                                                     &f.controller);
      widths.next(a);
      const int innerWidth = widths.next(inner).width();
      const int gap = mode == 0 ? 20 : 8;
      LOKA_VERIFY(innerWidth == 2 * buttonWidth + gap);
      f.layout(outer);
      LOKA_VERIFY(b->offer.width == buttonWidth && c->offer.width == buttonWidth);
      LOKA_VERIFY(c->offer.x - b->offer.x == buttonWidth + gap);
      LOKA_VERIFY(b->offer.x - a->offer.x == buttonWidth + (mode == 2 ? 20 : 8));
      // The nested offer must equal its measured width, not merely happen to fit its children.
      // Return width includes both nested seats and both resolved gaps.
      LayoutState seat = f.seat();
      const short width = LayoutNode(&outer, seat, &f.controller, 0);
      LOKA_VERIFY(width == 3 * buttonWidth + gap + (mode == 2 ? 20 : 8));
    }
  }

  class CountedBox : public BoxNode
  {
  public:
    unsigned visits;
    CountedBox()
        : BoxNode(Box().size(1, 1).props),
          visits(0)
    {
    }
    virtual BoundaryNode *asBoundary()
    {
      ++this->visits;
      return 0;
    }
  };

  class RejectedCoordinate : public IPlatformLayoutTraversal
  {
  public:
    int rejected;
    short y;
    RejectedCoordinate()
        : rejected(0),
          y(0)
    {
    }
    virtual bool refuseLayoutResultY(int value)
    {
      if (value >= SHRT_MIN && value <= SHRT_MAX)
        return false;
      this->rejected = value;
      return true;
    }
    virtual int layoutChild(Node *, const LayoutState &state)
    {
      this->y = state.y;
      return 1;
    }
    virtual void setLayoutResultY(short value)
    {
      this->y = value;
    }
    virtual short layoutResultY() const
    {
      return this->y;
    }
  };

  void overflowPin(bool edge)
  {
    Fixture f;
    ScrollViewNode scroll((ScrollViewProps()));
    StackNode *row = new StackNode(Row().gap(SHRT_MAX).props);
    CountedBox *a = new CountedBox(), *b = new CountedBox();
    row->addChild(a);
    row->addChild(b);
    scroll.addChild(row);
    scroll.requeueLayoutInputs(NODE_DIRTY_LAYOUT);
    LayoutState state = f.seat();
    state.x = edge ? -1 : 0;
    LayoutNode(&scroll, state, &f.controller, 0);
    LOKA_VERIFY(a->visits == 1 && b->visits == 0);
    LOKA_VERIFY(f.controller.scrollCalls.empty());
    LOKA_VERIFY(scroll.takeLayoutInputs() != NODE_DIRTY_NONE);
    LOKA_VERIFY(state.spacing == 31);
    f.controller.releaseNodeContexts(&scroll);

    // Observe the existing traversal acceptance door as well as the ScrollView
    // outcome: removing the origin check would otherwise be masked by its edge.
    RejectedCoordinate traversal;
    LayoutState rowState = f.seat(SHRT_MAX);
    rowState.x = edge ? -1 : 0;
    ComputeToolboxRowLayout(row, rowState, &traversal);
    LOKA_VERIFY(traversal.rejected == SHRT_MAX + 1);
  }

  void spansPin()
  {
    for (int band = 0; band != 3; ++band)
    {
      Fixture f;
      ScrollViewNode scroll((ScrollViewProps()));
      StackNode *column = new StackNode(Column().props);
      column->setPropsTypeId(StackProps::staticTypeId());
      GapText *leaves[5];
      for (int i = 0; i != 5; ++i)
      {
        leaves[i] = new GapText();
        column->addChild(leaves[i]);
      }
      scroll.addChild(column);
      LayoutState state = f.seat();
      state.height = band == 0 ? 30 : band == 1 ? 1 : 0;
      LayoutNode(&scroll, state, &f.controller, 0);
      loka::app::layout::StackSpans *spans = f.controller.scrollSpans();
      LOKA_VERIFY(spans && spans->validForGap(8) && spans->total() == 67);
      // Head-only and empty ledger selections must reject the old gap before reusing totals.
      if (band == 2)
        spans->placed(loka::core::Frame(0, 1000, 200, 0));
      LOKA_VERIFY(Column().gap(20).applyPropsToNode(column));
      scroll.takeLayoutInputs();
      scroll.requeueLayoutInputs(NODE_DIRTY_LAYOUT);
      const NodeDirtyFlags inputs = scroll.takeLayoutInputs();
      LOKA_VERIFY(inputs == NODE_DIRTY_LAYOUT);
      scroll.requeueLayoutInputs(inputs);
      state = f.seat();
      state.height = band == 0 ? 30 : band == 1 ? 1 : 0;
      LayoutNode(&scroll, state, &f.controller, 0);
      LOKA_VERIFY(spans->validForGap(20) && spans->total() == 115);
      short positions[5];
      for (int i = 0; i != 5; ++i)
      {
        positions[i] = leaves[i]->offer.y;
        LOKA_VERIFY(leaves[i]->calls == 2);
      }
      spans->invalidate();
      state = f.seat();
      state.height = band == 0 ? 30 : band == 1 ? 1 : 0;
      LayoutNode(&scroll, state, &f.controller, 0);
      LOKA_VERIFY(spans->total() == 115);
      for (int i = 0; i != 5; ++i)
        LOKA_VERIFY(leaves[i]->offer.y == positions[i] && positions[i] == i * 27);
      f.controller.releaseNodeContexts(&scroll);
    }
  }

  void overflowDefaultPin()
  {
    // Direct entry intentionally bypasses dispatch: horizontal measurement must
    // depend on the named horizontal default, not this offered vertical transport.
    Fixture f;
    ScrollViewNode scroll((ScrollViewProps()));
    StackProps props = Row().props;
    props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
    StackNode *row = new StackNode(props);
    row->addChild(new BoxNode(Box().size(70, 1).props));
    row->addChild(new BoxNode(Box().size(70, 1).props));
    scroll.addChild(row);
    LayoutState state = f.seat(23);
    state.width = 100;
    f.controller.layoutScrollView(&scroll, state, 0);
    LOKA_VERIFY(f.controller.scrollCalls.size() == 2);
    LOKA_VERIFY(f.controller.scrollCalls[1].maximum == 64);
    f.controller.releaseNodeContexts(&scroll);
  }

  void rootPin()
  {
    Fixture f;
    FragmentNode root((FragmentProps()));
    GapText *a = new GapText(), *b = new GapText();
    root.addChild(a);
    root.addChild(b);
    f.controller.rootNode_ = &root;
    ToolboxInputDoor::render(f.controller);
    LOKA_VERIFY(a->offer.x == 12 && a->offer.y == 12);
    LOKA_VERIFY(b->offer.y - a->offer.y == 7 + ToolboxLayoutMetrics::kDefaultGap);
    f.controller.rootNode_ = 0;
  }
} // namespace

int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  const char *mode = argv[1];
  if (!std::strcmp(mode, "column"))
    columnPin();
  else if (!std::strcmp(mode, "siblings"))
    siblingsPin();
  else if (!std::strcmp(mode, "resets"))
    resetsPin();
  else if (!std::strcmp(mode, "row"))
    rowPin();
  else if (!std::strcmp(mode, "measurement"))
    measurementPin();
  else if (!std::strcmp(mode, "origin"))
    overflowPin(false);
  else if (!std::strcmp(mode, "edge"))
    overflowPin(true);
  else if (!std::strcmp(mode, "spans"))
    spansPin();
  else if (!std::strcmp(mode, "overflow-default"))
    overflowDefaultPin();
  else if (!std::strcmp(mode, "root"))
    rootPin();
  else
    LOKA_VERIFY(false);
  std::printf("Toolbox gap %s passed\n", mode);
}
