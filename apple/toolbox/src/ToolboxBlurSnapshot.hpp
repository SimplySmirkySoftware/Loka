#ifndef LOKA_TOOLBOX_BLUR_SNAPSHOT_HPP
#define LOKA_TOOLBOX_BLUR_SNAPSHOT_HPP
#include "ToolboxUnitLedger.hpp"
#include "ToolboxErasedPass.hpp"

/** Native pixels have no callbacks or deferred residents. Candidate construction
    commits only complete pixels; the image owns its world. */
class ToolboxBlurPixels
{
public:
  ToolboxBlurPixels();
  ~ToolboxBlurPixels();
  static bool eligible(GrafPtr window, const Rect &rect);
  bool hasPixels() const
  {
    return this->image_ != 0;
  }
  bool capture(GrafPtr window, const Rect &rect, int cell);
  bool paint(GrafPtr window, const Rect &rect);
  void clear();

private:
  friend class ToolboxBlurSnapshotInspection;
  struct Image;
  Image *image_;
  /** Last capture stages and copy-back, in Microseconds; private diagnostics. */
  unsigned long timings_[4];
  ToolboxBlurPixels(const ToolboxBlurPixels &);
  ToolboxBlurPixels &operator=(const ToolboxBlurPixels &);
};

enum ToolboxBlurKind
{
  TOOLBOX_BLUR_FROZEN,
  TOOLBOX_BLUR_LIVE
};
enum ToolboxBlurPaint
{
  BLUR_PICTURE,
  BLUR_ABSENT,
  BLUR_GROUND
};

/** One controller-owned picture; kind fixes selection and capture policy.
    The key is borrowed identity only and is never dereferenced. */
template <ToolboxBlurKind K> class ToolboxBlurSnapshot
{
public:
  enum Phase
  {
    EMPTY,
    READY,
    DISABLED
  };
  ToolboxBlurSnapshot()
      : stack_(0),
        rect_(),
        capture_(CAPTURE_ALLOWED)
  {
  }
  void clear()
  {
    this->pixels_.clear();
    this->stack_ = 0;
    this->capture_ = CAPTURE_ALLOWED;
  }
  /** Once after layout; O(1), with a row selected by the ledger owner. */
  void reconcile(const ToolboxUnitLedger::FirstBlurUnits &selection)
  {
    const ToolboxUnitLedger::UnitSeat *row = K == TOOLBOX_BLUR_FROZEN ? selection.staticBlur : selection.band;
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
    explicit LayoutScope(ToolboxBlurSnapshot &owner)
        : pending_(&owner)
    {
    }
    ~LayoutScope()
    {
      if (this->pending_)
        this->pending_->clear();
    }
    void commit(const ToolboxUnitLedger::FirstBlurUnits &selection)
    {
      this->pending_->reconcile(selection);
      this->pending_ = 0;
    }

  private:
    ToolboxBlurSnapshot *pending_;
    LayoutScope(const LayoutScope &);
    LayoutScope &operator=(const LayoutScope &);
  };
  /** Once per first-unit paint, after ground and unit-scope admission. O(w*h)
      plus grid work only for this owner's capture; FROZEN READY never recaptures. */
  ToolboxBlurPaint paint(GrafPtr window, const ToolboxUnitLedger::UnitSeat &row, const ToolboxErasedPass &pass)
  {
    if (!this->matches(row))
      return BLUR_ABSENT;
    if (!ToolboxBlurPixels::eligible(window, row.rect))
      return BLUR_GROUND;
    if (this->capture_ != CAPTURE_DISABLED && (K == TOOLBOX_BLUR_LIVE || !this->pixels_.hasPixels())
        && pass.admitsCapture(row.rect))
    {
      if (!this->pixels_.capture(window, row.rect, K == TOOLBOX_BLUR_FROZEN ? 8 : 4))
        this->capture_ = CAPTURE_DISABLED;
    }
    if (this->pixels_.hasPixels())
      return this->pixels_.paint(window, row.rect) ? BLUR_PICTURE : BLUR_GROUND;
    return BLUR_ABSENT;
  }

  /** Once per dirty delivery; no ledger walk. */
  bool queryKeyRect(Rect &out) const
  {
    if (!this->stack_)
      return false;
    out = this->rect_;
    return true;
  }

private:
  friend class ToolboxBlurSnapshotInspection;
  bool matches(const ToolboxUnitLedger::UnitSeat &row) const
  {
    return this->stack_ && this->stack_ == row.stack && row.index == 1 && EqualRect(&this->rect_, &row.rect);
  }
  loka::app::ZStackNode *stack_;
  Rect rect_;
  enum CaptureDisposition
  {
    CAPTURE_ALLOWED,
    CAPTURE_DISABLED
  };
  Phase phase() const
  {
    return this->pixels_.hasPixels() ? READY : (this->capture_ == CAPTURE_DISABLED ? DISABLED : EMPTY);
  }
  CaptureDisposition capture_;
  ToolboxBlurPixels pixels_;
  ToolboxBlurSnapshot(const ToolboxBlurSnapshot &);
  ToolboxBlurSnapshot &operator=(const ToolboxBlurSnapshot &);
};
#endif
