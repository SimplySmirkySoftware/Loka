#ifndef LOKA_MAC_ZSTACK_CONTEXT_HPP
#define LOKA_MAC_ZSTACK_CONTEXT_HPP

#include "MacRetirableContext.hpp"
#include "../platform/MacProjection.hpp"

/** Owns one opaque view per upper child of a fixed ZStack composition.
    Deliberate twin of Win32ZStackContext; AppKit units are inserted once so
    relayout never removes the first responder from its window. */
class MacZStackContext : public MacRetirableContext
{
public:
  class Unit
  {
  public:
    explicit Unit(unsigned index);
    bool serves(unsigned index) const
    {
      return this->index_ == index;
    }
    /** Layout calls once per upper child per pass; constant work, one native
        allocation on first admission. The context owns the retained view. A unit
        refused on one pass and created later lands in composition order using
        the nearest later (below) or earlier (above) own view; no existing view
        moves. */
    bool ensure(void *parent, const loka::macos::MacRect &seat, void *below, void *above);
    /** Layout follows own rows to find the next admitted view on creation;
        constant access per row, with no parent-subview scan. */
    const Unit *following() const
    {
      return this->next;
    }
    void place(const loka::macos::MacRect &seat);
    void *view() const
    {
      return this->view_;
    }

  private:
    friend class MacZStackContext;
    const unsigned index_;
    Unit *next;
    void *view_;
  };

  explicit MacZStackContext(MacScenePlatformController *controller);
  virtual ~MacZStackContext();
  /** Layout's pass-local cursor visits each owned row once per composition. */
  Unit *unitFor(unsigned index, Unit **&cursor);
  Unit **unitsBegin()
  {
    return &this->units_;
  }
  /** The ZStack owns z order within its seat. Layout records one last-subview
      pointer per pass; this door moves only the views ground created in that
      pass, below the first existing own unit. Each candidate checks the own
      unit list before moving; existing units are never re-added. A missing
      snapshot refuses repair, since its new suffix cannot be identified. One
      parent-subview scan locates the snapshot; the fixed suffix bounds moves. */
  void lowerNewGroundViews(void *parent, void *previousLast);
  /** One own-unit walk per lifecycle fact; terminal removal precedes the
      controller's safe-point release, on every terminal path. */
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact previous, loka::app::scene::NodeLifecycleFact next);

private:
  Unit *units_;
  MacZStackContext(const MacZStackContext &);
  MacZStackContext &operator=(const MacZStackContext &);
};
#endif
