// Deliberate platform twin of apple/toolbox/src/platform/ToolboxUtf8Bytes; do not link Toolbox.
#ifndef LOKA_WEB_UTF8_BYTES_HPP
#define LOKA_WEB_UTF8_BYTES_HPP

#include "core/String.hpp"
#include "platform/String.hpp"

/** Acquires UTF-8 once: borrows when possible, otherwise owns a scratch copy.
    A handle-less String is valid and empty. A refused copy, including partial
    appends, exposes null bytes and zero length. Valid values have non-null bytes.
    The view is valid only while both the source String and this value live:
    take a named local String first when reading a prop. This value cannot copy.
    Cost: build/comparison/query once; borrows one String or copies its own scratch bytes. */
class WebUtf8Bytes
{
public:
  explicit WebUtf8Bytes(const loka::core::String &source);
#if __cplusplus >= 201103L
  WebUtf8Bytes(const loka::core::String &&) = delete;
#endif
  bool valid() const
  {
    return this->bytes() != 0;
  }
  const char *bytes() const
  {
    return this->view_.bytes;
  }
  std::size_t length() const
  {
    return this->view_.length;
  }

private:
  WebUtf8Bytes(const WebUtf8Bytes &);
  WebUtf8Bytes &operator=(const WebUtf8Bytes &);
  loka::platform::Utf8View view_;
  std::string scratch_;
};

#endif
