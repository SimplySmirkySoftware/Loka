#include "ToolboxOpenURL.hpp"
#include "ToolboxInternetConfigDispatch.hpp"
#include <cstddef>
#include "support/TestVerify.hpp"
#include <cstdarg>
#include <cstring>
#include <string>

namespace
{
  std::string trace;
  int failure;
  const char *expectedURL;
  ComponentInstance instance = reinterpret_cast<ComponentInstance>(0x1234);
  void note(const char *stage)
  {
    if (!trace.empty())
      trace += ' ';
    trace += stage;
  }
  bool cm()
  {
    note("cm");
    return failure != 1;
  }
  long count()
  {
    note("count");
    return failure == 2 ? 0 : 1;
  }
  ComponentInstance open()
  {
    note("open");
    return failure == 3 ? 0 : instance;
  }
  OSType creator()
  {
    note("creator");
    return 0x4C6F6B61;
  }
  OSStatus start(ComponentInstance inst, OSType code)
  {
    note("start");
    LOKA_VERIFY(inst == instance && code == 0x4C6F6B61);
    return failure == 4 ? -1 : 0;
  }
  OSStatus find(ComponentInstance inst)
  {
    note("find");
    LOKA_VERIFY(inst == instance);
    return failure == 5 ? -1 : 0;
  }
  OSStatus launch(ComponentInstance inst, ConstStr255Param hint, Ptr data, long len, long *first, long *last)
  {
    note("launch");
    LOKA_VERIFY(inst == instance && hint[0] == 0);
    LOKA_VERIFY(len == static_cast<long>(std::strlen(expectedURL)));
    LOKA_VERIFY(std::memcmp(data, expectedURL, len) == 0 && *first == 0 && *last == len);
    data[0] = 'X'; // The native writable buffer must not alias the WebURL.
    return failure == 6 ? -1 : 0;
  }
  OSStatus stop(ComponentInstance inst)
  {
    note("stop");
    LOKA_VERIFY(inst == instance);
    return -2;
  }
  OSErr close(ComponentInstance inst)
  {
    note("close");
    LOKA_VERIFY(inst == instance);
    return -3;
  }
  const ToolboxInternetConfigCalls calls = {cm, count, open, creator, start, find, launch, stop, close};
  void outcomes()
  {
    trace.clear();
    failure = 0;
    LOKA_VERIFY(ToolboxOpenWebURL(loka::app::WebURL::None(), calls) == loka::app::URL_NOT_OPENED);
    LOKA_VERIFY(trace.empty());
    const char *logs[] = {"cm count open creator start find launch stop close",
                          "cm",
                          "cm count",
                          "cm count open",
                          "cm count open creator start close",
                          "cm count open creator start find stop close",
                          "cm count open creator start find launch stop close"};
    const char *urls[] = {"http://example.com/", "https://example.com/?a=b#fragment", "mailto:a@example.com"};
    for (unsigned u = 0; u < 3; ++u)
    {
      expectedURL = urls[u];
      loka::app::WebURL url;
      LOKA_VERIFY(loka::app::WebURL::parse(loka::core::String(expectedURL), url));
      for (failure = 0; failure < 7; ++failure)
      {
        trace.clear();
        const loka::app::URLOpenKind expected = failure == 0   ? loka::app::URL_OPENED
                                                : failure <= 2 ? loka::app::URL_UNAVAILABLE
                                                               : loka::app::URL_NOT_OPENED;
        LOKA_VERIFY(ToolboxOpenWebURL(url, calls) == expected);
        LOKA_VERIFY(trace == logs[failure]);
        LOKA_VERIFY(url.text().equals(loka::core::String(expectedURL)));
      }
    }
  }
  unsigned selector;
  unsigned char hint[] = {0};
  char data[] = "https://example.com/";
  long first = 0, last = 20;
  void upp() {}
  long record(UniversalProcPtr proc, ProcInfoType info, ...)
  {
    const unsigned long descriptors[] = {0x00040000, 0x00060002, 0x00140011, 0x00000001};
    LOKA_VERIFY(proc == &upp && info == 0x000000F0);
    va_list args;
    va_start(args, info);
    const unsigned char *block = va_arg(args, const unsigned char *);
    va_end(args);
    // Descriptor first: flags, parameter bytes, selector.
    LOKA_VERIFY(block[0] == 0 && block[1] == (descriptors[selector] >> 16));
    short what;
    std::memcpy(&what, block + 2, sizeof(what));
    LOKA_VERIFY(what == static_cast<short>(descriptors[selector] & 0xFFFF));
    // Parameters in reverse declaration order, instance last (68K stack image).
    if (selector == 0)
    {
      const ToolboxICStartParams *p = reinterpret_cast<const ToolboxICStartParams *>(block);
      LOKA_VERIFY(p->creator == 0x4C6F6B61 && p->instance == instance);
      LOKA_VERIFY(offsetof(ToolboxICStartParams, creator) < offsetof(ToolboxICStartParams, instance));
    }
    if (selector == 1)
    {
      const ToolboxICFindParams *p = reinterpret_cast<const ToolboxICFindParams *>(block);
      LOKA_VERIFY(p->folders == 0 && p->count == 0 && p->instance == instance);
      LOKA_VERIFY(offsetof(ToolboxICFindParams, folders) < offsetof(ToolboxICFindParams, count));
      LOKA_VERIFY(offsetof(ToolboxICFindParams, count) < offsetof(ToolboxICFindParams, instance));
    }
    if (selector == 2)
    {
      const ToolboxICLaunchParams *p = reinterpret_cast<const ToolboxICLaunchParams *>(block);
      LOKA_VERIFY(p->selEnd == &last && p->selStart == &first && p->length == 20);
      LOKA_VERIFY(p->data == data && p->hint == hint && p->instance == instance);
      LOKA_VERIFY(offsetof(ToolboxICLaunchParams, selEnd) < offsetof(ToolboxICLaunchParams, selStart));
      LOKA_VERIFY(offsetof(ToolboxICLaunchParams, selStart) < offsetof(ToolboxICLaunchParams, length));
      LOKA_VERIFY(offsetof(ToolboxICLaunchParams, length) < offsetof(ToolboxICLaunchParams, data));
      LOKA_VERIFY(offsetof(ToolboxICLaunchParams, data) < offsetof(ToolboxICLaunchParams, hint));
      LOKA_VERIFY(offsetof(ToolboxICLaunchParams, hint) < offsetof(ToolboxICLaunchParams, instance));
    }
    if (selector == 3)
    {
      const ToolboxICStopParams *p = reinterpret_cast<const ToolboxICStopParams *>(block);
      LOKA_VERIFY(p->instance == instance);
    }
    return -123;
  }
  void dispatch()
  {
    selector = 0;
    LOKA_VERIFY(ToolboxICDispatchStart(record, upp, instance, 0x4C6F6B61) == -123);
    selector = 1;
    LOKA_VERIFY(ToolboxICDispatchFind(record, upp, instance) == -123);
    selector = 2;
    LOKA_VERIFY(ToolboxICDispatchLaunch(record, upp, instance, hint, data, 20, &first, &last) == -123);
    selector = 3;
    LOKA_VERIFY(ToolboxICDispatchStop(record, upp, instance) == -123);
  }
  void words()
  {
    const unsigned short actual[][5] = {{LOKA_IC_WORDS(LOKA_IC_START_DESCRIPTOR)},
                                        {LOKA_IC_WORDS(LOKA_IC_FIND_DESCRIPTOR)},
                                        {LOKA_IC_WORDS(LOKA_IC_LAUNCH_DESCRIPTOR)},
                                        {LOKA_IC_WORDS(LOKA_IC_STOP_DESCRIPTOR)}};
    const unsigned short expected[][5] = {{0x2F3C, 0x0004, 0x0000, 0x7000, 0xA82A},
                                          {0x2F3C, 0x0006, 0x0002, 0x7000, 0xA82A},
                                          {0x2F3C, 0x0014, 0x0011, 0x7000, 0xA82A},
                                          {0x2F3C, 0x0000, 0x0001, 0x7000, 0xA82A}};
    LOKA_VERIFY(std::memcmp(actual, expected, sizeof(actual)) == 0);
  }
} // namespace
int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 2);
  if (!std::strcmp(argv[1], "outcomes"))
    outcomes();
  else if (!std::strcmp(argv[1], "dispatch"))
    dispatch();
  else if (!std::strcmp(argv[1], "words"))
    words();
  else
    return 2;
  return 0;
}
