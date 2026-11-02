#ifndef LOKA_TOOLBOX_FULL_DRAW_PRESENTER_HPP
#define LOKA_TOOLBOX_FULL_DRAW_PRESENTER_HPP

#include <Quickdraw.h>
#include "ToolboxDrawMeasurement.hpp"
#include "ToolboxErasedPass.hpp"

/** Borrowed for one synchronous full render. Called after layout commits and
    extent publication, before paint; also called when render admission fails. */
class ToolboxFullDrawPresenter
{
public:
  virtual ~ToolboxFullDrawPresenter() {}
  virtual void eraseBeforePaint() = 0;
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
  explicit ToolboxPortErasePresenter(const Rect &port)
      : port_(port),
        erased_(0)
  {
  }
  /** Borrow only for the synchronous render inside the guard's scope. */
  void setErasedGuard(ToolboxErasedPass::Guard &erased)
  {
    this->erased_ = &erased;
  }
  virtual void eraseBeforePaint()
  {
#ifdef TEST_BUILD
    this->beginEraseForTesting();
#endif
    EraseRect(&this->port_);
    if (this->erased_)
      this->erased_->noteErased(this->erased_->shown());
  }

private:
  const Rect &port_;
  ToolboxErasedPass::Guard *erased_;
};

#endif
