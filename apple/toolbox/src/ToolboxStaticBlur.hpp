#ifndef LOKA_TOOLBOX_STATIC_BLUR_HPP
#define LOKA_TOOLBOX_STATIC_BLUR_HPP
#include "ToolboxUnitLedger.hpp"
#include "ToolboxErasedPass.hpp"

/** Native pixels have no callbacks or deferred residents. Candidate construction
    commits only complete pixels; the image owns its world and temporary bytes. */
class ToolboxStaticBlurPixels
{
public:
  ToolboxStaticBlurPixels();
  ~ToolboxStaticBlurPixels();
  static bool eligible(GrafPtr window, const Rect &rect);
  bool capture(GrafPtr window, const Rect &rect);
  bool paint(GrafPtr window, const Rect &rect);
  void clear();

private:
  friend class ToolboxStaticBlurInspection;
  struct Image;
  Image *image_;
  /** Last capture stages and copy-back, in Microseconds; private diagnostics. */
  unsigned long timings_[5];
  ToolboxStaticBlurPixels(const ToolboxStaticBlurPixels &);
  ToolboxStaticBlurPixels &operator=(const ToolboxStaticBlurPixels &);
};

/** One frozen lifetime per controller/window. The ledger chooses its first
    static unit; the key is borrowed identity only and is never dereferenced. */
class ToolboxStaticBlur
{
public:
  enum Phase
  {
    NEEDED,
    REQUESTED,
    READY,
    PLAIN
  };
  ToolboxStaticBlur()
      : stack_(0),
        rect_(),
        phase_(NEEDED)
  {
  }
  void clear()
  {
    this->pixels_.clear();
    this->stack_ = 0;
    this->phase_ = NEEDED;
  }
  /** Once after layout; O(1), with a row selected by the ledger owner. */
  void reconcile(const ToolboxUnitLedger::UnitSeat *row)
  {
    if (row && this->matches(*row))
      return;
    this->clear();
    if (row)
    {
      this->stack_ = row->stack;
      this->rect_ = row->rect;
    }
  }
  /** One render entry owns this scope; aborted layout releases by construction. */
  class LayoutScope
  {
  public:
    explicit LayoutScope(ToolboxStaticBlur &owner)
        : pending_(&owner)
    {
    }
    ~LayoutScope()
    {
      if (this->pending_)
        this->pending_->clear();
    }
    void commit(const ToolboxUnitLedger::UnitSeat *row)
    {
      this->pending_->reconcile(row);
      this->pending_ = 0;
    }

  private:
    ToolboxStaticBlur *pending_;
    LayoutScope(const LayoutScope &);
    LayoutScope &operator=(const LayoutScope &);
  };
  /** Once per first-unit paint, after ground and unit-scope admission. O(w*h)
      only for this owner's snapshot; READY never reads the live backdrop. */
  bool paint(GrafPtr window, const ToolboxUnitLedger::UnitSeat &row, const ToolboxErasedPass &pass)
  {
    if (!this->matches(row))
      return false;
    const bool pending = this->phase_ == NEEDED || this->phase_ == REQUESTED;
    if (!ToolboxStaticBlurPixels::eligible(window, row.rect))
    {
      // No replay can capture on this screen: end the lifetime plain instead
      // of asking for one every flush. READY keeps its picture for later.
      if (pending)
        this->phase_ = PLAIN;
      return false;
    }
    if (pending)
      this->phase_ = pass.admitsCapture(row.rect) ? (this->pixels_.capture(window, row.rect) ? READY : PLAIN) : NEEDED;
    return this->phase_ == READY && this->pixels_.paint(window, row.rect);
  }
  /** Once after each flush, after consumed rectangles have been cleared.
      Only a lifetime that a walk painted without an admitted capture is
      NEEDED (a swallowed request repaints it so), so a flush that never
      reached the unit asks nothing. Not from the render walk or scheduler
      drain. No rows are walked. */
  const Rect *completedFlush()
  {
    if (!this->stack_ || this->phase_ != NEEDED)
      return 0;
    this->phase_ = REQUESTED;
    return &this->rect_;
  }

private:
  friend class ToolboxStaticBlurInspection;
  bool matches(const ToolboxUnitLedger::UnitSeat &row) const
  {
    return this->stack_ && this->stack_ == row.stack && row.index == 1 && EqualRect(&this->rect_, &row.rect);
  }
  loka::app::ZStackNode *stack_;
  Rect rect_;
  Phase phase_;
  ToolboxStaticBlurPixels pixels_;
  ToolboxStaticBlur(const ToolboxStaticBlur &);
  ToolboxStaticBlur &operator=(const ToolboxStaticBlur &);
};
#endif
