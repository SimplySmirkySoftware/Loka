#ifndef LOKA_TOOLBOX_TEXTENSION_FAKE_HPP
#define LOKA_TOOLBOX_TEXTENSION_FAKE_HPP
#include "unicode/MacTextEditorSubset.h"
#include <string>
#include <vector>
namespace TextensionFake
{
  struct Draw
  {
    TXNObject object;
    int updateDepth;
  };
  struct Creation
  {
    WindowRef window;
    Rect frame;
    TXNFrameOptions options;
  };
  struct Control
  {
    TXNControlTag tag;
    UInt32 value;
  };
  const std::vector<Creation> &creations();
  const std::vector<Control> &controls();
  void reset();
  void failNext(const char *name, OSStatus status);
  void failControl(unsigned ordinal, OSStatus status);
  const std::vector<std::string> &calls();
  const std::vector<Draw> &draws();
  unsigned live();
  unsigned deletes();
  unsigned inits();
  unsigned terminates();
  void beginUpdate();
  void endUpdate();
} // namespace TextensionFake
#endif
