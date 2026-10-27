#include "app/nodes/controls/TextEditorRequests.hpp"
#include "reported_declaration.hpp"
void probe(loka::app::scene::Reported<loka::app::LineCursor> &reported,
           loka::app::TextEditorProps &props,
           loka::app::scene::Request<loka::app::LineCursor> &request)
{
  (void)reported;
  (void)props;
  (void)request;
  (void)reported.state()->get();
  props.moveCaretTo(request);
  request.set(loka::app::LineCursor::None());
}
