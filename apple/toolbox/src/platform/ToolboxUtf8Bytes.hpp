#ifndef LOKA_TOOLBOX_UTF8_BYTES_HPP
#define LOKA_TOOLBOX_UTF8_BYTES_HPP

#include "core/String.hpp"
#include "platform/String.hpp"

/** Acquires UTF-8 once: borrows when possible, otherwise owns a scratch copy.
    A handle-less String is valid and empty. A refused copy, including partial
    appends, exposes null bytes and zero length. Valid values have non-null bytes.
    The view is valid only while both the source String and this value live:
    take a named local String first when reading a prop. This value cannot copy. */
class ToolboxUtf8Bytes
{
public:
  explicit ToolboxUtf8Bytes(const loka::core::String &source);
#if __cplusplus >= 201103L
  ToolboxUtf8Bytes(const loka::core::String &&) = delete;
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
  ToolboxUtf8Bytes(const ToolboxUtf8Bytes &);
  ToolboxUtf8Bytes &operator=(const ToolboxUtf8Bytes &);
  loka::platform::Utf8View view_;
  std::string scratch_;
};

#endif
