#ifndef LOKA_APP_TEXT_DOCUMENT_PUBLISHER_HPP
#define LOKA_APP_TEXT_DOCUMENT_PUBLISHER_HPP
#include "app/scene/SceneTextDocuments.hpp"
class Window;
namespace loka
{
  namespace app
  {
    namespace detail
    {
      /** Single writer/taker, parallel to FocusPublisher; only Window enters completion. */
      class TextDocumentPublisher
      {
        static void complete(scene::SceneTextDocuments &documents, scene::IPlatformController &controller);
        static void publish(scene::TextDocumentRegistration &row, scene::IPlatformController &controller);
        friend class ::Window;
      };
    } // namespace detail
  } // namespace app
} // namespace loka
#endif
