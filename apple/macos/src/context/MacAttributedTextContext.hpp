#ifndef LOKA_MAC_ATTRIBUTED_TEXT_CONTEXT_HPP
#define LOKA_MAC_ATTRIBUTED_TEXT_CONTEXT_HPP

#include "MacRetirableContext.hpp"
#include "../platform/MacProjection.hpp"
#include "app/layout/MeasurementResult.hpp"
#include "app/layout/TextLineBreaker.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"

class MacScenePlatformController;
namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class MacAttributedTextTestAccess;
    }
  } // namespace dsl
} // namespace loka

/** WHOLE_LINE projection; the completed attributed object belongs to this
    context, independently of AppKit's presented pixels. */
class MacAttributedTextContext : public MacRetirableContext
{
public:
  MacAttributedTextContext(MacScenePlatformController *controller,
                           void *parentView,
                           loka::app::AttributedTextNode *node);
  virtual ~MacAttributedTextContext();
  /** False when AppKit refused the native label; the handler then refuses the context. */
  bool hasNativeLabel() const
  {
    return this->label_ != 0;
  }
  void readLifecycleFactOnAttach();
  virtual void onPropsApplied();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);

private:
  friend class loka::dsl::testing::MacAttributedTextTestAccess;
  /** One ordered slot per input segment, in completed-string UTF-16 units.
      Empty intervals own no units and never lie inside a non-empty interval. */
  struct MacTextSourceSlot
  {
    NSUInteger start, end;
  };

  /** Projection-owned source intervals; only build fills the reserved slots. */
  class MacTextSourceTable
  {
  public:
    bool reserve(std::size_t count)
    {
      return this->slots_.allocate(count);
    }
    MacTextSourceSlot &slot(std::size_t segment)
    {
      return this->slots_[segment];
    }
    void clear()
    {
      this->slots_.clear();
    }
    bool queryProvenance(NSUInteger start, NSUInteger end, loka::app::TextSegmentRange &out) const;

  private:
    loka::app::detail::TextMeasureTable<MacTextSourceSlot> slots_;
  };

  /** Null is unknown/refused; a completed object is immutable until clear. */
  class Projection
  {
  public:
    Projection();
    ~Projection();
    void clear();
    bool build(const loka::app::AttributedString &value,
               const loka::app::BlockStyle &block,
               const MacScenePlatformController &controller,
               const loka::macos::MacLength &constraint);
    bool queryProvenance(NSUInteger start, NSUInteger end, loka::app::TextSegmentRange &out) const;
    bool reusable(const loka::macos::MacLength &constraint) const
    {
      return this->measurement_.reusable(constraint.pt);
    }
    int height() const
    {
      return this->value_ ? this->measurement_.extent() : 0;
    }
    void *value() const
    {
      return this->value_;
    }

  private:
    friend class loka::dsl::testing::MacAttributedTextTestAccess;
    /** Construction body of build; false leaves partial state for build to clear. */
    bool fill(const loka::app::AttributedString &value,
              const loka::app::BlockStyle &block,
              const MacScenePlatformController &controller,
              const loka::macos::MacLength &constraint);
    MacTextSourceTable sources_;
    // No backing-scale invalidation until a measurement effect is demonstrated (#970).
    loka::app::MeasurementResult<CGFloat, int> measurement_;
    void *value_;
    Projection(const Projection &);
    Projection &operator=(const Projection &);
  };
  void clearProjection();
  loka::app::AttributedTextNode *node_;
  void *label_;
  Projection projection_;
};

void RegisterMacAttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);
#endif
