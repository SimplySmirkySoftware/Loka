#ifndef LOKA_TOOLBOX_ERASED_PASS_HPP
#define LOKA_TOOLBOX_ERASED_PASS_HPP
#include <cassert>

/** Positive coverage fact: this walk's current clip was entirely erased
    immediately before the walk. Only the two erase-and-walk sites open it. */
class ToolboxErasedPass
{
public:
  ToolboxErasedPass()
      : open_(false)
  {
  }
  bool isOpen() const
  {
    return this->open_;
  }

  /** Stack lifetime brackets one non-nesting render walk. A refused nested
      guard leaves the outer fact intact, including in release builds. */
  class Guard
  {
  public:
    explicit Guard(ToolboxErasedPass &pass)
        : owned_(pass.open_ ? 0 : &pass)
    {
      assert(!pass.open_);
      if (this->owned_)
        this->owned_->open_ = true;
    }
    ~Guard()
    {
      if (this->owned_)
        this->owned_->open_ = false;
    }

  private:
    ToolboxErasedPass *owned_;
    Guard(const Guard &);
    Guard &operator=(const Guard &);
  };

private:
  bool open_;
  ToolboxErasedPass(const ToolboxErasedPass &);
  ToolboxErasedPass &operator=(const ToolboxErasedPass &);
};
#endif
