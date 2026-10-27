#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/nodes/controls/TextEditor.hpp"
void check(loka::app::scene::Request<loka::app::LineCursor> &request)
{
  loka::app::scene::NodeState<loka::app::LineCursor> alias = request;
  (void)alias;
}
