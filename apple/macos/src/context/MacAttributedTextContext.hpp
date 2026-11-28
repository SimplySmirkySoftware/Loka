#ifndef LOKA_MAC_ATTRIBUTED_TEXT_CONTEXT_HPP
#define LOKA_MAC_ATTRIBUTED_TEXT_CONTEXT_HPP

#include "MacRetirableContext.hpp"
#include "../platform/MacProjection.hpp"
#include "app/internal/LinkPressSession.hpp"
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

/** A candidate copies the maximal run and native number of one primary DOWN. */
struct MacPressedLink
{
  loka::app::internal::PressedRun run;
  NSInteger number;
};

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
  /** One O(1) press invalidation per transition away from ATTACHED, before this leaf's native cleanup. */
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);
  virtual short layout(loka::app::scene::IPlatformController *controller, loka::app::scene::LayoutState &state);

private:
  friend class loka::dsl::testing::MacAttributedTextTestAccess;
  friend class MacInputDoor;
  /** Per live primary DOWN, at most one preparation and run resolution on this leaf. */
  void handleLinkDown(void *event);
  /** Per live primary UP with a candidate, the same query plus one own run resolution and app observers on delivery. */
  void handleLinkUp(void *event);
  class MacLinkPressTable;
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
    friend class MacLinkPressTable;
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
  /** Publishes out only on SOURCE. At most twice per primary press,
      never for an UP without a candidate. One view query and one provenance query at most;
      the projection searches its own covered slots in O(covered slots x log(segments + 1)). */
  MacTextHitAnswer prepareSourceHit(NSPoint viewPoint, MacTextHit &out) const;
  /** Once per candidate, one native presentation read followed by one source preparation. */
  MacTextHitAnswer prepareTextHit(NSPoint viewPoint, MacTextHit &out, MacPresentedText &fact) const;
  /** At most twice per press. One preparation, up to two segment comparisons and one maximal run in this text. */
  bool queryPressCandidate(NSPoint viewPoint, loka::app::AttributedString::LinkRun &out) const;
  /** Invalidation and projection mutation have one owner, as in Win32LinkPressTable. */
  class MacLinkPressTable
  {
  public:
    /** One constant-time borrow per projection read. */
    const Projection &table() const
    {
      return this->projection_;
    }
    /** One borrow per input procedure, independent of text length. */
    loka::app::internal::LinkPressSession<MacPressedLink> &session()
    {
      return this->session_;
    }
    /** Once before native work on clear or leaving ATTACHED. No row walk. */
    void invalidatePress()
    {
      this->session_.invalidate();
    }
    /** One uninstall per rebuild, refusal, props apply or retirement. Clears this projection's slots. */
    void clear(void *view);
    /** Once per dirty or uncached layout, builds and measures only this node's segments. */
    bool rebuild(const MacAttributedTextContext &context, const loka::macos::MacLength &constraint);

  private:
    friend class loka::dsl::testing::MacAttributedTextTestAccess;
    Projection projection_;
    loka::app::internal::LinkPressSession<MacPressedLink> session_;
  };
  loka::app::AttributedTextNode *node_;
  void *view_;
  MacLinkPressTable links_;
};

void RegisterMacAttributedTextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry);
#endif
