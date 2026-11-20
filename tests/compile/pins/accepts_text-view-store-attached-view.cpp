#include "app/text/TextViewClaim.hpp"
using namespace loka::app::scene;
class RailStore : public TextStoreRow
{
public:
  RailStore(TextStoreLedger &owner, IPlatformController &controller)
      : TextStoreRow(owner, controller)
  {
  }
  TextDocumentViewContext *view() const
  {
    return this->attachedView();
  }
};
