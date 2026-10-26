#ifndef LOKA_TESTS_PLATFORM_NULL_TEXT_CONTEXT_HPP
#define LOKA_TESTS_PLATFORM_NULL_TEXT_CONTEXT_HPP

#include "app/scene/projection/NativeNodeContext.hpp"
#include "app/nodes/Text.hpp"
#include "platform/null/context/NullTextMetrics.hpp"
#include "platform/null/context/NullPaintPlacement.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace loka
{
  namespace app
  {
    class TextNode;
    namespace testing
    {
      struct NullTextStyleAccess;
    }
  }
} // namespace loka

/** Resolved, pointer-free paint inputs; live style state is read when captured. */
struct NullTextPaintStyle
{
  NullTextPaintStyle()
      : fontSize(12),
        weight(loka::app::TEXT_WEIGHT_NORMAL),
        color(loka::app::TEXT_COLOR_UNSET),
        italic(false),
        wrap(loka::app::TEXT_WRAP_NONE),
        truncation(loka::app::TEXT_TRUNCATION_NONE),
        align(loka::app::TEXT_ALIGN_LEFT)
  {
  }
  explicit NullTextPaintStyle(const loka::app::TextProps &props);
  bool operator==(const NullTextPaintStyle &other) const
  {
    return fontSize == other.fontSize && weight == other.weight && italic == other.italic && color == other.color
           && family == other.family && wrap == other.wrap && truncation == other.truncation && align == other.align;
  }
  int fontSize;
  loka::app::TextWeight weight;
  loka::app::TextColor color;
  bool italic;
  loka::app::TextFamilyValue family;
  loka::app::TextWrap wrap;
  loka::app::TextTruncation truncation;
  loka::app::TextAlign align;
};

class NullTextContext : public loka::app::scene::NativeNodeContext
{
public:
  explicit NullTextContext(loka::app::TextNode *node);
  virtual ~NullTextContext();

  void readLifecycleFactOnAttach();

  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);

  const NullTextMeasurement &measurement() const;

  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &query) const;
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  using loka::app::scene::NativeNodeContext::commitPresented;
  bool commitPresented(const loka::core::String &value, const loka::app::scene::PaintScope &scope);
  void invalidatePaintHistory()
  {
    this->presented_.invalidate();
  }
  /** Called before every fallible projection: a placement is a derived cache and is
      re-established only by a successful layout in that pass (AGENTS.md
      failure-degradation). A refused projection therefore leaves no stale seat. */
  void invalidatePresentation()
  {
    this->presented_.invalidate();
    this->placement_.invalidate();
  }

private:
  friend struct loka::app::testing::NullTextMeasurementAccess;
  friend struct loka::app::testing::NullTextStyleAccess;
  loka::app::scene::PaintFact<loka::core::String> presented_;
  NullPaintPlacement placement_;
  NullTextPaintStyle placedStyle_;
  unsigned measurementBuilds_;
  loka::app::TextNode *node_;
  NullTextMeasurement measurement_;
};

namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Captured family from the last placement/presentation, not a props reread. */
      struct NullTextStyleAccess
      {
        static TextFamily family(const NullTextContext &context)
        {
          return context.placedStyle_.family.family();
        }
      };
    } // namespace testing
  } // namespace app
} // namespace loka

void RegisterNullTextNodeHandler(NullScenePlatformController &controller);
/** The Null presenter calls typed completion doors on these contexts, so the rail
    refuses to let a foreign handler replace them (NullScenePlatformController::registerNodeHandler). */
const void *NullTextNodeHandlerKey();
bool IsNullTextNodeHandler(const loka::app::scene::IPlatformNodeHandler *handler);

#endif // LOKA_TESTS_PLATFORM_NULL_TEXT_CONTEXT_HPP
