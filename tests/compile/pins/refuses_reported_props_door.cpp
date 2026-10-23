#include "support/TextBufferFixture.hpp"
#include "reported_declaration.hpp"
void probe(loka::testing::TextBufferFixture &lines, loka::app::scene::Reported<loka::app::LineCursor> &reported)
{
  (void)lines;
  (void)reported;
  (void)loka::app::scene::NodePropsBase<loka::app::TextEditorProps>::reportSeat(reported);
}
