#include "app/TextDocumentFile.hpp"
#include "app/PlatformContext.hpp"
#include "platform/file/FileIO.hpp"
#include "app/nodes/controls/TextDocumentLine.hpp"
#include <string>

namespace loka
{
  namespace app
  {
    namespace
    {
      /** Admission is checked by ReadBytes before allocation, including growth. */
      class TextReadCapacity : public platform::file::ReadCapacity
      {
      public:
        // User-provided so a const instance may be default-initialized:
        // compilers before DR 253 (Xcode 5/6 clang) refuse it otherwise.
        TextReadCapacity() {}
        virtual bool allows(std::size_t bytes) const
        {
          return bytes <= TextEditorProps::kMaxBytes + TextEditorProps::kMaxLines - 1;
        }
      };

      /** Both passes borrow the same live IDs and completed payloads. Reverse
          removal avoids shifting rows and admits full-to-full replacement. */
      class DocumentReplaceCursor : public core::ListOpCursor<core::String>
      {
      public:
        DocumentReplaceCursor(const core::ObservableList<core::String> &lines,
                              const core::String *rows,
                              unsigned short count)
            : lines_(lines),
              rows_(rows),
              count_(count),
              position_(0)
        {
        }
        virtual void rewind()
        {
          this->position_ = 0;
        }
        virtual bool next(core::ListOp<core::String> &out)
        {
          if (this->position_ < this->lines_.size())
            out =
                core::ListOp<core::String>(core::REMOVE, this->lines_.at(static_cast<unsigned short>(this->lines_.size() - 1 - this->position_)).id);
          else
          {
            const unsigned int row = this->position_ - this->lines_.size();
            if (row >= this->count_)
              return false;
            out = core::ListOp<core::String>(
                core::INSERT, core::ItemId(), static_cast<unsigned short>(row), this->rows_[row]);
          }
          ++this->position_;
          return true;
        }

      private:
        const core::ObservableList<core::String> &lines_;
        const core::String *const rows_;
        const unsigned short count_;
        unsigned int position_;
      };

      TextDocumentResult commitResult(core::ListEditResult result)
      {
        switch (result)
        {
        case core::EDIT_OK:
          return TEXT_DOCUMENT_OK;
        case core::EDIT_CAPACITY_EXCEEDED:
          return TEXT_DOCUMENT_CAPACITY;
        case core::EDIT_ID_NOT_FOUND:
          return TEXT_DOCUMENT_STALE_ID;
        case core::EDIT_INDEX_OUT_OF_RANGE:
          return TEXT_DOCUMENT_INVALID_INDEX;
        case core::EDIT_SEQ_EXHAUSTED:
        case core::EDIT_GENERATION_EXHAUSTED:
          return TEXT_DOCUMENT_ID_EXHAUSTED;
        case core::EDIT_NOT_ATTACHED:
          return TEXT_DOCUMENT_UNATTACHED;
        case core::EDIT_REENTRANT:
          return TEXT_DOCUMENT_REENTRANT;
        }
        return TEXT_DOCUMENT_UNATTACHED;
      }

      TextDocumentResult lineResult(EditorResult result)
      {
        // LineBytes/Measure return only these four outcomes.
        if (result == EDITOR_OK)
          return TEXT_DOCUMENT_OK;
        if (result == EDITOR_CAPACITY)
          return TEXT_DOCUMENT_TOO_LARGE;
        if (result == EDITOR_NON_ASCII)
          return TEXT_DOCUMENT_NON_ASCII;
        return TEXT_DOCUMENT_ALLOCATION;
      }
    } // namespace

