#include "../tools/site/SiteWriter.hpp"
#include "GetStarted.hpp"
#include "Home.hpp"
#include <csignal>

/** Cost: one invocation renders both authored pages and writes the site once. */
int main(int argc, char **argv)
{
  using loka::app::scene::Boundary;
  std::signal(SIGXFSZ, SIG_IGN);
  if (argc != 3)
  {
    std::fputs("Usage: LokaSite <out-dir> <stylesheet-file>\n", stderr);
    return 2;
  }
  document_site::SiteOutputs outputs;
  return document_site::addPage(outputs, "index.html", "Loka", Boundary<loka_site::Home>())
                 && document_site::addPage(
                     outputs, "start.html", "Get started with Loka", Boundary<loka_site::GetStarted>())
                 && document_site::writeSite(argv[1], argv[2], outputs)
             ? 0
             : 1;
}
