#if defined(LOKA_RETRO68)
#include <Quickdraw.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <Carbon/Carbon.h>
#else
#error Native SDK include-order probe requires its target SDK
#endif
#include "app/doc/Heading.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/doc/CodeBlock.hpp"
#include "app/doc/Inline.hpp"

void documentVocabularyAfterSdk(loka::app::scene::NodeComposition &composition)
{
  using namespace loka::app;
  composition.declare(Column() << doc::Heading(1, "Guide")
                               << doc::Paragraph(doc::Link("Guide", "https://example.com/guide") + doc::Code("x"))
                               << doc::CodeBlock("one\ntwo"));
}
