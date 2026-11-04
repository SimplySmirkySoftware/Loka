#include "app/text/TextDocumentValues.hpp"
using namespace loka::app;
using namespace loka::app::scene;
void pin()
{
  TextPosition p = TextPosition::None();
  TextPosition q(p);
  (void)(p != q);
  Request<TextPosition> request;
  (void)request;
}
