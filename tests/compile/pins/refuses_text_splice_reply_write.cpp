#include "app/text/TextDocument.hpp"
void spliceReplyPin(loka::app::TextDocument &document)
{
  document.spliceReply()->set(loka::app::scene::Reply<loka::app::TextSplice>());
}
