#ifndef LOKA_APP_URL_OPEN_URLS_HPP
#define LOKA_APP_URL_OPEN_URLS_HPP

#include "app/url/URLOpener.hpp"

namespace loka
{
  namespace app
  {
    /** Registers a borrowed config-owned opener; the config outlives App. */
    class OpenURLs
    {
    public:
      explicit OpenURLs(URLOpenerBase &opener)
          : opener_(opener)
      {
      }
      URLOpenerBase &opener() const
      {
        return this->opener_;
      }

    private:
      URLOpenerBase &opener_;
    };
  } // namespace app
} // namespace loka
#endif
