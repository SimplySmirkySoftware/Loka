#ifndef LOKA_APP_TEXT_DOCUMENT_FILE_HPP
#define LOKA_APP_TEXT_DOCUMENT_FILE_HPP

#include "app/TextBuffer.hpp"
#include "core/String.hpp"
#include "core/io/File.hpp"

class PlatformContext;

namespace loka
{
  namespace app
  {
    /** Synchronous document IO outcome. OPEN/WRITE/FLUSH/CLOSE failures mean
        the destination may have changed, including an OPEN failure. */
    enum TextDocumentResult
    {
      TEXT_DOCUMENT_OK,
      TEXT_DOCUMENT_READ_FAILED,
      TEXT_DOCUMENT_TOO_LARGE,
      TEXT_DOCUMENT_NON_ASCII,
      TEXT_DOCUMENT_ALLOCATION,
      TEXT_DOCUMENT_CAPACITY,
      TEXT_DOCUMENT_STALE_ID,
      TEXT_DOCUMENT_INVALID_INDEX,
      TEXT_DOCUMENT_ID_EXHAUSTED,
      TEXT_DOCUMENT_UNATTACHED,
      TEXT_DOCUMENT_REENTRANT,
      TEXT_DOCUMENT_PREPARE_FAILED,
      TEXT_DOCUMENT_NOT_TEXT,
      TEXT_DOCUMENT_OPEN_FAILED,
      TEXT_DOCUMENT_WRITE_FAILED,
      TEXT_DOCUMENT_FLUSH_FAILED,
      TEXT_DOCUMENT_CLOSE_FAILED
    };

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

    /** Validate and stage the complete editor document before filesystem work,
        then prepare and destructively truncate/write/flush/close. The caller
        retains the list unchanged. Zero rows serialize like one empty row.
        Reports the first observable seam/stdio failure; Retro68 CRT errors
        discarded inside that runtime cannot be recovered here. */
    TextDocumentResult
    WriteTextDocument(PlatformContext *context, const loka::file::File &file, const TextBuffer &buffer);
  } // namespace app
} // namespace loka
#endif
