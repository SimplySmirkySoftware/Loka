#ifndef LOKA_APP_TEXT_TEXT_DOCUMENT_VALUES_HPP
#define LOKA_APP_TEXT_TEXT_DOCUMENT_VALUES_HPP

#include "app/scene/state/Request.hpp"
#include "app/text/TextDocumentOrigin.hpp"

namespace loka
{
  namespace app
  {
    class TextDocument;
    namespace detail
    {
      class TextDocumentPublisher;
    }
    namespace testing
    {
      struct TextPositionTestAccess;
      struct TextDocumentFactTestAccess;
#ifdef TEST_BUILD
      void startTextDocumentRevision(TextDocument &, unsigned long);
#endif
    } // namespace testing

    /** Whether every visited window admitted the command refresh; not document availability. */
    enum TextInputCommit
    {
      TEXT_INPUT_SETTLED,
      TEXT_INPUT_UNAVAILABLE
    };

    /** Availability of the document's native text store. */
    enum TextDocumentState
    {
      TEXT_DOCUMENT_AWAITING_STORE,
      TEXT_DOCUMENT_READY,
      TEXT_DOCUMENT_MARKED,
      TEXT_DOCUMENT_UNAVAILABLE
    };

    /** Opaque revision-scoped position in the rail's native unit; only the document creates one. */
    class TextPosition
    {
    public:
      TextPosition()
          : revision_(0),
            offset_(0),
            held_(false)
      {
      }
      static TextPosition None()
      {
        return TextPosition();
      }
      bool isNone() const
      {
        return !this->held_;
      }
      bool operator==(const TextPosition &other) const
      {
        return this->held_ == other.held_
               && (!this->held_ || (this->revision_ == other.revision_ && this->offset_ == other.offset_));
      }
      bool operator!=(const TextPosition &other) const
      {
        return !(*this == other);
      }

    private:
      TextPosition(unsigned long revision, unsigned long offset)
          : revision_(revision),
            offset_(offset),
            held_(true)
      {
      }
      unsigned long revision_;
      unsigned long offset_;
      bool held_;
      friend class TextDocument;
      friend struct testing::TextPositionTestAccess;
    };

    /** Result of taking a document caret request. */
    enum TextRequestResult
    {
      TEXT_REQUEST_OK,
      TEXT_REQUEST_STALE,
      TEXT_REQUEST_UNAVAILABLE
    };

    /** Caret placement after a whole write; offsets count UTF-8 bytes in the written body. */
    class TextPlacement
    {
    public:
      /** The three supported whole-write placement choices. */
      enum Kind
      {
        START,
        END,
        AT_UTF8_OFFSET
      };
      static TextPlacement Start()
      {
        return TextPlacement(START, 0);
      }
      static TextPlacement End()
      {
        return TextPlacement(END, 0);
      }
      static TextPlacement AtUtf8Offset(unsigned long offset)
      {
        return TextPlacement(AT_UTF8_OFFSET, offset);
      }
      Kind kind() const
      {
        return this->kind_;
      }
      /** Meaningful for AT_UTF8_OFFSET; Start and End carry zero. */
      unsigned long utf8Offset() const
      {
        return this->offset_;
      }
      bool operator==(const TextPlacement &other) const
      {
        return this->kind_ == other.kind_ && this->offset_ == other.offset_;
      }
      bool operator!=(const TextPlacement &other) const
      {
        return !(*this == other);
      }

    private:
      TextPlacement(Kind kind, unsigned long offset)
          : kind_(kind),
            offset_(offset)
      {
      }
      Kind kind_;
      unsigned long offset_;
    };

    /** One immutable document snapshot; the optional changed interval is an invalidation hint. */
    class TextDocumentFact
    {
    public:
      TextDocumentFact()
          : state_(TEXT_DOCUMENT_AWAITING_STORE),
            textRevision_(0),
            caret_(),
            selectionAnchor_(),
            changedStart_(),
            insertedEnd_()
      {
      }
      /** Source and revision of the last landed replacement, unchanged by edits. */
      TextDocumentOrigin origin() const
      {
        return this->origin_;
      }
      TextDocumentState state() const
      {
        return this->state_;
      }
      unsigned long textRevision() const
      {
        return this->textRevision_;
      }
      TextPosition caret() const
      {
        return this->caret_;
      }
      TextPosition selectionAnchor() const
      {
        return this->selectionAnchor_;
      }
      /** Refusal leaves out unchanged; an empty reported range is still present. */
      bool queryChangedRange(TextPosition &start, TextPosition &insertedEnd) const
      {
        if (this->changedStart_.isNone())
          return false;
        start = this->changedStart_;
        insertedEnd = this->insertedEnd_;
        return true;
      }
      bool operator==(const TextDocumentFact &other) const
      {
        return this->state_ == other.state_ && this->textRevision_ == other.textRevision_
               && this->caret_ == other.caret_ && this->selectionAnchor_ == other.selectionAnchor_
               && this->changedStart_ == other.changedStart_ && this->insertedEnd_ == other.insertedEnd_
               && this->origin_ == other.origin_;
      }
      bool operator!=(const TextDocumentFact &other) const
      {
        return !(*this == other);
      }

    private:
      TextDocumentFact(TextDocumentState state,
                       unsigned long revision,
                       const TextPosition &caret,
                       const TextPosition &selectionAnchor,
                       const TextPosition &changedStart,
                       const TextPosition &insertedEnd,
                       const TextDocumentOrigin &origin = TextDocumentOrigin())
          : state_(state),
            textRevision_(revision),
            caret_(caret),
            selectionAnchor_(selectionAnchor),
            changedStart_(changedStart),
            insertedEnd_(insertedEnd),
            origin_(origin)
      {
      }
      TextDocumentState state_;
      unsigned long textRevision_;
      TextPosition caret_;
      TextPosition selectionAnchor_;
      TextPosition changedStart_;
      TextPosition insertedEnd_;
      TextDocumentOrigin origin_;
      friend class detail::TextDocumentPublisher;
      friend struct testing::TextDocumentFactTestAccess;
#ifdef TEST_BUILD
      friend void testing::startTextDocumentRevision(TextDocument &, unsigned long);
#endif
    };

    namespace scene
    {
      /** A caret destination replaces any earlier pending destination. */
      template <> struct RequestTraits<TextPosition>
      {
        enum
        {
          coalescable = 1
        };
        typedef TextRequestResult Reason;
        static Reason accepted()
        {
          return TEXT_REQUEST_OK;
        }
        static Reason unavailable()
        {
          return TEXT_REQUEST_UNAVAILABLE;
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
