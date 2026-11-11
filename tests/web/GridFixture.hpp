#ifndef LOKA_WEB_GRID_FIXTURE_HPP
#define LOKA_WEB_GRID_FIXTURE_HPP
#include "WebFixture.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/controls/Cell.hpp"
namespace webtest
{
  enum GridCell
  {
    GRID_BOX,
    GRID_ROOT,
    GRID_REMAINING,
    GRID_NATURAL,
    GRID_SCROLL,
    GRID_PADDED,
    GRID_REFUSED,
    GRID_SPARSE,
    GRID_EMPTY,
    GRID_MIXED,
    GRID_CAPACITY,
    GRID_WALL_ROOT,
    GRID_WALL_SCROLL,
    GRID_CELL_COUNT
  };
  inline GridCell &gridCell()
  {
    static GridCell value = GRID_BOX;
    return value;
  }
  inline bool &initialGridWall()
  {
    static bool value = false;
    return value;
  }
  class GridFixture;
  inline GridFixture *&gridFixture()
  {
    static GridFixture *value = 0;
    return value;
  }
  /** Test-owned consumer shared by host and browser; commands retain the tree. */
  class GridFixture : public loka::app::scene::BoundaryNodeFor<GridFixture>
  {
  public:
    loka::app::scene::NodeState<loka::core::Frame> viewport;
    loka::app::scene::Request<loka::app::ScrollTarget> request;
    loka::core::EmitterState clicked;
    unsigned clicks;
    explicit GridFixture(const loka::app::scene::BoundaryPropsFor<GridFixture> &props)
        : loka::app::scene::BoundaryNodeFor<GridFixture>(props),
          clicks(0)
    {
      gridFixture() = this;
      this->state(this->viewport, loka::core::Frame());
      this->state(this->request, loka::app::ScrollTarget::None());
    }
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.action(this->clicked, this, &GridFixture::click);
    }
    void click()
    {
      ++this->clicks;
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      Grid grid = Grid().rows(2).cols(2);
      if (gridCell() == GRID_REFUSED)
        grid << Cell() << Cell() << Cell() << Cell();
      else if (gridCell() == GRID_MIXED)
        grid << Cell() << Button("b", &this->clicked) << Cell() << Button("d", &this->clicked);
      else if (gridCell() != GRID_EMPTY)
      {
        grid << Button("a", &this->clicked) << Button("b", &this->clicked) << Button("c", &this->clicked);
        if (gridCell() != GRID_SPARSE)
          grid << Button("d", &this->clicked);
        if (gridCell() == GRID_CAPACITY)
          grid << Button("excess", &this->clicked);
      }
      switch (gridCell())
      {
      case GRID_BOX:
      case GRID_CAPACITY:
        c.declare(Box().width(100).height(80) << grid);
        break;
      case GRID_PADDED:
        c.declare(Box().width(110).height(90).padding(5) << grid);
        break;
      case GRID_ROOT:
        c.declare(grid);
        break;
      case GRID_REMAINING:
        c.declare(VStack().gap(0) << Button("before") << grid << Button("after"));
        break;
      case GRID_NATURAL:
        c.declare(VStack().gap(0).naturalHeight() << Button("before") << grid << Button("after"));
        break;
      case GRID_SCROLL:
        c.declare(ScrollView(this->viewport) << grid);
        break;
      case GRID_REFUSED:
      case GRID_SPARSE:
      case GRID_EMPTY:
      case GRID_MIXED:
        c.declare(VStack().gap(0) << grid << Button("after"));
        break;
      case GRID_WALL_ROOT:
      case GRID_WALL_SCROLL:
      {
        // Each leaf fits even when its offered cell's right edge does not.
        VStack content = VStack().gap(0) << (Box().width(100).padding(initialGridWall() ? 5 : 0)
                                             << (Box().width(32767)
                                                 << (Grid().rows(1).cols(2) << (Box().width(10) << Button("first"))
                                                                            << (Box().width(10) << Button("last")))))
                                         << Button("after");
        if (gridCell() == GRID_WALL_SCROLL)
          c.declare(ScrollView(this->viewport).scrollTo(this->request) << content);
        else
          c.declare(content);
        break;
      }
      case GRID_CELL_COUNT:
        break;
      }
    }
  };
  /** Cost: fixture command once; walks only its own mounted composition. */
  inline loka::app::GridNode *FindGrid(loka::app::scene::Node *node)
  {
    if (node->asGridNode())
      return node->asGridNode();
    loka::app::scene::INestable *nestable = node->asNestable();
    if (nestable)
      for (loka::app::scene::Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
        if (loka::app::GridNode *grid = FindGrid(child))
          return grid;
    return 0;
  }
  class GridConfig : public AppConfigurable
  {
  public:
    explicit GridConfig(PlatformContext *platform)
        : AppConfigurable(platform)
    {
    }
    virtual void compose(AppComposition &c)
    {
      c << WindowDef(WindowProps().size(100, 80).scene(loka::app::scene::Boundary<GridFixture>()));
    }
  };
  inline AppConfigurable *CreateGrid(PlatformContext *platform)
  {
    return new GridConfig(platform);
  }
} // namespace webtest
#endif
