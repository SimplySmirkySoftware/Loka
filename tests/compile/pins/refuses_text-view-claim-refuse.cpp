#include "app/text/TextDocumentViewContext.hpp"
#include "app/text/TextDocumentViewAccess.hpp"
using namespace loka::app;
void probe(scene::TextViewClaim &claim,
           scene::TextStoreRow &row,
           TextDocument &document,
           scene::TextDocumentViewContext &context)
{
  (void)claim;
  (void)row;
  (void)document;
  (void)context;
  claim.refuse(TextViewAttachment::VIEW_REFUSED_UNAVAILABLE);
}
