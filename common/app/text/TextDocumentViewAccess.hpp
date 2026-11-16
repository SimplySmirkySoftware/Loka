#ifndef LOKA_APP_TEXT_DOCUMENT_VIEW_ACCESS_HPP
#define LOKA_APP_TEXT_DOCUMENT_VIEW_ACCESS_HPP
#include "app/text/TextViewValues.hpp"
namespace loka
{
  namespace app
  {
    class TextDocument;
    namespace scene
    {
      class TextViewClaim;
      class TextDocumentViewContext;
    } // namespace scene
    namespace detail
    {
      /** Private kernel admission and projection doors; no app mutation authority. */
      class TextDocumentViewAccess
      {
        static void attach(scene::TextViewClaim &, TextDocument &, scene::TextDocumentViewContext &);
        static void detach(scene::TextViewClaim &);
        static void present(scene::TextViewClaim &, const TextViewPresentation &);
        friend class ::loka::app::scene::TextDocumentViewContext;
      };
    } // namespace detail
  } // namespace app
} // namespace loka
#endif
