#ifndef LOKA_TOOLBOX_TEXT_CONTEXT_HPP
#define LOKA_TOOLBOX_TEXT_CONTEXT_HPP

#include "context/ToolboxProjectedNodeContext.hpp"
#include "app/nodes/Text.hpp"
#include "core/String.hpp"
#include "app/layout/MeasurementResult.hpp"
#include "ToolboxPropsRefresh.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include <Quickdraw.h>
#include "platform/ToolboxPascalText.hpp"

class ToolboxScenePlatformController;
class ToolboxPlainTextLines;
namespace loka
{
  namespace testing
  {
    class ToolboxTextContextAccess;
  }
} // namespace loka
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

class ToolboxTextContext : public ToolboxProjectedNodeContext
{
public:
  ToolboxTextContext(loka::app::TextNode *node, ToolboxScenePlatformController *controller);
  virtual ~ToolboxTextContext();
  virtual void onPropsApplied();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  /** Repaint the captured visible placement without registering another hit. */
  void repaint();

  void updateData(loka::core::State<loka::core::String> *text);
  void updateRect(const Rect &rect, short textX, short textY);
  short visibleWidth();
  loka::core::State<loka::core::String> *liveTextState() const
  {
    return this->node_ ? ToolboxLiveTextSource(this->text_, this->node_->props.ownsText()) : 0;
  }
  void draw(ToolboxScenePlatformController *controller);
  virtual loka::core::State<loka::core::String> *projectedTextState()
  {
    return text_;
  }
  virtual void render(loka::app::scene::IPlatformController *controller);
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);

private:
  friend class loka::testing::ToolboxTextContextAccess;
  /** Rail inputs, including the legacy unset-font-size line pitch. */
  struct Constraint
  {
    short width, lineHeight;
    Constraint(short w = 0, short h = 0)
        : width(w),
          lineHeight(h)
    {
    }
    bool operator==(const Constraint &other) const
    {
      return this->width == other.width && this->lineHeight == other.lineHeight;
    }
  };
  /** Intrinsic geometry only; placement always uses the current LayoutState. */
  struct Extent
  {
    short height, baselineOffset, measuredWidth;
    loka::core::Managed<ToolboxPlainTextLines> lines;
    Extent(short h = 0, short b = 0, short w = 0)
        : height(h),
          baselineOffset(b),
          measuredWidth(w)
    {
    }
  };
  loka::app::MeasurementResult<Constraint, Extent> measurement_;
  /** Capture local data and report whether existing controller rows need refresh. */
  bool captureProps();
  void paint(bool erase);
  /** Reconcile the complete logical source before any width or paint entry. */
  bool reconcileProjection();
  ToolboxNativeText projection_;
  /** Revoke both measurement and its placement before refusal can be painted. */
  void clearMeasurement();
  loka::app::TextNode *node_;
  Rect rect_;
  Rect paintRect_;
  loka::app::scene::PaintFact<loka::core::String> presented_;
  short textX_;
  short textY_;
  short maxWidth_;
  loka::app::TextWrap wrapMode_;
  loka::app::TextTruncation truncationMode_;
  loka::core::State<loka::core::String> *text_;
};

bool RegisterToolboxTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_TOOLBOX_TEXT_CONTEXT_HPP
