#ifndef LOKA_WEB_RECT_SURFACE_FIXTURE_HPP
#define LOKA_WEB_RECT_SURFACE_FIXTURE_HPP
#include "WebFixture.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
namespace webtest
{
  enum SurfaceParent
  {
    SURFACE_COLUMN,
    SURFACE_NATURAL,
    SURFACE_ROW,
    SURFACE_ALIGNED,
    SURFACE_EXHAUSTED,
    SURFACE_GRID,
    SURFACE_SCROLL,
    SURFACE_DIRECT,
    SURFACE_WALL_ROOT,
    SURFACE_WALL_SCROLL
  };
  struct SurfaceSetup
  {
    SurfaceParent parent;
    short width, height;
    bool destroy;
    SurfaceSetup()
        : parent(SURFACE_COLUMN),
          width(0),
          height(0),
          destroy(false)
    {
    }
  };
  inline SurfaceSetup &surfaceSetup()
  {
    static SurfaceSetup value;
    return value;
  }
  class SurfaceFixture;
  inline SurfaceFixture *&surfaceFixture()
  {
    static SurfaceFixture *value = 0;
    return value;
  }
  /** Test-owned boundary: extent sentinel outlives the surface's Show generation. */
  class SurfaceFixture : public loka::app::scene::BoundaryNodeFor<SurfaceFixture>
  {
  public:
    loka::app::scene::NodeState<loka::core::Frame> viewport;
    loka::app::scene::NodeState<loka::core::Frame> extent;
    loka::app::scene::NodeState<loka::app::RectSurfaceModel> model;
    loka::app::scene::NodeState<bool> shown;
    explicit SurfaceFixture(const loka::app::scene::BoundaryPropsFor<SurfaceFixture> &props)
        : loka::app::scene::BoundaryNodeFor<SurfaceFixture>(props),
          notifications_(0)
    {
      surfaceFixture() = this;
      this->state(this->viewport, loka::core::Frame(0, 0, 0, 0));
      this->state(this->extent, loka::core::Frame(7, 11, 17, 19));
      this->state(this->model, loka::app::RectSurfaceModel());
      this->state(this->shown, true);
    }
    /** Cost: binding window once; observes only this fixture's sentinel. */
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.watch(*this->extent.state(), this, &SurfaceFixture::observe);
    }
    /** Cost: sentinel notification once; no rows. */
    void observe()
    {
      ++this->notifications_;
    }
    /** Cost: test assertion once; reads own notification count, no rows. */
    unsigned notifications() const
    {
      return this->notifications_;
    }
    /** Cost: mount once; declares only this fixture's children. */
    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      const RectSurface surface = RectSurface()
                                      .size(surfaceSetup().width, surfaceSetup().height)
                                      .model(this->model.state())
                                      .laidOutExtent(this->extent);
      switch (surfaceSetup().parent)
      {
      case SURFACE_COLUMN:
        c.declare(VStack().gap(0) << Box().height(20) << surface);
        break;
      case SURFACE_NATURAL:
        c.declare(VStack().gap(0).naturalHeight() << surface);
        break;
      case SURFACE_ROW:
        c.declare(HStack().gap(0) << surface);
        break;
      case SURFACE_ALIGNED:
        c.declare(HStack().gap(0).alignVertical(VERTICAL_ALIGNMENT_CENTER) << surface);
        break;
      case SURFACE_EXHAUSTED:
        c.declare(VStack().gap(0) << Box().height(120) << surface);
        break;
      case SURFACE_GRID:
        c.declare(Grid().rows(2).cols(1) << surface << surface);
        break;
      case SURFACE_SCROLL:
        c.declare(ScrollView(this->viewport) << surface << surface);
        break;
      case SURFACE_DIRECT:
        if (surfaceSetup().destroy)
          c.declare(Show(*this->shown.state()).destroyOnDetach() << surface);
        else
          c.declare(Show(*this->shown.state()) << surface);
        break;
      case SURFACE_WALL_ROOT:
        c.declare(VStack().gap(0) << Box().height(1) << (Box().padding(0) << surface));
        break;
      case SURFACE_WALL_SCROLL:
        c.declare(ScrollView(this->viewport) << (VStack().gap(0) << Box().height(1) << (Box().padding(0) << surface)));
        break;
      }
    }

  private:
    unsigned notifications_;
  };
  /** Cost: fixture command/observation once; walks this fixture's own composition only. */
  inline loka::app::scene::Node *FindSurfaceNode(loka::app::scene::Node *node, bool box = false)
  {
    if (box ? node->asBoxNode() != 0 : node->asRectSurfaceNode() != 0)
      return node;
    loka::app::scene::INestable *nestable = node->asNestable();
    if (nestable)
      for (loka::app::scene::Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
        if (loka::app::scene::Node *found = FindSurfaceNode(child, box))
          return found;
    return 0;
  }
  class SurfaceConfig : public AppConfigurable
  {
  public:
    explicit SurfaceConfig(PlatformContext *platform)
        : AppConfigurable(platform)
    {
    }
    /** Cost: app mount once; one fixture window, no foreign rows. */
    virtual void compose(AppComposition &c)
    {
      c << WindowDef(WindowProps().contentMargin(0).size(100, 80).scene(loka::app::scene::Boundary<SurfaceFixture>()));
    }
  };
  /** Cost: session start once; allocates one fixture config, no rows. */
  inline AppConfigurable *CreateSurfaces(PlatformContext *platform)
  {
    return new SurfaceConfig(platform);
  }
} // namespace webtest
#endif
