#include "app/text/TextDocument.hpp"
void spliceReplyPin(loka::app::TextDocument &document)
{
  const loka::app::scene::Reply<loka::app::TextSplice> &reply = document.spliceReply()->get();
  (void)reply;
}
