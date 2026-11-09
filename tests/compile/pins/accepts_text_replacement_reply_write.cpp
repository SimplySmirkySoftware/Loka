#include "app/text/TextDocument.hpp"
void replacementReplyPin(loka::app::TextDocument &document)
{
  const loka::app::scene::Reply<loka::app::TextReplacement> &reply = document.replaceReply()->get();
  (void)reply;
}
