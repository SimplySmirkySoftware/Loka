#include "app/text/TextDocument.hpp"
void replacementReplyPin(loka::app::TextDocument &document)
{
  document.replaceReply()->set(loka::app::scene::Reply<loka::app::TextReplacement>());
}
