#ifndef LOKA_TOOLBOX_TEXTENSION_FAKE_HPP
#define LOKA_TOOLBOX_TEXTENSION_FAKE_HPP
#include "unicode/MacTextEditorSubset.h"
#include <string>
#include <vector>
namespace TextensionFake
{
  struct Call
  {
    std::string name;
    TXNObject object;
    long argument;
    Rect frame;
  };
  const std::vector<Call> &events();
  void clearCalls();
  void service(bool consumed, bool rewrite);

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
  TXNObject latest();
  const std::vector<unsigned short> &body(TXNObject);
  void nextKey(const std::vector<unsigned short> &, bool replaceSelection = true);
  void nextClick(TXNOffset, TXNOffset);
  void nextCaret(TXNOffset, TXNOffset);
  void injectChange(const std::vector<unsigned short> &);
  void publicText(const std::vector<unsigned short> &);
  void lieSize(long);
  void lieHandle(long);
  void reset();
  void failNth(const char *, unsigned, OSStatus);
  extern void (*afterKey)();
  extern void (*afterService)();
  extern void (*afterIdle)();
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
