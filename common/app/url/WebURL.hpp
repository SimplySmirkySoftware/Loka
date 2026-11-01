#ifndef LOKA_APP_URL_WEB_URL_HPP
#define LOKA_APP_URL_WEB_URL_HPP

#include "core/String.hpp"

namespace loka
{
  namespace app
  {
    /**
     * An immutable openable string: http, https or mailto with RFC 3986 bytes.
     * Loka binds the string, leaving the handler, whether it comes forward, and
     * when it finishes to the OS. 2048 bytes is Loka's storage limit, not an OS promise.
     */
    class WebURL
    {
    public:
      enum
      {
        MAX_TEXT_BYTES = 2048
      };

      WebURL() {}
      static WebURL None()
      {
        return WebURL();
      }
      /** Refuses invalid text without changing out; only the scheme is lowercased. */
      static bool parse(const core::String &text, WebURL &out);
      bool isNone() const
      {
        return this->text_.empty();
      }
      const core::String &text() const
      {
        return this->text_;
      }
      bool operator==(const WebURL &other) const
      {
        return this->text_.equals(other.text_);
      }
      bool operator!=(const WebURL &other) const
      {
        return !(*this == other);
      }

    private:
      core::String text_;
    };
  } // namespace app
} // namespace loka

#endif // LOKA_APP_URL_WEB_URL_HPP
