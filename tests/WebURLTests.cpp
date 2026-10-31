#include "WebURLTests.hpp"

#include "app/url/WebURL.hpp"
#include "support/TestVerify.hpp"

#include <string>

namespace
{
  using loka::app::WebURL;
  using loka::core::String;

  void verifyRefused(const String &input)
  {
    WebURL out;
    LOKA_VERIFY(WebURL::parse(String("https://seed.example/"), out));
    const WebURL before(out);
    const bool accepted = WebURL::parse(input, out);
    LOKA_VERIFY(out == before);
    LOKA_VERIFY(!accepted);
  }
} // namespace

void testWebURLAcceptedAndPreserved()
{
  const char *const inputs[] = {"HTTPS://Example.com/",
                                "http://a/%41",
                                "http://a/../b",
                                "http://a:8080/",
                                "http://[::1]/",
                                "mailto:a@b.c",
                                "mailto:?subject=x",
                                "HTTP://Ex%41mple.COM/A?B#C",
                                "http://Ex%41mple.COM/A?B#C",
                                "HtTpS://a/",
                                "MaIlTo:a@b.c",
                                "http://a",
                                "https://a?q",
                                "https://a#f",
                                "http://user:pass@a:port/",
                                "http://a/-._~:/?#[]@!$&'()*+,;=%00%aF%Ff"};
  const char *const expected[] = {"https://Example.com/",
                                  "http://a/%41",
                                  "http://a/../b",
                                  "http://a:8080/",
                                  "http://[::1]/",
                                  "mailto:a@b.c",
                                  "mailto:?subject=x",
                                  "http://Ex%41mple.COM/A?B#C",
                                  "http://Ex%41mple.COM/A?B#C",
                                  "https://a/",
                                  "mailto:a@b.c",
                                  "http://a",
                                  "https://a?q",
                                  "https://a#f",
                                  "http://user:pass@a:port/",
                                  "http://a/-._~:/?#[]@!$&'()*+,;=%00%aF%Ff"};
  for (std::size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
  {
    WebURL out;
    LOKA_VERIFY(WebURL::parse(String(inputs[i]), out));
    LOKA_VERIFY(!out.isNone());
    LOKA_VERIFY(out.text().equals(String(expected[i])));
    LOKA_VERIFY(WebURL::parse(out.text(), out));
    LOKA_VERIFY(out.text().equals(String(expected[i])));
  }
}

void testWebURLRefusalPreservesOutput()
{
  const char *const refused[] = {"",
                                 "calc.exe",
                                 "C:\\x",
                                 "file:///",
                                 "javascript:alert(1)",
                                 "https:x",
                                 "http:",
                                 "https://",
                                 "https:///x",
                                 "https://?q",
                                 "https://#f",
                                 "mailto:",
                                 "https://a\\b",
                                 "https://a/\"",
                                 "https://a/ b",
                                 "https://a/\t",
                                 "https://a/%G0",
                                 "https://a/%0G",
                                 "https://a/%4",
                                 "https://a/%",
                                 "httpx://a",
                                 "mailtx:a",
                                 "http",
                                 "https",
                                 "mailto",
                                 "https:/a"};
  for (std::size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    verifyRefused(String(refused[i]));

  // Every forbidden ASCII byte, including embedded NUL and DEL, is load-bearing.
  for (int byte = 0; byte <= 127; ++byte)
  {
    const bool forbidden = byte <= 32 || byte == 127 || byte == '\\' || byte == '"' || byte == '<' || byte == '>'
                           || byte == '{' || byte == '}' || byte == '|' || byte == '^' || byte == '`';
    if (forbidden)
    {
      std::string input("https://a/");
      input += static_cast<char>(byte);
      input += 'x';
      verifyRefused(String::Utf8(input.data(), input.size()));
    }
  }
  const char nonAscii[] = "https://a/\xc2\x80";
  verifyRefused(String::Utf8(nonAscii, sizeof(nonAscii) - 1));
}

void testWebURLLengthBoundary()
{
  std::string text("http://a/");
  text.append(WebURL::MAX_TEXT_BYTES - text.size(), 'x');
  WebURL out;
  LOKA_VERIFY(WebURL::parse(String(text), out));
  LOKA_VERIFY(out.text().equals(String(text)));
  text += 'x';
  verifyRefused(String(text));
}

void testWebURLNoneAndEquality()
{
  const WebURL none;
  LOKA_VERIFY(none.isNone());
  LOKA_VERIFY(none.text().empty());
  LOKA_VERIFY(WebURL::None().isNone());
  LOKA_VERIFY(WebURL::None().text().empty());
  LOKA_VERIFY(none == WebURL::None());
  WebURL first;
  WebURL same;
  WebURL different;
  LOKA_VERIFY(WebURL::parse(String("HTTP://a/A"), first));
  LOKA_VERIFY(WebURL::parse(String("http://a/A"), same));
  LOKA_VERIFY(WebURL::parse(String("http://a/a"), different));
  LOKA_VERIFY(first == same);
  LOKA_VERIFY(!(first != same));
  LOKA_VERIFY(first != different);
  LOKA_VERIFY(!(first == different));
  LOKA_VERIFY(first != none);
  const WebURL copy(first);
  WebURL assigned;
  assigned = first;
  LOKA_VERIFY(copy == first && assigned == first);
  assigned = WebURL::None();
  LOKA_VERIFY(assigned.isNone() && !first.isNone());
}
