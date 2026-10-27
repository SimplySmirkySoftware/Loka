#include "app/Focus.hpp"
#include "app/nodes/controls/TextEditorRequests.hpp"
using namespace loka::app;
using namespace loka::app::scene;
void pin()
{
  Focus<int> focus;
  Request<loka::app::detail::FocusTarget<int> > request;
  RequestDeclarationWall<loka::app::detail::FocusTarget<int> >::Wall wall;
  EditorResult (Reply<LineCursor>::*reason)() const = &Reply<LineCursor>::reason;
  (void)focus;
  (void)request;
  (void)wall;
  (void)reason;
}
