#ifndef LOKA_APP_URL_URL_RECEIVE_SEAT_HPP
#define LOKA_APP_URL_URL_RECEIVE_SEAT_HPP

#include "app/url/URLInbox.hpp"
#include "dsl/flow/Flow.hpp"
#include <new>

namespace loka
{
  namespace app
  {
    /** App-owned receive registration borrowing the config-owned inbox.
        The config outlives the App. A received URL is untrusted input. */
    class URLReceiveSeatBase
    {
    public:
      explicit URLReceiveSeatBase(URLInboxBase &inbox)
          : inbox_(inbox)
      {
      }
      virtual ~URLReceiveSeatBase() {}
      URLInboxBase &inbox() const
      {
        return this->inbox_;
      }

      /** Takes one head and runs it; empty leaves firstRun untouched. */
      virtual bool serveOne(loka::dsl::FlowRunResult &firstRun) = 0;

    protected:
      bool take(IncomingURL &out)
      {
        return this->inbox_.take(out);
      }

    private:
      URLInboxBase &inbox_;
      URLReceiveSeatBase(const URLReceiveSeatBase &);
      URLReceiveSeatBase &operator=(const URLReceiveSeatBase &);
    };

    /** Owns the unexecuted Match definition for fresh delivery chains. */
    template <typename OutT> class URLReceiveSeat : public URLReceiveSeatBase
    {
    public:
      URLReceiveSeat(URLInboxBase &inbox, const loka::dsl::MatchSpec<IncomingURL, OutT> &match)
          : URLReceiveSeatBase(inbox),
            match_(match)
      {
      }

      /** A fresh chain per URL: never execute the seat's Match copy. Arms must
          finish synchronously. Pending runs are canceled and resumed once to
          finish cleanup before the stack input dies. Per delivery, copies all
          own arms and recursively clones children twice, scans matchers, runs
          the selected arm, then destroys the copies. */
      virtual bool serveOne(loka::dsl::FlowRunResult &firstRun)
      {
        IncomingURL url;
        if (!this->take(url))
          return false;
        loka::dsl::FlowChain<IncomingURL, OutT> chain =
            loka::dsl::Flow() | loka::dsl::Step(this->match_.id(), this->match_.adapter()).input(&url);
        firstRun = chain.runResult();
        if (firstRun == loka::dsl::FLOW_RUN_PENDING)
        {
          chain.cancel();
          chain.resumeResult(this->match_.id());
        }
        return true;
      }

    private:
      const loka::dsl::MatchSpec<IncomingURL, OutT> match_;
    };

    /** A declaration creates one independently owned receive registration. */
    class URLReceiveSeatDefinitionBase
    {
    public:
      virtual ~URLReceiveSeatDefinitionBase() {}
      virtual URLReceiveSeatBase *createSeat() const = 0;
    };

    /** Copyable declaration owning its Match value, including child chain clones.
        Every edge to app-borrowed data retained by the copied MatchSpec and its
        adapters, values, callbacks and child chains, recursively, must remain
        valid for the entire App lifetime: config-owned, outliving every Boundary.
        This includes userData, child steps' .input(...) targets, output values,
        MutableState and trackers, loading flags, and pointers inside adapters.
        Owned child chain clones and their steps instead live and die with the
        copy; function pointers point to code. Adapters must correctly copy their
        own contents. Arms must not retain references to IncomingURL or output
        values beyond the arm call. Destroying any copy, including its chains,
        steps, adapters and values, must fire no callback, write no State, and do
        nothing observable. A received URL is untrusted input.
        Once per composition, declaration and seat each copy the arm vector and
        clone each child chain; C++98 may add unelided return-value copies. */
    template <typename OutT> class URLReceiveSeatDefinition : public URLReceiveSeatDefinitionBase
    {
    public:
      URLReceiveSeatDefinition(URLInboxBase &inbox, const loka::dsl::MatchSpec<IncomingURL, OutT> &match)
          : inbox_(inbox),
            match_(match)
      {
      }
      /** Nothrow refuses only seat storage. The Match copy uses plain allocation;
          failure on Toolbox terminates through the out-of-memory quit. */
      virtual URLReceiveSeatBase *createSeat() const
      {
        return new (std::nothrow) URLReceiveSeat<OutT>(this->inbox_, this->match_);
      }

    private:
      URLInboxBase &inbox_;
      loka::dsl::MatchSpec<IncomingURL, OutT> match_;
    };

    /** Registers a config-owned inbox and copied Match under the declaration's
        lifetime contract above. Arbitrary Flow chains are not accepted. */
    template <typename OutT>
    URLReceiveSeatDefinition<OutT> ReceiveURLs(URLInboxBase &inbox,
                                               const loka::dsl::MatchSpec<IncomingURL, OutT> &match)
    {
      return URLReceiveSeatDefinition<OutT>(inbox, match);
    }
  } // namespace app
} // namespace loka
#endif
