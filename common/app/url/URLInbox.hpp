#ifndef LOKA_APP_URL_URL_INBOX_HPP
#define LOKA_APP_URL_URL_INBOX_HPP

#include "app/url/IncomingURL.hpp"
#include "app/url/ReceiveResult.hpp"
#include "app/scene/state/RequestRing.hpp"

class App;

namespace loka
{
  namespace app
  {
    namespace testing
    {
      struct URLInboxAccess;
    }

    /** Config-owned FIFO for one declared scheme; the config must outlive the App.
        Destruction silently drops waiting values, without callbacks or State writes.
        Deliberately mirrors URLOpenerBase's borrowed derived storage. */
    class URLInboxBase
    {
    protected:
      /** Parses the declaration once; malformed schemes assert in debug and
          refuse every offer in release. Storage is not touched during construction. */
      URLInboxBase(const char *scheme, IncomingURL *storage, unsigned char capacity);
      /** Waiting values are destroyed with the derived storage. */
      ~URLInboxBase();

    private:
      friend struct testing::URLInboxAccess;
      friend class ::App;
      friend class URLReceiveSeatBase;
      URLInboxBase(const URLInboxBase &);
      URLInboxBase &operator=(const URLInboxBase &);
      /** Parses before checking capacity; refusal leaves the ring unchanged. */
      ReceiveResult offer(const core::String &text);
      /** Takes the FIFO head; empty leaves out untouched. */
      bool take(IncomingURL &out);
      /** Number of waiting values. */
      unsigned count() const;
      /** Silently clears waiting values without callbacks or State writes. */
      void discardAll();
      const URLScheme scheme_;
      scene::RequestRing<IncomingURL> ring_;
    };

    /** Exactly N waiting received URLs (1..255), owned by the config. */
    template <unsigned N> class URLInbox : public URLInboxBase
    {
      typedef typename scene::RequestRingCapacity<N>::Wall CapacityWall;

    public:
      /** Declares the one accepted scheme and initializes N none slots. */
      explicit URLInbox(const char *scheme)
          : URLInboxBase(scheme, this->storage_, static_cast<unsigned char>(N))
      {
        (void)sizeof(CapacityWall);
      }

    private:
      IncomingURL storage_[N];
    };
  } // namespace app
} // namespace loka
#endif
