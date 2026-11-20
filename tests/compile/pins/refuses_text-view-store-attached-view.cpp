#include "app/text/TextViewClaim.hpp"
using namespace loka::app::scene;
TextDocumentViewContext *view(TextStoreRow &row)
{
  return row.attachedView();
}
