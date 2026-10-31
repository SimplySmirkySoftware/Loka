#ifndef LOKA_TOOLBOX_BUTTON_CONTEXT_HPP
#define LOKA_TOOLBOX_BUTTON_CONTEXT_HPP

#include "context/ToolboxProjectedNodeContext.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "app/nodes/controls/Button.hpp"
#include "core/String.hpp"
#include <Quickdraw.h>
#include <Controls.h>
#include <string>

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    void failToolboxButtonDrawConversions(unsigned count);
    bool consumeToolboxButtonDrawConversionRefusal();
  } // namespace testing
} // namespace loka
#endif

/** Reconcile one installed control; the controller owns its title cache. */
bool ReconcileToolboxButtonControl(ControlRef control, const loka::core::String &label,
                                   loka::core::State<bool> *enabled, std::string &installedLabel);

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

/** Completed native button inputs; comparisons never inspect controller rows. */
class ToolboxButtonPaintValue
{
public:
  ToolboxButtonPaintValue() : label_(), enabled_(false) {}
  ToolboxButtonPaintValue(const loka::core::String &label, bool enabled) : label_(label), enabled_(enabled) {}
  bool operator==(const ToolboxButtonPaintValue &other) const
  {
    return this->enabled_ == other.enabled_ && this->label_.equals(other.label_);
  }
  const loka::core::String &label() const { return this->label_; }
private:
  loka::core::String label_;
  bool enabled_;
};

class ToolboxButtonContext : public ToolboxProjectedNodeContext
{
public:
  ToolboxButtonContext(loka::app::ButtonNode *node, ToolboxScenePlatformController *controller);
  virtual ~ToolboxButtonContext();
  virtual void onPropsApplied();
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next);

  void updateData(const loka::core::String &label,
                  loka::core::EmitterState *emitter,
                  loka::core::State<bool> *enabled,
                  short resourceId,
                  int controlTag);
  void updateRect(const Rect &rect);
  void draw(ToolboxScenePlatformController *controller);
  /** Replay borrows the controller's current row only for this call. */
  void repaint(ControlRef control, std::string &installedLabel);
  /** Native removal revokes the completed installation/presentation fact. */
  void forgetPresentedControl();
  virtual void render(loka::app::scene::IPlatformController *controller);
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  bool handleMouseDown(const Point &point, ToolboxScenePlatformController *controller);

private:
  friend class ToolboxScenePlatformController;
  /** Capture local data and report whether existing controller rows need refresh. */
  bool captureProps();
  /** Native title width plus the push-button inset on both sides. */
  short naturalWidth(const loka::core::String &label) const;
  virtual void retireNativeProjection();
  loka::app::ButtonNode *node_;
  Rect rect_;
  Rect paintRect_;
  /** Layout provenance cannot be recovered from a coincidentally equal width. */
  bool widthFromText_;
  loka::core::String label_;
  loka::core::EmitterState *emitter_;
  loka::core::State<bool> *enabled_;
  short resourceId_;
  loka::app::scene::PaintFact<ToolboxButtonPaintValue> presented_;
};

bool RegisterToolboxButtonNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_TOOLBOX_BUTTON_CONTEXT_HPP
