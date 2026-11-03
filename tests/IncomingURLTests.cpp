#include "IncomingURLTests.hpp"

#include "app/url/IncomingURL.hpp"
#include "app/url/WebURL.hpp"
#include "support/TestVerify.hpp"

#include <string>

namespace
{
  using loka::app::IncomingURL;
  using loka::app::URLScheme;
  using loka::app::WebURL;
  using loka::core::String;

  URLScheme myappScheme()
  {
    URLScheme scheme;
    LOKA_VERIFY(URLScheme::parse("myapp", scheme));
    return scheme;
  }

  void verifyIncomingRefused(const String &text, const URLScheme &scheme)
  {
    IncomingURL out;
    LOKA_VERIFY(IncomingURL::parse(String("myapp:seed"), myappScheme(), out));
    const IncomingURL before(out);
    const bool accepted = IncomingURL::parse(text, scheme, out);
    LOKA_VERIFY(out == before);
    LOKA_VERIFY(!accepted);
  }
} // namespace

void testURLSchemeParsing()
{
  URLScheme scheme;
  LOKA_VERIFY(scheme.isNone());
  LOKA_VERIFY(scheme.name().empty());
  const char *const inputs[] = {"myapp", "x-loka.v2+a", "MyApp", "Z", "A0+-."};
  const char *const expected[] = {"myapp", "x-loka.v2+a", "myapp", "z", "a0+-."};
  for (std::size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
  {
    LOKA_VERIFY(URLScheme::parse(inputs[i], scheme));
    LOKA_VERIFY(!scheme.isNone());
    LOKA_VERIFY(scheme.name().equals(String(expected[i])));
  }
  const char *const refused[] = {0, "", "1abc", "+a", "a b", "a:b", "\xc3\xa9", "a/", "a_", "a%20"};
  for (std::size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
  {
    const String before = scheme.name();
    LOKA_VERIFY(!URLScheme::parse(refused[i], scheme));
    LOKA_VERIFY(scheme.name().equals(before));
  }
  // Scheme declarations have no independent length limit.
  const std::string longName(2048, 'A');
  LOKA_VERIFY(URLScheme::parse(longName.c_str(), scheme));
  LOKA_VERIFY(scheme.name().equals(String(std::string(2048, 'a'))));
  verifyIncomingRefused(String(longName + ":x"), scheme);
  verifyIncomingRefused(String("myapp:x"), URLScheme());
}

void testIncomingURLAcceptedAndPreserved()
{
  const URLScheme scheme = myappScheme();
  const char *const inputs[] = {
      "myapp:x", "myapp:/x", "myapp:?q", "myapp:#f", "MyApp:x", "MYAPP:x", "MyApp:Card/%2f/../X", "myapp:%2F/%252F/.."};
  const char *const expected[] = {
      "myapp:x", "myapp:/x", "myapp:?q", "myapp:#f", "myapp:x", "myapp:x", "myapp:Card/%2f/../X", "myapp:%2F/%252F/.."};
  for (std::size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
  {
    IncomingURL out;
    LOKA_VERIFY(IncomingURL::parse(String(inputs[i]), scheme, out));
    LOKA_VERIFY(!out.isNone());
    LOKA_VERIFY(out.text().equals(String(expected[i])));
    LOKA_VERIFY(IncomingURL::parse(out.text(), scheme, out));
    LOKA_VERIFY(out.text().equals(String(expected[i])));
  }
  IncomingURL out;
  LOKA_VERIFY(IncomingURL::parse(String("MyApp:Card/%2f/../X"), scheme, out));
  LOKA_VERIFY(out.rest().equals(String("Card/%2f/../X")));
  URLScheme extended;
  LOKA_VERIFY(URLScheme::parse("x-loka.v2+a", extended));
  LOKA_VERIFY(IncomingURL::parse(String("X-LOKA.V2+A:x"), extended, out));
  LOKA_VERIFY(out.text().equals(String("x-loka.v2+a:x")));
}

void testIncomingURLRefusalPreservesOutput()
{
  const URLScheme scheme = myappScheme();
  const char *const refused[] = {"other:x",  "myap:x",    "myapps:x", "myapp",    "myapp:",     ":x",
                                 "",         "myapp:%G0", "myapp:%",  "myapp:%4", "myapp:%4G",  "myapp:x%",
                                 "myapp: x", "myapp:\"",  "myapp:<",  "myapp:>",  "myapp:\\",   "myapp:^",
                                 "myapp:`",  "myapp:{",   "myapp:|",  "myapp:}",  "myapp:\x7f", "myapp:\xc3\xa9"};
  for (std::size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    verifyIncomingRefused(String(refused[i]), scheme);
}

void testIncomingURLLexicalAgreement()
{
  const URLScheme scheme = myappScheme();
  const char *const fragments[] = {"x",        "-._~:/?#[]@!$&'()*+,;=",
                                   "%00",      "%2f/%2F/../X",
                                   "%252F",    " ",
                                   "\"",       "<",
                                   ">",        "\\",
                                   "^",        "`",
                                   "{",        "|",
                                   "}",        "\x7f",
                                   "\xc3\xa9", "%",
                                   "%4",       "%4G",
                                   "%G0",      "x%",
                                   "\t"};
  for (std::size_t i = 0; i < sizeof(fragments) / sizeof(fragments[0]); ++i)
  {
    WebURL web;
    IncomingURL incoming;
    LOKA_VERIFY(WebURL::parse(String(std::string("https://a/") + fragments[i]), web)
                == IncomingURL::parse(String(std::string("myapp:") + fragments[i]), scheme, incoming));
  }
  const std::string longestFragment(2038, 'x');
  WebURL web;
  IncomingURL incoming;
  LOKA_VERIFY(WebURL::parse(String(std::string("https://a/") + longestFragment), web));
  LOKA_VERIFY(IncomingURL::parse(String(std::string("myapp:") + longestFragment), scheme, incoming));
  const char fragment[] = "a\0b";
  const std::string bytes(fragment, sizeof(fragment) - 1);
  LOKA_VERIFY(WebURL::parse(String(std::string("https://a/") + bytes), web)
              == IncomingURL::parse(String(std::string("myapp:") + bytes), scheme, incoming));
}

void testIncomingURLEmbeddedNul()
{
  const char bytes[] = "myapp:a\0b";
  verifyIncomingRefused(String::Utf8(bytes, sizeof(bytes) - 1), myappScheme());
}

void testIncomingURLLengthBoundary()
{
  const URLScheme scheme = myappScheme();
  std::string text("myapp:");
  text.append(2048 - text.size(), 'x');
  IncomingURL out;
  LOKA_VERIFY(IncomingURL::parse(String(text), scheme, out));
  LOKA_VERIFY(out.text().equals(String(text)));
  text += 'x';
  verifyIncomingRefused(String(text), scheme);
}

void testIncomingURLNoneAndEquality()
{
  const IncomingURL none;
  LOKA_VERIFY(none.isNone());
  LOKA_VERIFY(none.text().empty());
  LOKA_VERIFY(none.rest().empty());
  LOKA_VERIFY(!none.restStartsWith(""));
  LOKA_VERIFY(!none.restStartsWith("x"));
  LOKA_VERIFY(!none.restStartsWith(0));
  LOKA_VERIFY(none == IncomingURL::None());
  const URLScheme scheme = myappScheme();
  IncomingURL first;
  IncomingURL same;
  IncomingURL different;
  LOKA_VERIFY(IncomingURL::parse(String("MyApp:x"), scheme, first));
  LOKA_VERIFY(IncomingURL::parse(String("myapp:x"), scheme, same));
  LOKA_VERIFY(IncomingURL::parse(String("myapp:y"), scheme, different));
  LOKA_VERIFY(first == same);
  LOKA_VERIFY(!(first != same));
  LOKA_VERIFY(first != different);
  LOKA_VERIFY(!(first == different));
  LOKA_VERIFY(first != none);
  const IncomingURL copy(first);
  IncomingURL assigned;
  assigned = first;
  LOKA_VERIFY(copy == first && assigned == first);
  assigned = IncomingURL::None();
  LOKA_VERIFY(assigned.isNone() && !first.isNone());
}

void testIncomingURLRestStartsWith()
{
  IncomingURL out;
  LOKA_VERIFY(IncomingURL::parse(String("myapp:Card/X"), myappScheme(), out));
  LOKA_VERIFY(out.restStartsWith("Card/"));
  LOKA_VERIFY(!out.restStartsWith("card/"));
  LOKA_VERIFY(out.restStartsWith(""));
  LOKA_VERIFY(!out.restStartsWith(0));
  LOKA_VERIFY(out.restStartsWith("Card/X"));
  LOKA_VERIFY(!out.restStartsWith("Card/XX"));
  LOKA_VERIFY(out.rest().equals(String("Card/X")));
}
