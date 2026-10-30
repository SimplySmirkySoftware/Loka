#ifndef LOKA_MAC_TEXT_CONTEXT_HPP
#define LOKA_MAC_TEXT_CONTEXT_HPP

#include "MacRetirableContext.hpp"
#include "../MacObjCCompat.hpp"
#include "app/layout/MeasurementResult.hpp"
#include "core/State.hpp"
#include "core/String.hpp"

namespace loka
{
  namespace app
  {
    class TextNode;
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka

class MacScenePlatformController;

class MacTextContext : public MacRetirableContext, public loka::app::scene::ICapturableBitmap
{
public:
  MacTextContext(MacScenePlatformController *controller,
                 void *parentView,
                 int x,
                 int y,
                 int width,
                 int height,
                 loka::app::TextNode *node);
  virtual ~MacTextContext();
  /** Current single-line intrinsic width, using the Text layout measurement home.
      Refusal leaves width untouched; no cache or native context is required. */
  static bool
  queryNaturalWidth(const loka::app::TextNode *text, const MacScenePlatformController *controller, short &width);
  /** Current one-line leaf height; refusal leaves height untouched. */
  static bool
  queryNaturalHeight(const loka::app::TextNode *text, const MacScenePlatformController *controller, short &height);
  virtual void onPropsApplied();
  virtual loka::app::scene::ICapturableBitmap *asCapturableBitmap()
  {
    return this;
  }
  virtual const loka::app::scene::ICapturableBitmap *asCapturableBitmap() const
  {
    return this;
  }
  virtual bool captureBitmap(loka::core::resource::Image &out) const;
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);
  /** Attach-time read (late-subscriber rule): presentation from the current
      fact, called by the installing handler right after setContext. */
  void readLifecycleFactOnAttach();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                             loka::app::scene::NodeLifecycleFact next);
  void relayout(int x, int y, int width, int height);

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  void bindText();
  void unbindText();
  void applyText();
  void clearMeasurement();
  bool applyStyle(bool initial = false);
  void requestRelayoutIfNeeded();
  static void TextChangedThunk(void *userData);

  // Controller fonts/metrics are fixed; style activation/removal/value changes carry #965 marks.
  // No backing-scale invalidation until a measurement effect is demonstrated (#970).
  loka::app::MeasurementResult<CGFloat, int> measurement_;
  loka::app::TextNode *node_;
  void *parentView_;
  void *label_;
  void *originalFont_;
  loka::core::State<loka::core::String> *textState_;
  bool textStateBound_;
  bool didInitialApply_;
};

void RegisterMacTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_MAC_TEXT_CONTEXT_HPP
