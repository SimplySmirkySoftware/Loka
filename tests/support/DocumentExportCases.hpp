#ifndef LOKA_TESTS_SUPPORT_DOCUMENT_EXPORT_CASES_HPP
#define LOKA_TESTS_SUPPORT_DOCUMENT_EXPORT_CASES_HPP

#include "support/DocumentFixture.hpp"

namespace DocumentExportCases
{
  /** Each scene observation has an exact spelling for both exits. */
  typedef void (*Check)(loka::app::scene::Node *, const char *markdown, const char *html);
  /** Cost: each exit mounts one pruning fixture and checks the whole tree and one subtree. */
  void walk(Check check);
  /** Cost: each exit visits the selected rows once after each fixture transition. */
  void seats(Check check);
  /** Cost: each exit attempts one late export per invalid authored input. */
  void invalidInline(void (*refuse)(const loka::app::AttributedString &));

  /** Cost: the refusal fixtures wrap one test string per input. */
  loka::core::String raw(const std::string &bytes, bool readable = true);
} // namespace DocumentExportCases

#endif
