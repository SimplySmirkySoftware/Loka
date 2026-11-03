#ifndef LOKA_APP_URL_URL_OPEN_KIND_HPP
#define LOKA_APP_URL_URL_OPEN_KIND_HPP

namespace loka
{
  namespace app
  {
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

  } // namespace app
} // namespace loka
#endif
