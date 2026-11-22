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

/** Completed source hit. Deliberate twin of LinkHit/HitAnswer in win32/src/context/Win32AttributedTextTable.hpp.
    The later predicate resolves links, and the applied container width supplies the seat dimension. */
struct MacTextHit
{
  loka::app::TextSegmentRange source;
  CGFloat containerWidth;
};
/** Text and container width certified by the current native presentation. */
struct MacPresentedText
{
  loka::app::AttributedString value;
  CGFloat width;
};

/** One constant-time test of the leaf's resolved QuartzCore headers. */
bool MacTextLayerCompiledIn();
/** One NSView capability query at each layer entry, no row walk. */
bool MacTextLayerCapable();
enum MacTextHitAnswer
{
  MAC_HIT_SOURCE,
  MAC_HIT_NONE,
  MAC_HIT_REFUSED
};

/** WHOLE_LINE projection; the completed attributed object belongs to this
    context, independently of AppKit's presented pixels. */
class MacAttributedTextContext : public MacRetirableContext
{
public:
  MacAttributedTextContext(MacScenePlatformController *controller,
                           void *parentView,
                           loka::app::AttributedTextNode *node);
  virtual ~MacAttributedTextContext();
  /** False when AppKit refused the native view; the handler then refuses the context. */
  bool hasNativeView() const
  {
    return this->view_ != 0;
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
               const MacScenePlatformController &controller);
    bool queryProvenance(NSUInteger start, NSUInteger end, loka::app::TextSegmentRange &out) const;
    bool reusable(const loka::macos::MacLength &constraint) const
    {
      return this->measurement_.reusable(constraint.pt);
    }
    int height() const
    {
      CGFloat constraint;
      return this->measurement_.queryConstraint(constraint) ? this->measurement_.extent() : 0;
    }
    /** Borrow once per install or candidate. O(1), even for a multi-segment value. */
    const loka::app::AttributedString &snapshot() const
    {
      return this->snapshot_;
    }
    void *value() const
    {
      return this->value_;
    }

  private:
    friend class loka::dsl::testing::MacAttributedTextTestAccess;
    friend class MacAttributedTextContext;
    void commitMeasurement(const loka::macos::MacLength &constraint, int extent)
    {
      this->measurement_.commit(constraint.pt, extent);
    }
    /** Construction body of build; false leaves partial state for build to clear. */
    bool fill(const loka::app::AttributedString &value,
              const loka::app::BlockStyle &block,
              const MacScenePlatformController &controller);
    MacTextSourceTable sources_;
    // No backing-scale invalidation until a measurement effect is demonstrated (#970).
    loka::app::MeasurementResult<CGFloat, int> measurement_;
    // Deliberate twin of the Toolbox table snapshot_, reset and published with its table.
    loka::app::AttributedString snapshot_;
    void *value_;
    Projection(const Projection &);
    Projection &operator=(const Projection &);
  };
  /** Publishes out only on SOURCE. Tests only here; at most twice per future press,
      never for an UP without a candidate. One view query and one provenance query at most;
      the projection searches its own covered slots in O(covered slots x log(segments + 1)). */
  MacTextHitAnswer prepareSourceHit(NSPoint viewPoint, MacTextHit &out) const;
  /** Once per candidate, one native presentation read followed by one source preparation. */
  MacTextHitAnswer prepareTextHit(NSPoint viewPoint, MacTextHit &out, MacPresentedText &fact) const;
  /** At most twice per press. One preparation, up to two segment comparisons and one maximal run in this text. */
  bool queryPressCandidate(NSPoint viewPoint, loka::app::AttributedString::LinkRun &out) const;
  bool rebuildProjection(const loka::macos::MacLength &constraint);
  void clearProjection();
  loka::app::AttributedTextNode *node_;
  void *view_;
  Projection projection_;
};

void RegisterMacAttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);
#endif
