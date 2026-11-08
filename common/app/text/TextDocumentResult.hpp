#ifndef LOKA_APP_TEXT_DOCUMENT_RESULT_HPP
#define LOKA_APP_TEXT_DOCUMENT_RESULT_HPP
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

  } // namespace app
} // namespace loka
#endif
