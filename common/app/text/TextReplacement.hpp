#ifndef LOKA_APP_TEXT_REPLACEMENT_HPP
#define LOKA_APP_TEXT_REPLACEMENT_HPP
#include "app/text/TextDocumentValues.hpp"
#include "app/text/TextDocumentResult.hpp"
#include "app/text/TextSeparators.hpp"
#include "core/resource/Blob.hpp"
#include "core/io/File.hpp"
#include "core/Utf8Decode.hpp"
#include <climits>
class PlatformContext;
namespace loka
{
  namespace app
  {
    /** Immutable validated whole-body input. Copies retain sealed bytes, never copy the body. */
    class TextReplacement
    {
    public:
      enum Stage
      {
        NONE,
        EMPTY,
        BYTES
      };
      TextReplacement()
          : stage_(NONE),
            placement_(TextPlacement::Start()),
            length_(0)
      {
      }
      static TextReplacement Empty(TextPlacement placement = TextPlacement::Start())
      {
        return TextReplacement(EMPTY, core::resource::Blob(), placement, 0);
      }
      /** Only sealed UTF-8 is admitted. Failure preserves out; memory input has no source. */
      static TextDocumentResult
      FromUtf8(const core::resource::Blob &sealed, TextPlacement placement, TextReplacement &out)
      {
        if (!sealed.isCompleted())
          return TEXT_DOCUMENT_PREPARE_FAILED;
        unsigned long length = 0;
        const TextDocumentResult result = measure(sealed, length);
        if (result != TEXT_DOCUMENT_OK)
          return result;
        out = TextReplacement(BYTES, sealed, placement, length);
        return TEXT_DOCUMENT_OK;
      }
      Stage stage() const
      {
        return this->stage_;
      }
      TextPlacement placement() const
      {
        return this->placement_;
      }
      unsigned long logicalLength() const
      {
        return this->length_;
      }
      const core::resource::Blob &bytes() const
      {
        return this->bytes_;
      }
      bool querySource(file::File &out) const
      {
        if (this->source_.sharesRepresentation(file::File()))
          return false;
        out = this->source_;
        return true;
      }
      bool operator!=(const TextReplacement &other) const
      {
        return this->stage_ != other.stage_ || this->bytes_ != other.bytes_ || this->placement_ != other.placement_
               || !this->source_.sharesRepresentation(other.source_);
      }

    private:
      /** Validate and measure private scratch or an already sealed input in one pass. */
      static TextDocumentResult measure(const core::resource::Blob &blob, unsigned long &length)
      {
        std::size_t cursor = 0;
        length = 0;
        const char *bytes = reinterpret_cast<const char *>(blob.data());
        while (cursor < blob.size())
        {
          const std::size_t start = cursor;
          unsigned int scalar = 0;
          if (!core::detail::DecodeUtf8Codepoint(bytes, blob.size(), cursor, scalar))
            return TEXT_DOCUMENT_NOT_TEXT;
          const bool separator = TextSeparators::Fold(scalar, bytes, blob.size(), cursor);
          const unsigned long count = separator ? 1 : static_cast<unsigned long>(cursor - start);
          if (count > ULONG_MAX - length)
            return TEXT_DOCUMENT_TOO_LARGE;
          length += count;
        }
        return TEXT_DOCUMENT_OK;
      }
      TextReplacement(Stage stage, const core::resource::Blob &bytes, TextPlacement placement, unsigned long length)
          : stage_(stage),
            bytes_(bytes),
            placement_(placement),
            length_(length)
      {
      }
      friend class detail::TextDocumentPublisher;
      Stage stage_;
      core::resource::Blob bytes_;
      TextPlacement placement_;
      unsigned long length_;
      file::File source_;
      friend TextDocumentResult
      ReadTextReplacement(PlatformContext &, const file::File &, TextPlacement, TextReplacement &);
    };
    /** Small immutable receipt; deliberately retains neither the bytes nor their source. */
    class TextReplacementReceipt
    {
    public:
      TextReplacementReceipt()
          : stage_(TextReplacement::NONE),
            placement_(TextPlacement::Start()),
            length_(0)
      {
      }
      explicit TextReplacementReceipt(const TextReplacement &value)
          : stage_(value.stage()),
            placement_(value.placement()),
            length_(value.logicalLength())
      {
      }
      TextReplacement::Stage stage() const
      {
        return this->stage_;
      }
      TextPlacement placement() const
      {
        return this->placement_;
      }
      unsigned long logicalLength() const
      {
        return this->length_;
      }
      bool operator!=(const TextReplacementReceipt &other) const
      {
        return this->stage_ != other.stage_ || this->placement_ != other.placement_ || this->length_ != other.length_;
      }

    private:
      TextReplacement::Stage stage_;
      TextPlacement placement_;
      unsigned long length_;
    };
    /** Result of applying a whole-body replacement. */
    enum TextReplaceResult
    {
      TEXT_REPLACE_OK,
      TEXT_REPLACE_UNAVAILABLE,
      TEXT_REPLACE_COMPOSING,
      TEXT_REPLACE_NOT_REPRESENTABLE,
      TEXT_REPLACE_ALLOCATION,
      TEXT_REPLACE_CAPACITY
    };
    namespace scene
    {
      /** Whole-body input is a coalescable value; admission belongs to its endpoint. */
      template <> struct RequestTraits<TextReplacement>
      {
        enum
        {
          coalescable = 1
        };
        typedef TextReplaceResult Reason;
        static Reason accepted()
        {
          return TEXT_REPLACE_OK;
        }
        static Reason unavailable()
        {
          return TEXT_REPLACE_UNAVAILABLE;
        }
      };
      /** Specialization is visible with the value, before any Reply instantiation. */
      template <> struct ReplyReceipt<TextReplacement>
      {
        typedef TextReplacementReceipt Type;
        static Type of(const TextReplacement &value)
        {
          return Type(value);
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
