#include "app/text/TextReplacement.hpp"
void replacementStagePin()
{
  const loka::app::TextReplacement value = loka::app::TextReplacement::Empty();
  const loka::app::scene::Reply<loka::app::TextReplacement> reply =
      loka::app::scene::Reply<loka::app::TextReplacement>::Granted(value);
  const loka::app::TextReplacementReceipt &receipt = reply.applied();
  (void)receipt;
}
