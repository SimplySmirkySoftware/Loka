#ifndef LOKA_TOOLBOX_FULL_DRAW_PRESENTER_HPP
#define LOKA_TOOLBOX_FULL_DRAW_PRESENTER_HPP

#include <Quickdraw.h>

/** Borrowed for one synchronous full render. Called after layout commits and
    extent publication, before paint; also called when render admission fails. */
class ToolboxFullDrawPresenter
{
public:
  virtual ~ToolboxFullDrawPresenter() {}
  virtual void eraseBeforePaint() = 0;
};

/** The full-draw window owns the port and paint clip for this stack borrow. */
class ToolboxPortErasePresenter : public ToolboxFullDrawPresenter
{
public:
  explicit ToolboxPortErasePresenter(const Rect &port)
      : port_(port)
  {
  }
  virtual void eraseBeforePaint()
  {
    EraseRect(&this->port_);
  }

private:
  const Rect &port_;
};

#endif
