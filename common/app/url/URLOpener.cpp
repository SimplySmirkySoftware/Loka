#include "app/url/URLOpener.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace loka
{
  namespace app
  {
    URLOpenerBase::URLOpenerBase(WebURL *storage, unsigned char capacity)
        : ring_(storage, capacity),
          tracker_(),
          outcome_(URLOpenOutcome::None()),
          phase_(IDLE),
          sealed_()
    {
      this->tracker_.addState(&this->outcome_);
    }
    URLOpenerBase::~URLOpenerBase()
    {
      this->tracker_.removeState(&this->outcome_);
    }
    scene::PostResult URLOpenerBase::post(const WebURL &url)
    {
      if (url.isNone())
        return scene::POST_INVALID;
      if (!this->ring_.pushBack(url))
        return scene::POST_QUEUE_FULL;
      return scene::POST_ACCEPTED;
    }
    bool URLOpenerBase::launchNext(URLOpenKind (*open)(const WebURL &, void *), void *user)
    {
      if (this->phase_ != IDLE)
        return false;
      WebURL next;
      if (!this->ring_.advance(next))
        return false;
      this->phase_ = LAUNCHING;
      const URLOpenKind result = open(next, user);
      this->sealed_ = URLOpenOutcome(result, next);
      this->phase_ = SEALED;
      return true;
    }
    bool URLOpenerBase::deliverSealed()
    {
      if (this->phase_ != SEALED)
        return false;
      const URLOpenOutcome delivered = this->sealed_;
      this->sealed_ = URLOpenOutcome::None();
      this->phase_ = DELIVERING;
      {
        core::StateTrackerGuard guard(&this->tracker_);
        this->outcome_.set(delivered);
      }
      this->phase_ = IDLE;
      return true;
    }
  } // namespace app
} // namespace loka
