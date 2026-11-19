#include "app/doc/Html.hpp"
#include "app/doc/Markdown.hpp"
bool second(std::string &out)
{
  return loka::app::doc::WriteMarkdown(0, out) || loka::app::doc::WriteHtml(0, out);
}
