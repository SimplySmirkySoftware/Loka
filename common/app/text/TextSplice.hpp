#ifndef LOKA_APP_TEXT_SPLICE_HPP
#define LOKA_APP_TEXT_SPLICE_HPP
#include "app/text/TextReplacement.hpp"
#include <cstring>
namespace loka
{
  namespace app
  {
    /** Immutable range replacement. Copies share the sealed inserted bytes. */
    class TextSplice
    {
    public:
      enum Stage
      {
        NONE,
        SPLICE
      };
      TextSplice()
          : stage_(NONE)
      {
      }
      static TextSplice None()
      {
        return TextSplice();
      }
      bool isNone() const
      {
        return this->stage_ == NONE;
      }
      /** Positions must come from the same document (caller's precondition).
       * Foreign positions outside the body are refused by later range validation.
       * Failure preserves out; deletion allocates nothing.
       */
      static TextDocumentResult FromUtf8(const TextPosition &from,
                                         const TextPosition &to,
                                         const char *bytes,
                                         unsigned long n,
                                         TextPlacement placement,
                                         TextSplice &out)
      {
        if (from.isNone() || to.isNone() || from.revision_ != to.revision_ || from.offset_ > to.offset_)
          return TEXT_DOCUMENT_PREPARE_FAILED;
        if (!bytes && n > 0)
          return TEXT_DOCUMENT_PREPARE_FAILED;
        if (from == to && n == 0)
          return TEXT_DOCUMENT_PREPARE_FAILED;
        TextReplacement inserted = TextReplacement::Empty(placement);
        if (n > 0)
        {
          core::resource::Blob sealed = core::resource::Blob::Create();
          if (!sealed.isValid())
            return TEXT_DOCUMENT_ALLOCATION;
          if (!sealed.tryResize(n))
            return TEXT_DOCUMENT_ALLOCATION;
          std::memcpy(sealed.mutableData(), bytes, n);
          sealed.sealBytes();
          const TextDocumentResult result = TextReplacement::FromUtf8(sealed, placement, inserted);
          if (result != TEXT_DOCUMENT_OK)
            return result;
        }
        out = TextSplice(from, to, inserted);
        return TEXT_DOCUMENT_OK;
      }
      Stage stage() const
      {
        return this->stage_;
      }
      TextPosition from() const
      {
        return this->from_;
      }
      TextPosition to() const
      {
        return this->to_;
      }
      const TextReplacement &inserted() const
      {
        return this->inserted_;
      }
      TextPlacement placement() const
      {
        return this->inserted_.placement();
      }
      bool operator!=(const TextSplice &other) const
      {
        return this->stage_ != other.stage_ || this->from_ != other.from_ || this->to_ != other.to_
               || this->inserted_ != other.inserted_;
      }

    private:
      TextSplice(const TextPosition &from, const TextPosition &to, const TextReplacement &inserted)
          : stage_(SPLICE),
            from_(from),
            to_(to),
            inserted_(inserted)
      {
      }
      Stage stage_;
      TextPosition from_;
      TextPosition to_;
      TextReplacement inserted_;
    };
    /** Small immutable receipt; retains neither inserted bytes nor requested positions. */
    class TextSpliceReceipt
    {
    public:
      TextSpliceReceipt()
          : placement_(TextPlacement::Start()),
            length_(0)
      {
      }
      explicit TextSpliceReceipt(const TextSplice &value)
          : placement_(value.placement()),
            length_(value.inserted().logicalLength())
      {
      }
      TextPlacement placement() const
      {
        return this->placement_;
      }
      unsigned long logicalLength() const
      {
        return this->length_;
      }
      bool operator!=(const TextSpliceReceipt &other) const
      {
        return this->placement_ != other.placement_ || this->length_ != other.length_;
      }

    private:
      TextPlacement placement_;
      unsigned long length_;
    };
    /** Result of applying a range replacement. */
    enum TextSpliceResult
    {
      /** The splice was accepted. */
      TEXT_SPLICE_OK,
      /** The store is absent or unreadable, the rail has no splice door, or native editing refused. */
      TEXT_SPLICE_UNAVAILABLE,
      /** The revision differs or a replacement succeeded in the same completion. */
      TEXT_SPLICE_STALE,
      /** The document has an active composition. */
      TEXT_SPLICE_COMPOSING,
      /** An endpoint is outside the body or is not a native text boundary. */
      TEXT_SPLICE_RANGE,
      /** The rail cannot represent the inserted content, including unsupported NUL. */
      TEXT_SPLICE_NOT_REPRESENTABLE,
      /** Preparing the splice could not allocate storage. */
      TEXT_SPLICE_ALLOCATION,
      /** The resulting body would exceed the native editor's capacity. */
      TEXT_SPLICE_CAPACITY
    };
    namespace scene
    {
      /** Splices accumulate as commands and must not coalesce. */
      template <> struct RequestTraits<TextSplice>
      {
        enum
        {
          coalescable = 0
        };
        typedef TextSpliceResult Reason;
        static Reason accepted()
        {
          return TEXT_SPLICE_OK;
        }
        static Reason unavailable()
        {
          return TEXT_SPLICE_UNAVAILABLE;
        }
      };
      /** Visible with the value, before any Reply instantiation. */
      template <> struct ReplyReceipt<TextSplice>
      {
        typedef TextSpliceReceipt Type;
        static Type of(const TextSplice &value)
        {
          return Type(value);
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
