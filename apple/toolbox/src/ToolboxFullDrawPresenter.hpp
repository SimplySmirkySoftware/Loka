#ifndef LOKA_TOOLBOX_FULL_DRAW_PRESENTER_HPP
#define LOKA_TOOLBOX_FULL_DRAW_PRESENTER_HPP

#include <Quickdraw.h>
#include "ToolboxDrawMeasurement.hpp"
#include "ToolboxErasedPass.hpp"
#include "ToolboxPresentObservation.hpp"
#include "ToolboxWindowGeometry.hpp"

/** Borrowed for one synchronous full render. Called after layout commits and
    extent publication, before paint; also called when render admission fails. */
class ToolboxFullDrawPresenter
{
public:
  virtual ~ToolboxFullDrawPresenter() {}
  virtual ToolboxPresentExecuted eraseBeforePaint(const ToolboxPresentPlan &) = 0;
#ifdef TEST_BUILD
  void beginLayoutForTesting()
  {
    this->measurement_.beginLayout();
  }
  void endLayoutForTesting()
  {
    this->measurement_.endLayout();
  }
  void finishDrawForTesting()
  {
    this->measurement_.finish();
  }

protected:
  void beginEraseForTesting()
  {
    this->measurement_.beginErase();
  }

private:
  loka::testing::WholeDrawBracket measurement_;
#endif
};

/** The full-draw window owns the port and paint clip for this stack borrow. */
class ToolboxPortErasePresenter : public ToolboxFullDrawPresenter
{
public:
  enum CopyAdmission
  {
    COPY_ALLOWED,
    COPY_REFUSED
  };
  explicit ToolboxPortErasePresenter(const Rect &port, CopyAdmission admission = COPY_REFUSED)
      : port_(port),
        admission_(admission),
        erased_(0),
        repair_(0),
        exposed_(0),
        scratch_(0)
  {
  }
  ~ToolboxPortErasePresenter()
  {
    if (this->repair_)
      DisposeRgn(this->repair_);
    if (this->exposed_)
      DisposeRgn(this->exposed_);
    if (this->scratch_)
      DisposeRgn(this->scratch_);
  }
  void setErasedGuard(ToolboxErasedPass::Guard &erased)
  {
    this->erased_ = &erased;
  }
  /** Once per full draw, bounded unions over plan values; never visits controller rows. */
  virtual ToolboxPresentExecuted eraseBeforePaint(const ToolboxPresentPlan &plan)
  {
#ifdef TEST_BUILD
    this->beginEraseForTesting();
#endif
    if (plan.verdict != TOOLBOX_PRESENT_COPY || this->admission_ == COPY_REFUSED || !this->erased_
        || !ToolboxErasedEvidence::contains(this->erased_->shown(), this->port_))
      return this->erase();
    this->repair_ = NewRgn();
    if (!ToolboxRegionOK(this->repair_))
      return this->erase();
    this->exposed_ = NewRgn();
    if (!ToolboxRegionOK(this->exposed_))
      return this->erase();
    this->scratch_ = NewRgn();
    if (!ToolboxRegionOK(this->scratch_))
      return this->erase();
    GrafPtr port;
    GetPort(&port);
    if (!port || !ToolboxRegionOK(port->visRgn))
      return this->erase();
    RectRgn(this->scratch_, &plan.copyDomain);
    if (!ToolboxRegionOK(this->scratch_))
      return this->erase();
    DiffRgn(this->scratch_, port->visRgn, this->scratch_);
    if (!ToolboxRegionOK(this->scratch_))
      return this->erase();
    const bool visible = EmptyRgn(this->scratch_);
    if (!ToolboxRegionOK(this->scratch_) || !visible)
      return this->erase();
    for (unsigned i = 0; i < plan.repair.count(); ++i)
      if (!this->addPair(plan.repair.unit(i), plan.dv))
        return this->erase();
    for (unsigned i = 0; i < plan.sealedRects.count(); ++i)
      if (!this->addPair(plan.sealedRects.at(i), plan.dv))
        return this->erase();
    if (!this->addPair(plan.damage, plan.dv) || !this->add(plan.repair.bar())
        || !this->addPair(ToolboxWindowSizeBoxRect(this->port_), plan.dv))
      return this->erase();
    ScrollRect(&plan.copyDomain, 0, static_cast<short>(plan.dv), this->exposed_);
    if (!ToolboxRegionOK(this->exposed_))
      return this->recover();
    UnionRgn(this->repair_, this->exposed_, this->repair_);
    if (!ToolboxRegionOK(this->repair_))
      return this->recover();
    SetClip(this->repair_);
    if (!ToolboxRegionOK(this->repair_))
      return this->recover();
    GetClip(this->scratch_);
    if (!ToolboxRegionOK(this->scratch_) || (**this->scratch_).rgnSize != (**this->repair_).rgnSize
        || !EqualRect(&(**this->scratch_).rgnBBox, &(**this->repair_).rgnBBox))
      return this->recover();
    EraseRect(&this->port_);
    if (this->erased_)
      this->erased_->noteErased(this->repair_, this->scratch_);
    return TOOLBOX_PRESENT_COPIED;
  }
private:
  ToolboxPresentExecuted erase()
  {
    EraseRect(&this->port_);
    if (this->erased_)
      this->erased_->noteErased(this->erased_->shown());
    return TOOLBOX_PRESENT_ERASED;
  }
  ToolboxPresentExecuted recover()
  {
    ClipRect(&this->port_);
    if (!ToolboxRegionOK(this->scratch_))
      return TOOLBOX_PRESENT_COPY_FAILED;
    GetClip(this->scratch_);
    if (!ToolboxRegionOK(this->scratch_) || (**this->scratch_).rgnSize != 10
        || !EqualRect(&(**this->scratch_).rgnBBox, &this->port_))
      return TOOLBOX_PRESENT_COPY_FAILED;
    return this->erase();
  }
  bool add(const Rect &rect)
  {
    if (rect.left >= rect.right || rect.top >= rect.bottom)
      return true;
    Rect visible;
    if (!SectRect(&rect, &this->port_, &visible))
      return true;
    RectRgn(this->scratch_, &visible);
    if (!ToolboxRegionOK(this->scratch_))
      return false;
    UnionRgn(this->repair_, this->scratch_, this->repair_);
    return ToolboxRegionOK(this->repair_);
  }
  bool addPair(const Rect &rect, int dv)
  {
    if (rect.left >= rect.right || rect.top >= rect.bottom)
      return true;
    if (!this->add(rect))
      return false;
    Rect shifted = rect;
    OffsetRect(&shifted, 0, static_cast<short>(dv));
    return this->add(shifted);
  }
  const Rect &port_;
  const CopyAdmission admission_;
  ToolboxErasedPass::Guard *erased_;
  RgnHandle repair_, exposed_, scratch_;
  ToolboxPortErasePresenter(const ToolboxPortErasePresenter &);
  ToolboxPortErasePresenter &operator=(const ToolboxPortErasePresenter &);
};
#endif
