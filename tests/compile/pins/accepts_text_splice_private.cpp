#include "app/text/TextDocument.hpp"
void splicePin(loka::app::TextDocument &document, const loka::app::TextSplice &value)
{
  document.splice(value);
}
