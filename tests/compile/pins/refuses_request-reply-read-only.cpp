#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/nodes/controls/TextEditor.hpp"
void check(loka::app::scene::RequestWithReply<loka::app::LineCursor> &request)
{
  request.reply().set(loka::app::scene::Reply<loka::app::LineCursor>());
}
