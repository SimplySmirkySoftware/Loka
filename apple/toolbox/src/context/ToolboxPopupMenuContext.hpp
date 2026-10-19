#ifndef LOKA_TOOLBOX_POPUP_MENU_CONTEXT_HPP
#define LOKA_TOOLBOX_POPUP_MENU_CONTEXT_HPP

#include "context/ToolboxProjectedNodeContext.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "core/Vector.hpp"
#include "app/scene/projection/PaintFact.hpp"
#include <Quickdraw.h>
#include <Menus.h>

class ToolboxScenePlatformController;
class ToolboxPaintClip;
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
    namespace scene
    {
      class BoundaryNode;
    }
  } // namespace core
} // namespace loka
namespace loka
{
  namespace core
  {
    namespace scene
    {
      class IPlatformController;
    }
  } // namespace core
} // namespace loka

class ToolboxPopupMenuContext : public ToolboxProjectedNodeContext
{
public:
  ToolboxPopupMenuContext(loka::app::PopupMenuNode *node, ToolboxScenePlatformController *controller);
  virtual ~ToolboxPopupMenuContext();
  virtual void onPropsApplied();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                            loka::app::scene::NodeLifecycleFact next);

  void updateData(const loka::Vector<loka::core::String> *items,
                  loka::core::State<int> *selectedIndex,
                  const loka::app::scene::WriteSeat<int> &selectedIndexSeat,
                  loka::core::EmitterState *onChange,
                  loka::core::State<bool> *enabled);
  void updateRect(const Rect &rect, short lineHeight);
  void draw();
  /** Replay erases visible paint bounds before painting the captured face. */
  void repaint();
  virtual void render(loka::app::scene::IPlatformController *controller);
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  bool handleMouseDown(const Point &point, ToolboxScenePlatformController *controller);

  const Rect &rect() const
  {
    return rect_;
  }

private:
  friend class ToolboxScenePlatformController;
  /** Capture local data and report whether existing controller rows need refresh. */
  bool captureProps();
  /** Completed face inputs; only the drawer owns presentation history. */
  class FaceValue
  {
  public:
    FaceValue() : label_(), selectedIndex_(0), enabled_(true) {}
    FaceValue(const loka::core::String &label, int selectedIndex, bool enabled)
        : label_(label), selectedIndex_(selectedIndex), enabled_(enabled) {}
    const loka::core::String &label() const { return this->label_; }
    bool equals(const FaceValue &other) const
    {
      return this->label_.equals(other.label_) && this->selectedIndex_ == other.selectedIndex_
             && this->enabled_ == other.enabled_;
    }
  private:
    loka::core::String label_;
    int selectedIndex_;
    bool enabled_;
  };
  FaceValue faceValue() const;
  void paintFace(const ToolboxPaintClip &clip);
  short clampIndex(int index) const;
  short menuId() const;

  loka::app::PopupMenuNode *node_;
  Rect rect_;
  Rect paintRect_;
  loka::app::scene::PaintFact<FaceValue> presented_;
  short lineHeight_;
  const loka::Vector<loka::core::String> *items_;
  loka::core::State<int> *selectedIndex_;
  loka::app::scene::WriteSeat<int> selectedIndexSeat_;
  loka::core::EmitterState *onChange_;
  loka::core::State<bool> *enabled_;
};

bool RegisterToolboxPopupMenuNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_TOOLBOX_POPUP_MENU_CONTEXT_HPP
