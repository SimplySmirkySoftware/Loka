#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/nodes/controls/TextEditor.hpp"
void check(loka::app::scene::Request<loka::app::LineCursor> &request)
{
  request.set(loka::app::LineCursor::None());
}
