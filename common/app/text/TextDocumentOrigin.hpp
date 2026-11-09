#ifndef LOKA_APP_TEXT_DOCUMENT_ORIGIN_HPP
#define LOKA_APP_TEXT_DOCUMENT_ORIGIN_HPP
#include "core/io/File.hpp"
namespace loka
{
  namespace app
  {
    /** Source and revision of a completed replacement, independent of later edits. */
    class TextDocumentOrigin
    {
    public:
      TextDocumentOrigin()
          : revision_(0)
      {
      }
      TextDocumentOrigin(const file::File &source, unsigned long revision)
          : source_(source),
            revision_(revision)
      {
      }
      bool querySource(file::File &out) const
      {
        if (this->source_.sharesRepresentation(file::File()))
          return false;
        out = this->source_;
        return true;
      }
      unsigned long revision() const
      {
        return this->revision_;
      }
      bool operator==(const TextDocumentOrigin &other) const
      {
        return this->revision_ == other.revision_ && this->source_.sharesRepresentation(other.source_);
      }
      bool operator!=(const TextDocumentOrigin &other) const
      {
        return !(*this == other);
      }

    private:
      file::File source_;
      unsigned long revision_;
    };
  } // namespace app
} // namespace loka
#endif
