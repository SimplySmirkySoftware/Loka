#ifndef LOKA_TOOLBOX_CELL_CONTEXT_HPP
#define LOKA_TOOLBOX_CELL_CONTEXT_HPP

#include "context/ToolboxProjectedNodeContext.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "ToolboxPropsRefresh.hpp"
#include "app/scene/projection/PaintFact.hpp"
#include <Quickdraw.h>

class ToolboxScenePlatformController;
namespace loka
{
  namespace app
  {
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

namespace loka
{
  namespace core
  {
    template <typename T> class State;
    namespace scene
    {
      class BoundaryNode;
    }
  } // namespace core
} // namespace loka

class ToolboxCellContext : public ToolboxProjectedNodeContext
{
public:
  ToolboxCellContext(loka::app::CellNode *node, ToolboxScenePlatformController *controller);
  virtual ~ToolboxCellContext();
  virtual void onPropsApplied();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                            loka::app::scene::NodeLifecycleFact next);

  void updateData(loka::core::State<loka::core::String> *text);
  void updateRect(const Rect &rect);
  loka::core::State<loka::core::String> *liveTextState() const
  {
    return this->node_ ? ToolboxLiveTextSource(this->text_, this->node_->props.ownsText_) : 0;
  }
  void draw(ToolboxScenePlatformController *controller);
  virtual void render(loka::app::scene::IPlatformController *controller);
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  bool handleMouseDown(const Point &point, ToolboxScenePlatformController *controller);

private:
  friend class ToolboxScenePlatformController;
  /** Capture local data and report whether existing controller rows need refresh. */
  bool captureProps();
  loka::app::CellNode *node_;
  Rect rect_;
  Rect paintRect_;
  /** The layout input, not the resulting rectangle, determines text sizing. */
  bool widthFromText_;
  loka::app::scene::PaintFact<loka::core::String> presented_;
  loka::core::State<loka::core::String> *text_;
};

bool RegisterToolboxCellNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_TOOLBOX_CELL_CONTEXT_HPP
