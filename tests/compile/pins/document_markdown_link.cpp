#include "app/doc/Markdown.hpp"
#include "app/doc/Html.hpp"
bool second(std::string &);
int main()
{
  std::string out;
  return loka::app::doc::WriteMarkdown(0, out) || loka::app::doc::WriteHtml(0, out) || second(out);
}
