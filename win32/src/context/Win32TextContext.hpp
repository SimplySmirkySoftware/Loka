#ifndef LOKA_WIN32_TEXT_CONTEXT_HPP
#define LOKA_WIN32_TEXT_CONTEXT_HPP

#include <windows.h>
#include <string>
#include "Win32RetirableContext.hpp"
#include "../Win32TextEnvironment.hpp"
#include "core/String.hpp"
#include "app/style/Style.hpp"
#include "app/layout/MeasurementResult.hpp"

namespace loka
{
  namespace testing
  {
    class Win32TextColorAccess;
    class Win32TextFamilyAccess;
  }
  namespace core
  {
    template <typename T> class State;
  }
} // namespace loka

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

class Win32ScenePlatformController;

class Win32TextContext : public Win32RetirableContext,
                         public Win32TextEnvironment::Listener,
                         public loka::app::scene::ICapturableBitmap
{
public:
  Win32TextContext(Win32ScenePlatformController *controller,
                   HWND parent,
                   int x,
                   int y,
                   int width,
                   int height,
                   loka::app::TextNode *node);
  virtual ~Win32TextContext();
  /** Native CALCRECT operation shared by constrained height and natural width.
      The caller supplies the selected Text font and the initial rectangle; a
      null font measures with the DC's current font, the historical unstyled
      height. */
  static bool measureTextRect(HWND hwnd, HFONT font, const std::wstring &wide, RECT &rect);
  /** One-line width using the same font and rectangle measurement as Text layout.
      Refusal leaves width untouched; no context or measurement cache is created. */
  static bool
  queryNaturalWidth(const loka::app::TextNode *node, const Win32ScenePlatformController *controller, short &width);
  /** One-line height shared with Text layout; refusal leaves height untouched. */
  static bool
  queryNaturalHeight(const loka::app::TextNode *node, const Win32ScenePlatformController *controller, short &height);
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
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
  virtual void onPropsApplied();
  virtual void onTextEnvironmentChanged();
  /** HWND-local intent lookup, paired with terminal property removal. */
  static Win32TextContext *fromWindow(HWND window);
  bool queryFontStyle(loka::app::TextStyle &style) const;
  void relayout(int x, int y, int width, int height);

protected:
  virtual void onNativeFact(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  friend class loka::testing::Win32TextColorAccess;
  friend class loka::testing::Win32TextFamilyAccess;
  Win32TextEnvironment::Subscription textEnvironmentSubscription_;
  /** Plain measurement is keyed by the native width the STATIC receives
      (edges projected at the placement), so a placement whose rounding
      changes the native width is a miss, as on the attributed rail. */
  struct Constraint
  {
    Constraint(int w = 0, HFONT f = 0) : width(w), font(f) {}
    bool operator==(const Constraint &other) const
    {
      return this->width == other.width && this->font == other.font;
    }
    int width;
    HFONT font;
  };
  loka::app::MeasurementResult<Constraint, int> measurement_;
  void clearMeasurement();
  bool applyStyle();
  void applyAttachedPresentation();
  void applyDetachedPresentation();
  void bindText();
  void unbindText();
  void applyText();
  bool writeText(const std::wstring &wide);
  void requestRelayoutIfNeeded();
  static void TextChangedThunk(void *userData);

  loka::app::TextNode *node_;
  loka::core::State<loka::core::String> *textState_;
  bool didInitialApply_;
  /** Completed outcome of applyText's native submission, never a future promise. */
  loka::app::scene::PaintAnswer textDelivery_;
};

void RegisterWin32TextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);

#endif // LOKA_WIN32_TEXT_CONTEXT_HPP
