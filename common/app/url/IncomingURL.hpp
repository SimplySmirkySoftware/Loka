#ifndef LOKA_APP_URL_INCOMING_URL_HPP
#define LOKA_APP_URL_INCOMING_URL_HPP

#include "app/url/URLLexical.hpp"
#include "app/url/URLScheme.hpp"

namespace loka
{
  namespace app
  {
    /**
     * An immutable received URL with checked lexical bytes and a matching scheme.
     * Received text is untrusted input: this does not promise sender identity,
     * the meaning of %2F, .. or double encoding, or that the target exists.
     * The application decides how to interpret and validate the rest.
     */
    class IncomingURL
    {
    public:
      enum
      {
        MAX_TEXT_BYTES = internal::URL_MAX_TEXT_BYTES
      };

      /** Constructs none. */
      IncomingURL() {}
      /** Returns a value with no URL text. */
      static IncomingURL None()
      {
        return IncomingURL();
      }
      /** Refuses without changing out; only ASCII scheme letters are lowercased. */
      static bool parse(const core::String &text, const URLScheme &scheme, IncomingURL &out);
      /** True when no URL has been parsed. */
      bool isNone() const
      {
        return this->text_.empty();
      }
      /** Complete text, or empty for none. */
      const core::String &text() const
      {
        return this->text_;
      }
      /** Builds the bytes after the first colon on each call; empty for none. */
      core::String rest() const;
      /** Case-sensitive byte prefix; empty matches a value, null and none never match. */
      bool restStartsWith(const char *prefix) const;
      /** Compares complete normalized text. */
      bool operator==(const IncomingURL &other) const
      {
        return this->text_.equals(other.text_);
      }
      /** Compares complete normalized text. */
      bool operator!=(const IncomingURL &other) const
      {
        return !(*this == other);
      }

    private:
      core::String text_;
    };
  } // namespace app
} // namespace loka

#endif // LOKA_APP_URL_INCOMING_URL_HPP
