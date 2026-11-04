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
      /** Single writer/taker, parallel to FocusPublisher; only Window enters completion or refresh. */
      class TextDocumentPublisher
      {
        class Visitor;
        static void complete(scene::SceneTextDocuments &documents, scene::IPlatformController &controller);
        enum Requests
        {
          TAKE_REQUESTS,
          KEEP_REQUESTS
        };
        static void refresh(scene::SceneTextDocuments &documents, scene::IPlatformController &controller);
        static void
        publish(scene::TextDocumentRegistration &row, scene::IPlatformController &controller, Requests requests);
        /** One row of complete or refresh; refresh first asks a linked row to commit.
            A member of the befriended class, so GCC 4.0.1 grants it the registration's
            private phase, which the nested Visitor would not get. */
        static void visitRow(scene::TextDocumentRegistration &row, scene::IPlatformController &controller, Requests requests);
        friend class ::Window;
      };
    } // namespace detail
  } // namespace app
} // namespace loka
#endif
