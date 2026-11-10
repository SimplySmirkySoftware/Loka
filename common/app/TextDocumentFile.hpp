#ifndef LOKA_APP_TEXT_DOCUMENT_FILE_HPP
#define LOKA_APP_TEXT_DOCUMENT_FILE_HPP

#include "app/TextBuffer.hpp"
#include "app/text/TextReplacement.hpp"
#include "core/String.hpp"
#include "core/io/File.hpp"

class PlatformContext;

namespace loka
{
  namespace app
  {
    class TextDocument;

    /** The rail's serialized separator; no separator follows the last row. */
#if defined(LOKA_RETRO68)
    const char kTextDocumentNewline[] = "\r";
#elif defined(_WIN32)
    const char kTextDocumentNewline[] = "\r\n";
#else
    const char kTextDocumentNewline[] = "\n";
#endif

    /** Read within the admitted capacity plus CRLF expansion and a leading UTF-8 BOM,
        drop that BOM, split CR/LF/CRLF, and replace through one
        list apply. All refusals preserve rows, IDs and revision. An empty file
        becomes one empty row; trailing separators remain trailing empty rows.
        Located/refused Files never retry their display name as a path.
        Completed row handles are sealed before allocation-free list replay. */
    TextDocumentResult ReadTextDocument(PlatformContext *context, const loka::file::File &file, TextBuffer &buffer);

    /** Read raw UTF-8, removing only the leading file BOM. Failure preserves out.
        This is the only factory that attaches a source. */
    TextDocumentResult ReadTextReplacement(PlatformContext &context,
                                           const file::File &file,
                                           TextPlacement placement,
                                           TextReplacement &out);

    /** Validate and stage the complete editor document before filesystem work,
        then prepare and destructively truncate/write/flush/close. The caller
        retains the list unchanged. Zero rows serialize like one empty row.
        Reports the first observable seam/stdio failure; Retro68 CRT errors
        discarded inside that runtime cannot be recovered here. */
    TextDocumentResult
    WriteTextDocument(PlatformContext *context, const loka::file::File &file, const TextBuffer &buffer);
    /** Finish dialogs before calling; do not pump from call start to return.
        Validate the entire body before filesystem work, then read again and
        serialize in bounded chunks. Pass 2 can fail after truncation, including
        allocation refusal, and returns WRITE_FAILED. No whole-body copy. */
    TextDocumentResult
    WriteTextDocument(PlatformContext *context, const loka::file::File &file, const TextDocument &document);
  } // namespace app
} // namespace loka
#endif
