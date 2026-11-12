#ifndef LOKA_WEB_CELL_FIXTURE_HPP
#define LOKA_WEB_CELL_FIXTURE_HPP
#include "WebFixture.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "../../example/MineSweeper/src/MyAppConfig.hpp"
namespace webtest
{
  inline int &cellMode()
  {
    static int value = 0;
    return value;
  }
  class CellFixture;
  inline CellFixture *&cellFixture()
  {
    static CellFixture *value = 0;
    return value;
  }
  /** Test-owned identity recorder; each reset constructs a fresh board. */
  class CellFixture : public loka::app::scene::BoundaryNodeFor<CellFixture>
  {
  public:
    loka::app::scene::NodeState<loka::core::String> text;
    loka::app::scene::NodeState<bool> enabled, shown;
    loka::app::scene::NodeState<int> generation;
    loka::app::scene::NodeState<loka::core::Frame> viewport;
    loka::core::EmitterState cells[64], button;
    struct Hit
    {
      int count;
      Hit()
          : count(0)
      {
      }
      void record()
      {
        ++this->count;
      }
    };
    Hit hits[64];
    int buttonHits;
    explicit CellFixture(const loka::app::scene::BoundaryPropsFor<CellFixture> &props)
        : loka::app::scene::BoundaryNodeFor<CellFixture>(props),
          buttonHits(0)
    {
      cellFixture() = this;
      this->state(this->text, loka::core::String::Literal("a\r\nb"));
      this->state(this->enabled, false);
      this->state(this->shown, true);
      this->state(this->generation, 0);
      this->state(this->viewport, loka::core::Frame());
    }
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      for (unsigned i = 0; i < 64; ++i)
        token.action(this->cells[i], &this->hits[i], &Hit::record);
      token.action(this->button, this, &CellFixture::reset);
    }
    void reset()
    {
      ++this->buttonHits;
      this->generation.set(this->generation.get() + 1);
    }
    void board(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      Grid grid = Grid().rows(8).cols(8);
      for (unsigned i = 0; i < 64; ++i)
        grid << Cell(".").onClick(&this->cells[i]);
      c.declare(grid);
    }
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      if (cellMode() == 1)
      {
        c.declare(
            VStack().gap(0)
            << Button("New Game", &this->button)
            << Keyed(*this->generation.state(),
                     this,
                     &CellFixture::board,
                     reservation::SeatNodes<
                         reservation::Nodes<GridNode, 1, reservation::Nodes<CellNode, 64, reservation::End> > >()));
        return;
      }
      const Cell first = Cell(this->text.state()).onClick(&this->cells[0]);
      switch (cellMode())
      {
      case 2:
        c.declare(VStack().gap(0).naturalHeight() << first << Cell("owned").onClick(&this->cells[1]));
        break;
      case 3:
        c.declare(HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER) << first << Cell("owned").onClick(&this->cells[1]));
        break;
      case 4:
        c.declare(HStack().alignVertical(VERTICAL_ALIGNMENT_CENTER) << first << Button("button", &this->button));
        break;
      case 5:
      case 6:
        c.declare(ScrollView(this->viewport) << first);
        break;
      case 7:
        c.declare(VStack().gap(0) << first << Cell("owned").onClick(&this->cells[1]));
        break;
      case 0:
      case 1:
        c.declare(VStack().gap(0) << Button("button", &this->button).enabled(this->enabled.state())
                                  << (Show(*this->shown.state())
                                      << (Grid().rows(1).cols(3)
                                          << first << Cell("owned").onClick(&this->cells[1]) << Cell("null"))));
        break;
      }
    }
  };
  class CellConfig : public AppConfigurable
  {
  public:
    explicit CellConfig(PlatformContext *platform)
        : AppConfigurable(platform)
    {
    }
    virtual void compose(AppComposition &c)
    {
      c << WindowDef(WindowProps()
                         .contentMargin(0)
                         .size(220, cellMode() == 6 ? 0 : 240)
                         .scene(loka::app::scene::Boundary<CellFixture>()));
    }
  };
  inline AppConfigurable *CreateCells(PlatformContext *platform)
  {
    return new CellConfig(platform);
  }
  inline AppConfigurable *CreateSeededMine(PlatformContext *platform)
  {
    return new MineSweeperAppConfig(platform, minesweeper::MainProps(1));
  }
} // namespace webtest
#endif
