#include "app/style/LinkTarget.hpp"
#include "app/style/AttributedTextRead.hpp"
#include "core/StringAccess.hpp"

namespace loka
{
  namespace app
  {
    namespace style_internal
    {
      TextReadKind ReadAttributedText(const core::String &text, core::StringBuffer &scratch, platform::Utf8View &out)
      {
        out = platform::Utf8View();
        const core::Managed<platform::String> &handle = core::StringAccess::handle(text);
        if (!handle.isValid())
          return TEXT_EMPTY;
        if (!handle->queryUtf8(out))
        {
          scratch = text.bufferWithEncoding(core::StringEncodingUtf8);
          if (!scratch.platformHandle().isValid())
            return TEXT_REFUSED;
          out.bytes = static_cast<const char *>(scratch.data());
          out.length = scratch.length();
        }
        return out.length == 0 ? TEXT_EMPTY : TEXT_READABLE;
      }

      int CompareLinkTargets(const LinkTarget &left, const LinkTarget &right)
      {
        const core::Managed<platform::String> &leftHandle = core::StringAccess::handle(left.text());
        const core::Managed<platform::String> &rightHandle = core::StringAccess::handle(right.text());
        if (leftHandle == rightHandle)
          return 0;
        core::StringBuffer leftScratch, rightScratch;
        platform::Utf8View leftView, rightView;
        const TextReadKind leftKind = ReadAttributedText(left.text(), leftScratch, leftView);
        const TextReadKind rightKind = ReadAttributedText(right.text(), rightScratch, rightView);
        if (leftKind != rightKind)
          return leftKind < rightKind ? -1 : 1;
        if (leftKind == TEXT_EMPTY)
          return 0;
        if (leftKind == TEXT_REFUSED)
          return leftHandle.identityBefore(rightHandle) ? -1 : 1;
        const std::size_t count = leftView.length < rightView.length ? leftView.length : rightView.length;
        for (std::size_t i = 0; i < count; ++i)
        {
          const unsigned char a = static_cast<unsigned char>(leftView.bytes[i]);
          const unsigned char b = static_cast<unsigned char>(rightView.bytes[i]);
          if (a != b)
            return a < b ? -1 : 1;
        }
        if (leftView.length != rightView.length)
          return leftView.length < rightView.length ? -1 : 1;
        return 0;
      }
    } // namespace style_internal

    LinkTarget::LinkTarget()
        : text_()
    {
    }
    LinkTarget::LinkTarget(const core::String &text)
        : text_(text)
    {
    }
    const core::String &LinkTarget::text() const
    {
      return this->text_;
    }
    bool LinkTarget::isNone() const
    {
      core::StringBuffer scratch;
      platform::Utf8View view;
      return style_internal::ReadAttributedText(this->text_, scratch, view) == style_internal::TEXT_EMPTY;
    }
    bool LinkTarget::startsWith(const char *prefix) const
    {
      if (!prefix)
        return false;
      core::StringBuffer scratch;
      platform::Utf8View view;
      if (style_internal::ReadAttributedText(this->text_, scratch, view) == style_internal::TEXT_REFUSED)
        return false;
      for (std::size_t i = 0; prefix[i]; ++i)
      {
        if (i == view.length || static_cast<unsigned char>(prefix[i]) != static_cast<unsigned char>(view.bytes[i]))
          return false;
      }
      return true;
    }
    bool LinkTarget::operator==(const LinkTarget &other) const
    {
      return style_internal::CompareLinkTargets(*this, other) == 0;
    }
    bool LinkTarget::operator!=(const LinkTarget &other) const
    {
      return !(*this == other);
    }
  } // namespace app
} // namespace loka
