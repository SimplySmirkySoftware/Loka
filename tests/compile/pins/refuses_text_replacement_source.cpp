#include "app/text/TextReplacement.hpp"
void replacementSourcePin()
{
  loka::app::TextReplacement out;
  loka::core::resource::Blob blob;
  loka::app::TextReplacement::FromUtf8(blob, loka::file::File(), loka::app::TextPlacement::Start(), out);
}
