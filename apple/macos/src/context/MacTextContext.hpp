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
namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class MacTextVersionTestAccess;
    }
  } // namespace dsl
  namespace macos
  {
    struct MacLength;
  }
} // namespace loka

/** Completed native inputs, retained together with their synthesized value.
    Opaque pointers keep AppKit out of non-Objective-C translation units. */
class MacTextVersion
{
public:
  struct Cell
  {
    Cell();
    bool wraps, scrollable, singleLine;
    int lineBreakMode, alignment;
    void *font;
    void *colour;
  };
  typedef loka::app::MeasurementResult<CGFloat, int> Measure;
  MacTextVersion();
  MacTextVersion(void *value, const Cell &cell, bool wraps, const Measure &measure);
  MacTextVersion(const MacTextVersion &other);
  MacTextVersion &operator=(const MacTextVersion &other);
  ~MacTextVersion();
  void *value() const
  {
    return this->value_;
  }
  const Cell &cell() const
  {
    return this->cell_;
  }
  bool wraps() const
  {
    return this->wraps_;
  }
  const Measure &measure() const
  {
    return this->measure_;
  }

private:
  void swap(MacTextVersion &other);
  void *value_;
  Cell cell_;
  bool wraps_;
  Measure measure_;
};

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
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  void relayout(int x, int y, int width, int height);

private:
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  void bindText();
  void unbindText();
  MacTextVersion buildVersion(const loka::macos::MacLength *constraint) const;
  void present(bool newVersion);
  void replaceFromProps();
  friend class loka::dsl::testing::MacTextVersionTestAccess;
  static void TextChangedThunk(void *userData);

  // Controller fonts/metrics are fixed; style activation/removal/value changes carry #965 marks.
  // No backing-scale invalidation until a measurement effect is demonstrated (#970).
  MacTextVersion version_;
  loka::app::TextNode *node_;
  void *label_;
  loka::core::State<loka::core::String> *textState_;
  bool textStateBound_;
};

void RegisterMacTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_MAC_TEXT_CONTEXT_HPP
