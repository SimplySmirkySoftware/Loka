#include "app/doc/List.hpp"
#include "app/doc/Quote.hpp"
#include "app/doc/Html.hpp"
#include "app/doc/Markdown.hpp"
bool second(std::string &out)
{
  using namespace loka::app;
  doc::Quote quote;
  quote << (doc::List() << doc::Item("pin"));
  if (quote.childrenCount() != 2)
    return true;
  return loka::app::doc::WriteMarkdown(0, out) || loka::app::doc::WriteHtml(0, out);
}
