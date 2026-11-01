#ifndef LOKA_APP_URL_URL_OPENER_HPP
#define LOKA_APP_URL_URL_OPENER_HPP

#include "app/url/WebURL.hpp"
#include "app/scene/state/Request.hpp"
#include "core/State.hpp"

namespace loka
{
  namespace app
  {
    namespace testing
    {
      struct URLOpenerAccess;
    }

    /** System acceptance only; no completion or handler-specific guarantees. */
    enum URLOpenKind
    {
      /** The system accepted the URL; this does not promise browser display. */
      URL_OPENED,
      /** The system refused the URL; no particular reason is promised. */
      URL_NOT_OPENED,
      /** This system has no URL-opening mechanism; not a handler refusal. */
      URL_UNAVAILABLE
    };

    /** Last delivered fact. A none URL means no result, regardless of kind. */
    struct URLOpenOutcome
    {
      URLOpenOutcome()
          : kind(URL_UNAVAILABLE),
            url()
      {
      }
      URLOpenOutcome(URLOpenKind result, const WebURL &value)
          : kind(result),
            url(value)
      {
      }
      static URLOpenOutcome None()
      {
        return URLOpenOutcome();
      }
      bool isNone() const
      {
        return this->url.isNone();
      }
      bool operator==(const URLOpenOutcome &other) const
      {
        return this->kind == other.kind && this->url == other.url;
      }
      bool operator!=(const URLOpenOutcome &other) const
      {
        return !(*this == other);
      }
      URLOpenKind kind;
      WebURL url;
    };

    /** Config-owned FIFO and last result; the config must outlive the App.
        One run owns one opener. Destruction silently drops waiting and sealed
        values. Borrowers neither retain it nor extend its lifetime. */
    class URLOpenerBase
    {
    public:
      /** Enqueues only: does not wake the App or open a URL. */
      scene::PostResult post(const WebURL &url);
      unsigned pending() const
      {
        return this->ring_.count();
      }
      /** Only the last delivered result, not a reply per post. Same-value
          delivery may not notify observers. Initially none. */
      const core::State<URLOpenOutcome> &outcome() const
      {
        return this->outcome_;
      }

    protected:
      URLOpenerBase(WebURL *storage, unsigned char capacity);
      ~URLOpenerBase();

    private:
      friend struct testing::URLOpenerAccess;
      URLOpenerBase(const URLOpenerBase &);
      URLOpenerBase &operator=(const URLOpenerBase &);
      /** Only these two doors transition IDLE -> LAUNCHING -> SEALED ->
          DELIVERING -> IDLE. Launch removes a stack copy before calling open
          synchronously; the callback and user pointer are borrowed only for
          that call. */
      bool launchNext(URLOpenKind (*open)(const WebURL &, void *), void *user);
      /** Joins the active turn. Under Operation::Regime a turn is required.
          Stays DELIVERING, refusing both doors, until set's callbacks return. */
      bool deliverSealed();
      scene::RequestRing<WebURL> ring_;
      core::PushStateTracker tracker_;
      core::MutableState<URLOpenOutcome> outcome_;
      enum Phase
      {
        IDLE,
        LAUNCHING,
        SEALED,
        DELIVERING
      };
      Phase phase_;
      URLOpenOutcome sealed_;
    };

    /** Exactly N waiting URLs (1..255), with no published request slot. */
    template <unsigned N> class URLOpener : public URLOpenerBase
    {
      typedef typename scene::RequestRingCapacity<N>::Wall CapacityWall;

    public:
      URLOpener()
          : URLOpenerBase(this->storage_, static_cast<unsigned char>(N))
      {
        (void)sizeof(CapacityWall);
      }

    private:
      WebURL storage_[N];
    };
  } // namespace app
} // namespace loka
#endif
