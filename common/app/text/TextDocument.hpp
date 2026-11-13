#ifndef LOKA_APP_TEXT_DOCUMENT_HPP
#define LOKA_APP_TEXT_DOCUMENT_HPP
#include "app/text/TextSplice.hpp"
#include "app/scene/TwinLink.hpp"
namespace loka
{
  namespace app
  {
    namespace detail
    {
      class TextDocumentReplyAccess;
    }
    namespace scene
    {
      class TextDocumentRegistration;
    }
    namespace testing
    {
      struct TextDocumentAccess;
    }
    /** Root-Boundary-owned document. The kernel alone writes its fact. */
    class TextDocument
    {
    public:
      TextDocument()
          : registration_(this)
      {
      }
      core::State<TextDocumentFact> *fact() const
      {
        return this->fact_.state();
      }
      void post(const TextPosition &caret)
      {
        if (this->request_.isValid())
          this->request_.set(caret);
      }
      /** One pending replacement; publication also closes admission. Refused posts
          leave both boxes unchanged. Cancellation on detach delivers no reply. */
      scene::PostResult replace(const TextReplacement &replacement);
      /** Last taken replacement result; receipts retain no body bytes. */
      core::State<scene::Reply<TextReplacement> > *replaceReply() const
      {
        return this->replacement_.reply().state();
      }
      /** One pending splice; publication refuses admission. Cancellation delivers no
          reply. Positions must belong to this document (caller's precondition). */
      scene::PostResult splice(const TextSplice &value);
      /** Last taken splice result; receipts retain no body bytes. */
      core::State<scene::Reply<TextSplice> > *spliceReply() const
      {
        return this->splice_.reply().state();
      }
      bool queryLength(unsigned long &out) const;
      bool queryLineCount(unsigned long &out) const;
      bool queryLineRange(unsigned long line, TextPosition &start, TextPosition &end) const;
      bool queryUtf8Offset(const TextPosition &position, unsigned long &out) const;
      bool queryUtf16Offset(const TextPosition &position, unsigned long &out) const;
      bool queryPositionAtUtf8(unsigned long offset, TextPosition &out) const;
      bool queryOrder(const TextPosition &a, const TextPosition &b, int &out) const;

    private:
      TextDocument(const TextDocument &);
      TextDocument &operator=(const TextDocument &);
      bool canQuery() const;
      bool queryNativeOffset(const TextPosition &position, unsigned long &out) const;
      static TextPosition position(unsigned long revision, unsigned long offset)
      {
        return TextPosition(revision, offset);
      }
      static bool offset(const TextPosition &position, unsigned long revision, unsigned long &out)
      {
        if (position.isNone() || position.revision_ != revision)
          return false;
        out = position.offset_;
        return true;
      }
      scene::NodeState<TextDocumentFact> fact_;
      scene::Request<TextPosition> request_;
      scene::RequestWithReply<TextReplacement> replacement_;
      scene::RequestQueue<TextSplice, 1> splice_;
      scene::TwinLink<TextDocument, scene::TextDocumentRegistration> registration_;
      friend class TextReader;
      friend class scene::ComposableNode;
      friend class scene::TextDocumentRegistration;
      friend class detail::TextDocumentPublisher;
      friend struct testing::TextDocumentAccess;
#ifdef TEST_BUILD
      friend void testing::startTextDocumentRevision(TextDocument &, unsigned long);
#endif
    };
  } // namespace app
} // namespace loka
#endif