    TextDocumentResult
    ReadTextDocument(PlatformContext *context, const loka::file::File &file, core::ObservableList<core::String> &lines)
    {
      using namespace platform::file;
      if (file.base() == loka::file::File::BASE_REFUSED)
        return TEXT_DOCUMENT_READ_FAILED;
      FileHandle handle;
      const bool resolved = context && context->openFile(file, handle);
      const bool located = !file.locator().empty();
      if (located && !resolved)
        return TEXT_DOCUMENT_READ_FAILED;
      core::resource::Blob bytes = core::resource::Blob::Create();
      if (!bytes.isValid())
        return TEXT_DOCUMENT_ALLOCATION;
      const TextReadCapacity capacity;
      ReadResult result;
      if (resolved)
      {
        result = ReadBytes(handle, bytes, &capacity);
        if (!located && result != READ_OK && result != READ_CAPACITY_REFUSED && result != READ_ALLOCATION_REFUSED)
          result = ReadBytes(file.base() == loka::file::File::BASE_APPLICATION ? handle.displayPath : file.toString(),
                             bytes,
                             &capacity);
      }
      else if (file.base() == loka::file::File::BASE_APPLICATION)
        return TEXT_DOCUMENT_READ_FAILED; // unresolved: no path to flatten
      else
        result = ReadBytes(file.toString(), bytes, &capacity);
      if (result == READ_ALLOCATION_REFUSED)
        return TEXT_DOCUMENT_ALLOCATION;
      if (result == READ_CAPACITY_REFUSED || result == READ_SIZE_OVERFLOW)
        return TEXT_DOCUMENT_TOO_LARGE;
      if (result != READ_OK)
        return TEXT_DOCUMENT_READ_FAILED;

      core::String rows[TextEditorProps::kMaxLines];
      unsigned short count = 0;
      std::size_t start = 0, normalized = 0;
      for (std::size_t i = 0;; ++i)
      {
        if (i < bytes.size() && bytes.data()[i] != '\r' && bytes.data()[i] != '\n')
          continue;
        if (count == TextEditorProps::kMaxLines)
          return TEXT_DOCUMENT_TOO_LARGE;
        const std::size_t length = i - start;
        const char *data = length ? reinterpret_cast<const char *>(&bytes.data()[start]) : "";
        const EditorResult valid = text_document_detail::ValidateRow(data, length);
        if (valid != EDITOR_OK)
          return lineResult(valid);
        rows[count] = core::String::Utf8(data, length);
        const text_document_detail::LineBytes row(rows[count]);
        if (row.result() != EDITOR_OK)
          return lineResult(row.result());
        // A nullable rail String allocation must not turn a nonempty row empty.
        if (row.size() != length)
          return TEXT_DOCUMENT_ALLOCATION;
        normalized += row.size() + (count ? 1 : 0);
        if (normalized > TextEditorProps::kMaxBytes)
          return TEXT_DOCUMENT_TOO_LARGE;
        ++count;
        if (i == bytes.size())
          break;
        if (bytes.data()[i] == '\r' && i + 1 < bytes.size() && bytes.data()[i + 1] == '\n')
          ++i;
        start = i + 1;
      }
      DocumentReplaceCursor ops(lines, rows, count);
      return commitResult(lines.apply(ops));
    }

    TextDocumentResult WriteTextDocument(PlatformContext *context,
                                         const loka::file::File &file,
                                         const core::ObservableList<core::String> &lines)
    {
      using namespace platform::file;
      std::size_t normalized = 0;
      const TextDocumentResult valid = lineResult(text_document_detail::Measure(lines, normalized));
      if (valid != TEXT_DOCUMENT_OK)
        return valid;
      std::string bytes;
      bytes.reserve(normalized + (lines.size() ? lines.size() - 1 : 0));
      for (unsigned short i = 0; i < lines.size(); ++i)
      {
        const text_document_detail::LineBytes row(lines.at(i).value);
        if (row.result() != EDITOR_OK)
          return lineResult(row.result());
        if (i)
          bytes.append(kTextDocumentNewline);
        bytes.append(row.data(), row.size());
      }
      FileHandle handle;
      if (file.base() == loka::file::File::BASE_REFUSED || !context || !context->openFile(file, handle))
        return TEXT_DOCUMENT_OPEN_FAILED;
      const PrepareResult prepared = platform::file::PrepareTextDocumentDestination(handle);
      if (prepared != PREPARE_OK)
        return prepared == PREPARE_NOT_TEXT ? TEXT_DOCUMENT_NOT_TEXT : TEXT_DOCUMENT_PREPARE_FAILED;
      std::FILE *stream = platform::file::OpenWriteTruncate(handle);
      if (!stream)
        return TEXT_DOCUMENT_OPEN_FAILED;
      TextDocumentResult result = TEXT_DOCUMENT_OK;
      if (std::fwrite(bytes.data(), 1, bytes.size(), stream) != bytes.size() || std::ferror(stream))
        result = TEXT_DOCUMENT_WRITE_FAILED;
      if (!platform::file::FlushWrite(stream, handle) && result == TEXT_DOCUMENT_OK)
        result = TEXT_DOCUMENT_FLUSH_FAILED;
      if (std::fclose(stream) != 0 && result == TEXT_DOCUMENT_OK)
        result = TEXT_DOCUMENT_CLOSE_FAILED;
      return result;
    }
  } // namespace app
} // namespace loka
