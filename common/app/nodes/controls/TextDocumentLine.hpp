#ifndef LOKA_APP_TEXT_DOCUMENT_LINE_HPP
#define LOKA_APP_TEXT_DOCUMENT_LINE_HPP

#include "app/nodes/controls/TextEditor.hpp"
#include "platform/String.hpp"
#include "core/StringAccess.hpp"

namespace loka
{
  namespace app
  {
    namespace text_document_detail
    {
      /** Checked size addition; refusal leaves the output untouched. */
      inline bool ExpandedSize(std::size_t bytes, std::size_t separators, std::size_t &out)
      {
        if (separators > static_cast<std::size_t>(-1) - bytes)
          return false;
        out = bytes + separators;
        return true;
      }
      /** Raw paste may contain one extra CR byte for every normalized separator. */
      inline bool PasteFits(std::size_t length, const TextCapacity &capacity)
      {
        return length <= capacity.bytes || length - capacity.bytes <= capacity.bytes;
      }
      /** The editor alphabet is all nonzero ASCII except row separators. */
      inline EditorResult ValidateRow(const char *bytes, std::size_t length, const TextCapacity &capacity)
      {
        if (length > capacity.bytes)
          return EDITOR_CAPACITY;
        for (std::size_t i = 0; i < length; ++i)
        {
          const unsigned char c = static_cast<unsigned char>(bytes[i]);
          if (!c || c > 127 || c == '\r' || c == '\n')
            return EDITOR_NON_ASCII;
        }
        return EDITOR_OK;
      }
      /** Borrow the rail's UTF-8 bytes when available. Other rails retain the
          existing StringBuffer conversion/refusal contract, as AttributedString
          does. String construction retains the rail contract (#827). */
      class LineBytes
      {
      public:
        LineBytes(const core::String &value, const TextCapacity &capacity)
            : buffer_(),
              view_(),
              result_(EDITOR_OK)
        {
          const core::Managed<platform::String> &handle = core::StringAccess::handle(value);
          if (!handle.isValid())
          {
            this->view_.bytes = "";
            this->view_.length = 0;
            return;
          }
          if (!handle->queryUtf8(this->view_))
          {
            this->buffer_ = value.bufferWithEncoding(core::StringEncodingUtf8);
            if (!this->buffer_.platformHandle().isValid())
            {
              this->result_ = EDITOR_ALLOCATION;
              return;
            }
            this->view_.bytes = static_cast<const char *>(this->buffer_.data());
            this->view_.length = this->buffer_.length();
          }
          if (!this->view_.length)
            this->view_.bytes = "";
          this->result_ = ValidateRow(this->view_.bytes, this->view_.length, capacity);
        }
        EditorResult result() const
        {
          return this->result_;
        }
        const char *data() const
        {
          return this->view_.bytes;
        }
        std::size_t size() const
        {
          return this->view_.length;
        }

      private:
        LineBytes(const LineBytes &);
        LineBytes &operator=(const LineBytes &);
        core::StringBuffer buffer_;
        platform::Utf8View view_;
        EditorResult result_;
      };
      /** Shared editor/file measurement, excluding attachment and cursor ownership. */
      inline EditorResult Measure(const TextLines &lines, std::size_t &bytes)
      {
        bytes = 0;
        if (lines.size() > lines.capacity().lines)
          return EDITOR_CAPACITY;
        for (unsigned short i = 0; i < lines.size(); ++i)
        {
          const LineBytes line(lines.at(i).value, lines.capacity());
          if (line.result() != EDITOR_OK)
            return line.result();
          const std::size_t separator = i ? 1 : 0;
          if (separator > lines.capacity().bytes - bytes || line.size() > lines.capacity().bytes - bytes - separator)
            return EDITOR_CAPACITY;
          bytes += line.size() + separator;
        }
        return EDITOR_OK;
      }
    } // namespace text_document_detail
  } // namespace app
} // namespace loka
#endif
