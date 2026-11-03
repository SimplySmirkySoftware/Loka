#include "app/url/URLInbox.hpp"
#include <cassert>

namespace loka
{
  namespace app
  {
    namespace
    {
      URLScheme parsedScheme(const char *scheme)
      {
        URLScheme parsed;
        URLScheme::parse(scheme, parsed);
        return parsed;
      }
    } // namespace

    URLInboxBase::URLInboxBase(const char *scheme, IncomingURL *storage, unsigned char capacity)
        : scheme_(parsedScheme(scheme)),
          ring_(storage, capacity)
    {
      assert(!this->scheme_.isNone());
    }
    URLInboxBase::~URLInboxBase() {}
    ReceiveResult URLInboxBase::offer(const core::String &text)
    {
      IncomingURL url;
      if (!IncomingURL::parse(text, this->scheme_, url))
        return RECEIVE_INVALID;
      if (!this->ring_.pushBack(url))
        return RECEIVE_FULL;
      return RECEIVE_QUEUED;
    }
    bool URLInboxBase::take(IncomingURL &out)
    {
      return this->ring_.advance(out);
    }
    unsigned URLInboxBase::count() const
    {
      return this->ring_.count();
    }
    void URLInboxBase::discardAll()
    {
      this->ring_.clear();
    }
  } // namespace app
} // namespace loka
