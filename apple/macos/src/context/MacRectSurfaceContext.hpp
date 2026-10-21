#ifndef LOKA_MAC_RECT_SURFACE_CONTEXT_HPP
#define LOKA_MAC_RECT_SURFACE_CONTEXT_HPP

#include "app/RectSurface.hpp"
#include "MacRetirableContext.hpp"

namespace loka
{
  namespace app
  {
    class RectSurfaceNode;
  }
} // namespace loka

class MacScenePlatformController;

class MacRectSurfaceContext : public MacRetirableContext
{
public:
  MacRectSurfaceContext(MacScenePlatformController *controller,
                        void *parentView,
                        int x,
                        int y,
                        int width,
                        int height,
                        loka::app::RectSurfaceNode *node);
  virtual ~MacRectSurfaceContext();
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next);

  /** Placement after measurement; preparation no longer moves the view. */
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  void relayout(int x, int y, int width, int height);
  void draw(void *viewBounds);
  /** False when the native view could not be created: the context then
      holds no surface and must not be installed on the node. */
  bool hasNativeSurface() const
  {
    return this->view_ != 0;
  }

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  static void ModelChangedThunk(void *userData);
  void bindModel();
  void unbindModel();
  void applyModel();

  MacScenePlatformController *controller_;
  loka::app::RectSurfaceNode *node_;
  loka::core::State<loka::app::RectSurfaceModel> *modelState_;
  void *view_;
};

#endif
