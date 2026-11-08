#ifndef LOKA_APP_TEXT_READER_HPP
#define LOKA_APP_TEXT_READER_HPP
#include "app/text/TextDocument.hpp"
#include "app/scene/MemberList.hpp"
namespace loka
{
  namespace app
  {
    /** Each successful read returns bytes; completion and refusal return zero bytes. */
    enum TextReadResult
    {
      TEXT_READ_MORE,
      TEXT_READ_END,
      TEXT_READ_REFUSED
    };
    /** The first refusal reason is retained for the reader's remaining lifetime. */
    enum TextReadRefusal
    {
      TEXT_READ_NOT_READY,
      TEXT_READ_MALFORMED,
      TEXT_READ_RESOURCE,
      TEXT_READ_RANGE
    };
    /** One reader phase, including its sticky refusal reason. */
    class TextReadPhase
    {
    public:
      enum Stage
      {
        OPEN,
        ENDED,
        REFUSED
      };
      TextReadPhase()
          : stage_(OPEN),
            reason_(TEXT_READ_NOT_READY)
      {
      }
      Stage stage() const
      {
        return this->stage_;
      }
      TextReadRefusal refusal() const
      {
        return this->reason_;
      }
      void end()
      {
        this->stage_ = ENDED;
      }
      void refuse(TextReadRefusal reason)
      {
        if (this->stage_ == REFUSED)
          return;
        this->stage_ = REFUSED;
        this->reason_ = reason;
      }

    private:
      Stage stage_;
      TextReadRefusal reason_;
    };
    /** Synchronous pull of a revision's LF-normalized UTF-8 body. Do not pump
        while open. Holds no native pointer and refuses after either owner disappears. */
    class TextReader
    {
    public:
      explicit TextReader(const TextDocument &document);
      TextReader(const TextDocument &document, const TextPosition &from, const TextPosition &to);
      ~TextReader()
      {
        this->membership_.leave();
      }
      TextReadResult read(char *out, unsigned long capacity, unsigned long &n);
      TextReadRefusal refusal() const
      {
        return this->phase_.refusal();
      }

    private:
      TextReader(const TextReader &);
      TextReader &operator=(const TextReader &);
      void attach(const TextDocument &document);
      scene::MemberHook<TextReader> membership_;
      unsigned long nativeOffset_, nativeEnd_, revision_;
      TextReadPhase phase_;
      friend struct testing::TextDocumentAccess;
    };
  } // namespace app
} // namespace loka
#endif
