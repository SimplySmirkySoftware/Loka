#ifndef LOKA_STANDALONE_SCREEN_DEPTH_HPP
#define LOKA_STANDALONE_SCREEN_DEPTH_HPP

#include <Errors.h>
#include <Palettes.h>
#include <Quickdraw.h>

/** Owns one main-device depth request made after QuickDraw initialization.
    Explicit restore reports the result before a verdict; destruction covers early exits. */
class StandaloneScreenDepth
{
public:
  StandaloneScreenDepth()
      : originalDepth_(0),
        accepted_(false)
  {
  }
  ~StandaloneScreenDepth()
  {
    this->restore();
  }

  OSErr request(short depth)
  {
    // One request per owner: a second call must not record the requested
    // depth as the original one.
    if (this->accepted_)
      return noErr;
    this->originalDepth_ = (**(**GetMainDevice()).gdPMap).pixelSize;
    if (!HasDepth(GetMainDevice(), depth, 1, 1))
      return paramErr;
    const OSErr result = SetDepth(GetMainDevice(), depth, 1, 1);
    this->accepted_ = result == noErr;
    return result;
  }
  OSErr restore()
  {
    if (!this->accepted_)
      return noErr;
    this->accepted_ = false;
    return SetDepth(GetMainDevice(), this->originalDepth_, 1, 1);
  }
  short originalDepth() const
  {
    return this->originalDepth_;
  }
  bool accepted() const
  {
    return this->accepted_;
  }

private:
  StandaloneScreenDepth(const StandaloneScreenDepth &);
  StandaloneScreenDepth &operator=(const StandaloneScreenDepth &);
  short originalDepth_;
  bool accepted_;
};

#endif
