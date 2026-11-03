#ifndef LOKA_APP_URL_URL_SCHEME_HPP
#define LOKA_APP_URL_URL_SCHEME_HPP

#include "core/String.hpp"
#include <cstddef>

namespace loka
{
  namespace app
  {
    class IncomingURL;

    /** An immutable RFC 3986 scheme name, stored in ASCII lowercase; none matches nothing. */
    class URLScheme
    {
    public:
      /** Constructs none. */
      URLScheme() {}
      /** Accepts ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ); refusal leaves out untouched. */
      static bool parse(const char *name, URLScheme &out);
      /** True when no scheme has been parsed. */
      bool isNone() const
      {
        return this->name_.empty();
      }
      /** The lowercase name, or empty for none. */
      const core::String &name() const
      {
        return this->name_;
      }

    private:
      bool matchesIgnoringCase(const char *bytes, std::size_t length) const;
      core::String name_;
      friend class IncomingURL;
    };
  } // namespace app
} // namespace loka

#endif // LOKA_APP_URL_URL_SCHEME_HPP
