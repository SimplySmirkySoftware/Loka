#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/nodes/controls/TextEditor.hpp"
void check(loka::app::TextEditorProps &props,
           loka::app::scene::NodeState<loka::app::LineCursor> &bare,
           loka::app::scene::Request<loka::app::LineCursor> &request)
{
  props.moveCaretTo(bare);
  (void)bare;
  (void)request;
}
