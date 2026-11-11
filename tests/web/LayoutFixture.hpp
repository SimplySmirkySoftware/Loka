#ifndef LOKA_WEB_LAYOUT_FIXTURE_HPP
#define LOKA_WEB_LAYOUT_FIXTURE_HPP
#include "WebFixture.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/controls/Toolbar.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/RectSurface.hpp"
namespace webtest
{
  enum LayoutCell
  {
    ROW_SHARED,
    ROW_NATURAL,
    TOOLBAR_LABELS,
    ALIGNED_TEXT_BUTTON,
    WRAPPED_ROW,
    REFUSED_ROW,
    AXIS_ROUND_TRIP,
    COLUMN_REMAINING,
    ROW_SCROLL,
    DEGRADE_SCROLL,
    TEXT_ONLY,
    UNALIGNED_NESTED,
    MID_FAILURE,
    MID_FAILURE_SCROLL,
    DEGRADE_WRAPPED,
    DEGRADE_EXPLICIT,
    MID_FAILURE_Y,
    MID_FAILURE_Y_SCROLL,
    LEAF_FAILURE_ROOT,
    LEAF_FAILURE_SCROLL,
    LAYOUT_CELL_COUNT
  };
  inline LayoutCell &layoutCell()
  {
    static LayoutCell value = ROW_SHARED;
    return value;
  }
  inline int &layoutVariant()
  {
    static int value = 0;
    return value;
  }
  class LayoutFixture;
  inline LayoutFixture *&layoutFixture()
  {
    static LayoutFixture *value = 0;
    return value;
  }
  /** Test-owned composition shared by host and browser cells. */
  class LayoutFixture : public loka::app::scene::BoundaryNodeFor<LayoutFixture>
  {
  public:
    loka::app::scene::NodeState<loka::app::StackAxis> axis;
    loka::app::scene::NodeState<loka::core::Frame> viewport;
    loka::app::scene::NodeState<bool> shown;
    loka::app::scene::NodeState<int> wallWidth;
    loka::app::scene::Request<loka::app::ScrollTarget> request;
    explicit LayoutFixture(const loka::app::scene::BoundaryPropsFor<LayoutFixture> &props)
        : loka::app::scene::BoundaryNodeFor<LayoutFixture>(props)
    {
      layoutFixture() = this;
      this->state(this->axis, loka::app::STACK_AXIS_ROW);
      this->state(this->viewport, loka::core::Frame());
      this->state(this->shown, true);
      this->state(this->wallWidth,
                  layoutCell() == MID_FAILURE_Y || layoutCell() == MID_FAILURE_Y_SCROLL ? 32744 : 32760);
      this->state(this->request, loka::app::ScrollTarget::None());
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      switch (layoutCell())
      {
      case ROW_SHARED:
        c.declare(HStack().gap(6) << Text("a") << Text("long label") << Box().width(20));
        break;
      case ROW_NATURAL:
        c.declare(HStack().naturalWidth().gap(6) << Text("a") << Text("long label") << Box().width(20));
        break;
      case TOOLBAR_LABELS:
        c.declare(Toolbar() << ToolbarItem::label("label") << ToolbarItem("Go") << ToolbarItem::flexibleSpace());
        break;
      case ALIGNED_TEXT_BUTTON:
        c.declare(
            VStack().gap(4) << (HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER) << Text("center") << Button("Go"))
                            << (HStack().alignVertical(VERTICAL_ALIGNMENT_BOTTOM) << Text("bottom") << Button("Go"))
                            << Text("after"));
        break;
      case WRAPPED_ROW:
        c.declare(VStack().gap(4) << (HStack().gap(6).alignVertical(VERTICAL_ALIGNMENT_CENTER)
                                      << (Text("abcdefghijabcdefghij") + BlockStyle().wrap(TEXT_WRAP_CHAR))
                                      << Button("Go"))
                                  << Text("after"));
        break;
      case REFUSED_ROW:
      {
        HStack row;
        row.gap(6);
        if (layoutVariant() != 1)
          row.alignVertical(VERTICAL_ALIGNMENT_CENTER);
        if (layoutVariant() == 2)
          row << EditText() << PopupMenu();
        else if (layoutVariant() == 3)
          row << ImageView().size(20, 40);
        else if (layoutVariant() == 4)
          row << RectSurface().size(20, 40);
        else if (layoutVariant() == 5)
          row << Cell("refused");
        else if (layoutVariant() == 6)
          row << scene::NodeDefinition<CardBandProps, CardBandNode>(CardBandProps());
        else
          row << ImageView();
        c.declare(VStack().gap(0) << row << Text("after"));
        break;
      }
      case AXIS_ROUND_TRIP:
        c.declare(Stack(this->axis.state()).gap(6) << Text("a") << Text("long label"));
        break;
      case COLUMN_REMAINING:
      {
        VStack column;
        column.gap(4);
        if (layoutVariant() == 1)
          column.naturalHeight();
        column << Box().height(30);
        if (layoutVariant() == 2)
          column << (Box().height(17) << (ScrollView(this->viewport) << Text("inside")));
        else
          column << (Box() << (ScrollView(this->viewport) << Text("inside")));
        c.declare(column);
      }
      break;
      case ROW_SCROLL:
      {
        HStack row;
        if (layoutVariant() != 3)
          row.alignVertical(VERTICAL_ALIGNMENT_CENTER);
        if (layoutVariant() != 1)
          row.naturalWidth();
        row << Text("a") << Button("a very long button label that overflows");
        if (layoutVariant() == 2)
          row << ImageView();
        c.declare(Show(*this->shown.state()) << (ScrollView(this->viewport).scrollTo(this->request) << row));
        break;
      }
      case DEGRADE_SCROLL:
        c.declare(ScrollView(this->viewport).scrollTo(this->request)
                  << (VStack().gap(0) << (HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER) << ImageView())
                                      << (HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER) << ImageView())
                                      << Text("after")));
        break;
      case TEXT_ONLY:
        c.declare(Text("plain"));
        break;
      case UNALIGNED_NESTED:
        c.declare(VStack() << (HStack().naturalWidth() << (Box() << Text("a")) << Text("long label")));
        break;
      case MID_FAILURE:
        c.declare(VStack().gap(0) << (HStack().naturalWidth().gap(0)
                                      << (Box().width(this->wallWidth.state()) << Text("first"))
                                      << (Box().width(7) << Text("edge")) << (Box().width(1) << Text("unreached")))
                                  << Text("after"));
        break;
      case MID_FAILURE_SCROLL:
        c.declare(ScrollView(this->viewport).scrollTo(this->request)
                  << (VStack().gap(0) << (HStack().naturalWidth().gap(0)
                                          << (Box().width(this->wallWidth.state()) << Text("first"))
                                          << (Box().width(7) << Text("edge")) << (Box().width(1) << Text("unreached")))
                                      << Text("after")));
        break;
      case DEGRADE_WRAPPED:
        c.declare(
            VStack().gap(0) << (HStack().alignVertical(VERTICAL_ALIGNMENT_TOP)
                                << (Text("top") + BlockStyle().wrap(TEXT_WRAP_CHAR)) << Button("top button"))
                            << (HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER)
                                << (Text("center") + BlockStyle().wrap(TEXT_WRAP_CHAR)) << Button("center button"))
                            << (HStack().alignVertical(VERTICAL_ALIGNMENT_BOTTOM)
                                << (Text("bottom") + BlockStyle().wrap(TEXT_WRAP_CHAR)) << Button("bottom button"))
                            << Text("after"));
        break;
      case DEGRADE_EXPLICIT:
        c.declare(
            VStack().gap(0) << (HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER) << Box().height(40) << Button("Go"))
                            << Text("after"));
        break;
      case MID_FAILURE_Y:
        c.declare(VStack().gap(0) << Box().height(
                      32744) << (HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER) << Text("first") << Button("last")));
        break;
      case MID_FAILURE_Y_SCROLL:
        c.declare(ScrollView(this->viewport).scrollTo(this->request)
                  << (VStack().gap(0) << Box().height(32744)
                                      << (HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER)
                                          << Text("first") << Button("last"))));
        break;
      case LEAF_FAILURE_ROOT:
        c.declare(VStack().gap(0) << Text("first leaf") << Text("after"));
        break;
      case LEAF_FAILURE_SCROLL:
        c.declare(ScrollView(this->viewport).scrollTo(this->request)
                  << (VStack().gap(0) << Text("first leaf") << Text("after")));
        break;
      case LAYOUT_CELL_COUNT:
        break;
      }
    }
  };
  class LayoutConfig : public AppConfigurable
  {
  public:
    explicit LayoutConfig(PlatformContext *platform)
        : AppConfigurable(platform)
    {
    }
    virtual void compose(AppComposition &c)
    {
      c << WindowDef(WindowProps().size(220, 120).scene(loka::app::scene::Boundary<LayoutFixture>()));
    }
  };
  inline AppConfigurable *CreateLayout(PlatformContext *platform)
  {
    return new LayoutConfig(platform);
  }
} // namespace webtest
#endif
