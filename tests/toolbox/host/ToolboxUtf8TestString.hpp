#ifndef LOKA_TOOLBOX_UTF8_TEST_STRING_HPP
#define LOKA_TOOLBOX_UTF8_TEST_STRING_HPP

#include "platform/String.hpp"

/** Test-owned source with independently selectable lending and copy refusal. */
class ToolboxUtf8TestString : public loka::platform::String
{
public:
  ToolboxUtf8TestString(const std::string &bytes, bool lends, bool refuses, unsigned &appends)
      : bytes_(bytes),
        lends_(lends),
        refuses_(refuses),
        appends_(appends)
  {
  }
  virtual bool queryUtf8(loka::platform::Utf8View &out) const
  {
    if (!this->lends_)
      return false;
    out.bytes = this->bytes_.data();
    out.length = this->bytes_.size();
    return true;
  }
  virtual bool appendUtf8(std::string &out) const
  {
    ++this->appends_;
    out += this->bytes_;
    return !this->refuses_;
  }

private:
  const std::string bytes_;
  const bool lends_;
  const bool refuses_;
  unsigned &appends_;
};

#endif
