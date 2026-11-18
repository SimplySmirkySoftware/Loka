#include "app/doc/Markdown.hpp"
bool second(std::string &);
int main()
{
  std::string out;
  return loka::app::doc::WriteMarkdown(0, out) || second(out);
}
