#include "ToolboxOpenURL.hpp"
#include <vector>

namespace
{
  /** Returns only acquired resources, in reverse order, on every exit. */
  class ToolboxInternetConfigSession
  {
    const ToolboxInternetConfigCalls &calls_;
    ComponentInstance instance_;
    enum Held
    {
      HELD_OPEN,
      HELD_STARTED
    } held_;
    ToolboxInternetConfigSession(const ToolboxInternetConfigSession &);
    ToolboxInternetConfigSession &operator=(const ToolboxInternetConfigSession &);

  public:
    explicit ToolboxInternetConfigSession(const ToolboxInternetConfigCalls &calls)
        : calls_(calls),
          instance_(0),
          held_(HELD_OPEN)
    {
    }
    ~ToolboxInternetConfigSession()
    {
      if (this->instance_)
      {
        if (this->held_ == HELD_STARTED)
          this->calls_.stop(this->instance_);
        this->calls_.close(this->instance_);
      }
    }
    bool open()
    {
      this->instance_ = this->calls_.openLauncher();
      return this->instance_ != 0;
    }
    bool start()
    {
      if (this->calls_.start(this->instance_, this->calls_.currentCreator()) != noErr)
        return false;
      this->held_ = HELD_STARTED;
      return true;
    }
    ComponentInstance instance() const
    {
      return this->instance_;
    }
  };
} // namespace
loka::app::URLOpenKind ToolboxOpenWebURL(const loka::app::WebURL &url, const ToolboxInternetConfigCalls &calls)
{
  // Direct platform callers can pass the default WebURL; no native buffer exists.
  if (url.isNone())
    return loka::app::URL_NOT_OPENED;
  if (!calls.hasComponentManager() || calls.countLaunchers() == 0)
    return loka::app::URL_UNAVAILABLE;
  ToolboxInternetConfigSession session(calls);
  if (!session.open() || !session.start() || calls.findConfigFile(session.instance()) != noErr)
    return loka::app::URL_NOT_OPENED;
  const loka::core::StringBuffer source = url.text().bufferWithEncoding(loka::core::StringEncodingUtf8);
  const char *data = static_cast<const char *>(source.data());
  std::vector<char> bytes(data, data + source.length());
  const unsigned char hint[] = {0};
  const long length = static_cast<long>(bytes.size());
  long start = 0;
  long end = length;
  const OSStatus status = calls.launch(session.instance(), hint, &bytes[0], length, &start, &end);
  return status == noErr ? loka::app::URL_OPENED : loka::app::URL_NOT_OPENED;
}
