#include "WebAttributedLayout.hpp"
#include "WebAttributedBounds.hpp"
#include "WebTextLayout.hpp"
#include "WebRangeWidthMemo.hpp"
#include "FakeBoundary.hpp"
#include "support/TestVerify.hpp"
#include "core/util/ScopedPtr.hpp"
#include "platform/String.hpp"
#include <climits>
#include <cstring>
#include <cstdio>
using namespace loka::app;
namespace
{
  typedef WebAttributedSnapshot Snapshot;
  typedef loka::core::ScopedPtr<Snapshot> Owner;
  /** Cost: test setup per font; creates a nine-word key, no source rows. */
  webtest::MetricKey Key(const TextStyle &style)
  {
    const WebTextDescriptor d = WebMetricDescriptor(WebFontKey(style));
    return webtest::MetricKey(d.data(), d.data() + 9);
  }
  /** Cost: test setup once per scenario; writes two fake metric rows. */
  void MixedMetrics()
  {
    webtest::descriptorMetrics[Key(TextStyle())] = std::make_pair(12, 2);
    webtest::descriptorMetrics[Key(Bold)] = std::make_pair(7, 9);
  }
  /** Cost: test per input; one direct build, no Scene. */
  Snapshot *Build(const AttributedString &value, const BlockStyle &block = BlockStyle(), short width = 100)
  {
    Snapshot *out = 0;
    LOKA_VERIFY(WebAttributedLayout::build(value, block, width, out) == WEB_MEASURE_OK);
    LOKA_VERIFY(out);
    return out;
  }
  /** Cost: assertion per snapshot; scans only the supplied completed payload. */
  void Packed(const Snapshot &s, const std::string &expected)
  {
    LOKA_VERIFY(s.byteLength() == expected.size());
    LOKA_VERIFY(!s.byteLength() || std::memcmp(s.bytes(), expected.data(), expected.size()) == 0);
    LOKA_VERIFY(s.words()[0] == 1);
    LOKA_VERIFY(s.wordCount() == 3 + 7 * s.fragmentCount() + 9 * s.descriptorCount());
    int cursor = 0;
    for (unsigned i = 0; i < s.fragmentCount(); ++i)
    {
      const Snapshot::Fragment &f = s.fragment(i);
      LOKA_VERIFY(f.byteStart == cursor && f.byteEnd > cursor);
      LOKA_VERIFY(f.left >= 0 && f.top >= 0 && f.width >= 0 && f.height > 0);
      LOKA_VERIFY(f.left + f.width <= SHRT_MAX && f.top + f.height <= SHRT_MAX);
      const int row[] = {f.descriptor, f.left, f.top, f.width, f.height, f.byteStart, f.byteEnd};
      LOKA_VERIFY(std::memcmp(row, s.words() + 3 + 7 * i, sizeof(row)) == 0);
      cursor = f.byteEnd;
    }
    LOKA_VERIFY(cursor == static_cast<int>(s.byteLength()));
    for (unsigned i = 0; i < s.descriptorCount(); ++i)
      LOKA_VERIFY(std::memcmp(s.descriptor(i).words, s.words() + 3 + 7 * s.fragmentCount() + 9 * i, 9 * sizeof(int))
                  == 0);
  }
  /** Cost: assertion per fragment; compares nine completed words against its declared style. */
  void Paint(const Snapshot &s, unsigned i, const TextStyle &style)
  {
    const WebTextDescriptor expected = WebMetricDescriptor(WebFontKey(style), style);
    LOKA_VERIFY(std::memcmp(s.descriptor(s.fragment(i).descriptor).words, expected.data(), 9 * sizeof(int)) == 0);
  }
  /** Cost: test group once; builds supplied mixed runs and checks local observations. */
  void GeometryAndPaint()
  {
    webtest::reset();
    MixedMetrics();
    const TextStyle styles[] = {TextStyle(), Bold};
    for (unsigned reverse = 0; reverse < 2; ++reverse)
    {
      Owner s(Build(Styled("a", styles[reverse]) + Styled("b", styles[1 - reverse])));
      Packed(*s, "ab");
      std::printf("mixed %u: H=%d tops=%d,%d heights=%d,%d\n",
                  reverse,
                  s->height(),
                  s->fragment(0).top,
                  s->fragment(1).top,
                  s->fragment(0).height,
                  s->fragment(1).height);
      LOKA_VERIFY(s->height() == 21 && s->fragmentCount() == 2);
      LOKA_VERIFY(s->fragment(reverse).top == 0 && s->fragment(1 - reverse).top == 5);
      LOKA_VERIFY(s->fragment(reverse).height == 14 && s->fragment(1 - reverse).height == 16);
      LOKA_VERIFY(s->fragment(0).descriptor == 0 && s->fragment(1).descriptor == 1);
      Paint(*s, 0, styles[reverse]);
      Paint(*s, 1, styles[1 - reverse]);
    }
    const TextStyle painted = TextStyle().underline(true).color(TEXT_COLOR_SECONDARY);
    Owner paint(Build(Styled("a", TextStyle()) + Styled("b", painted)));
    LOKA_VERIFY(paint->descriptorCount() == 2);
    Paint(*paint, 0, TextStyle());
    Paint(*paint, 1, painted);
    webtest::reset();
    MixedMetrics();
    Owner wrapped(Build(Styled("aa", TextStyle()) + Styled("bb", Bold), BlockStyle().wrap(TEXT_WRAP_CHAR), 21));
    LOKA_VERIFY(wrapped->height() == 37 && wrapped->fragmentCount() == 3);
    LOKA_VERIFY(wrapped->fragment(2).top == 21);
    // Every required metric precedes the first advance; no duplicate key fetch.
    bool advance = false;
    for (unsigned i = 0; i < webtest::measurementOrder.size(); ++i)
      if (webtest::measurementOrder[i] == "advance")
        advance = true;
      else
        LOKA_VERIFY(!advance);
    LOKA_VERIFY(webtest::metricCalls == 2);
    LOKA_VERIFY(webtest::metricDescriptors[0] != webtest::metricDescriptors[1]);
    for (int align = TEXT_ALIGN_LEFT; align <= TEXT_ALIGN_RIGHT; ++align)
      for (int offered = -1; offered <= 18; offered += offered < 1 ? 1 : 17)
      {
        Owner s(Build(
            Styled("ab", TextStyle()), BlockStyle().align(static_cast<TextAlign>(align)), static_cast<short>(offered)));
        const int spare = offered > 14 ? offered - 14 : 0;
        LOKA_VERIFY(s->fragment(0).left
                    == (align == TEXT_ALIGN_CENTER  ? spare / 2
                        : align == TEXT_ALIGN_RIGHT ? spare
                                                    : 0));
      }
    Owner odd(Build(Styled("ab", TextStyle()), BlockStyle().align(TEXT_ALIGN_CENTER), 19));
    LOKA_VERIFY(odd->fragment(0).left == 2);
    webtest::measuredWidth = 0;
    Owner zero(Build(Styled("ink", TextStyle())));
    Packed(*zero, "ink");
    LOKA_VERIFY(zero->fragmentCount() == 1 && zero->fragment(0).width == 0);
  }
  /** Cost: group once; direct builds of break/normalization fixtures only. */
  void BreaksAndPacking()
  {
    webtest::reset();
    MixedMetrics();
    const TextStyle styles[] = {TextStyle(), Bold};
    for (unsigned reverse = 0; reverse < 2; ++reverse)
    {
      Owner s(Build(Styled("a\r", styles[reverse]) + Styled("", Italic) + Styled("\nb", styles[1 - reverse])));
      Packed(*s, "ab");
      LOKA_VERIFY(s->fragmentCount() == 2);
      LOKA_VERIFY(s->height() == (reverse ? 35 : 37));
      LOKA_VERIFY(s->fragment(1).top == 21);
      LOKA_VERIFY(s->fragment(0).byteStart == 0 && s->fragment(0).byteEnd == 1);
      LOKA_VERIFY(s->fragment(1).byteStart == 1 && s->fragment(1).byteEnd == 2);
    }
    Owner multibyte(Build(Styled("\xe6\x97\xa5\r", TextStyle()) + Styled("\n\xf0\x9f\x98\x80", Bold)));
    Packed(*multibyte, "\xe6\x97\xa5\xf0\x9f\x98\x80");
    LOKA_VERIFY(multibyte->fragment(0).byteEnd == 3 && multibyte->fragment(1).byteStart == 3
                && multibyte->fragment(1).byteEnd == 7);
    Owner lone(Build(Styled("a\rb", TextStyle())));
    Packed(*lone, "ab");
    LOKA_VERIFY(lone->height() == 28);
    Owner lines(Build(Styled("\n\n", Bold)));
    Packed(*lines, "");
    LOKA_VERIFY(lines->height() == 48);
    Owner trailing(Build(Styled("a\n", Bold)));
    LOKA_VERIFY(trailing->height() == 32);
    Owner empty(Build(Styled("", Bold)));
    Packed(*empty, "");
    LOKA_VERIFY(empty->height() == 14);
    Owner absent(Build(AttributedString()));
    LOKA_VERIFY(absent->height() == 14);
    const BlockStyle ellipsis = BlockStyle().wrap(TEXT_WRAP_CHAR).truncation(TEXT_TRUNCATION_ELLIPSIS);
    const char *bodies[] = {"a\rb", "a\nb", "a\r\nb", "a\r", "a\n", "a\r\n"};
    for (unsigned i = 0; i < 6; ++i)
    {
      Owner s(Build(Styled(bodies[i], TextStyle()), ellipsis, 1));
      Packed(*s, "a");
      LOKA_VERIFY(s->height() == 14 && s->fragment(0).width == 7);
    }
    Owner leading(Build(Styled("\r", Bold) + Styled("\na", Bold), ellipsis));
    Packed(*leading, "");
    LOKA_VERIFY(leading->height() == 14);
    for (unsigned reverse = 0; reverse < 2; ++reverse)
    {
      Owner s(Build(Styled("a", styles[reverse]) + Styled("\r\nb", styles[1 - reverse]), ellipsis));
      Packed(*s, "a");
      LOKA_VERIFY(s->height() == (reverse ? 16 : 14));
    }
    Owner normalized(Build(Styled("\xe2\x82", TextStyle()) + Styled("\xac\t\f\xef\xbb\xbf\xf0\x9f\x98\x80", Bold)));
    Packed(*normalized, "\xef\xbf\xbd\xef\xbf\xbd  \xef\xbb\xbf\xf0\x9f\x98\x80");
    const char nul[] = {'a', 0, '\t', '\f', 'b'};
    Owner nulValue(Build(Styled(loka::core::String::Utf8(nul, sizeof(nul)), TextStyle())));
    Packed(*nulValue, "a\xef\xbf\xbd  b");
    Owner clip(Build(Styled("a\rb", TextStyle()), BlockStyle().truncation(TEXT_TRUNCATION_CLIP)));
    Packed(*clip, "ab");
    LOKA_VERIFY(clip->height() == 28);
    for (int align = TEXT_ALIGN_LEFT; align <= TEXT_ALIGN_RIGHT; ++align)
    {
      Owner aligned(
          Build(Styled("ab\nignored", TextStyle()), BlockStyle(ellipsis).align(static_cast<TextAlign>(align)), 19));
      Packed(*aligned, "ab");
      LOKA_VERIFY(aligned->fragment(0).left == (align == TEXT_ALIGN_CENTER ? 2 : align == TEXT_ALIGN_RIGHT ? 5 : 0));
    }
    Owner bom(Build(Styled("a\n", TextStyle()) + Styled("\xef\xbb\xbfZ", Bold)));
    Packed(*bom, "a\xef\xbb\xbfZ");
  }
  /** Cost: group once; local coalescing, range oracle, and memo identity fixtures. */
  void SpansAndReuse()
  {
    webtest::reset();
    const TextStyle plain;
    const LinkTarget link(loka::core::String::Literal("target"));
    webtest::rangeWidths[webtest::RangeKey(Key(plain), "a")] = 9;
    webtest::rangeWidths[webtest::RangeKey(Key(plain), "b")] = 8;
    webtest::rangeWidths[webtest::RangeKey(Key(plain), "ab")] = 11;
    // Positive control: standalone range is observably non-additive.
    int a = 0, b = 0, ab = 0;
    const WebTextDescriptor d = WebMetricDescriptor(WebFontKey(plain));
    LOKA_VERIFY(web_measure(d.data(), "a", 1, &a) == 0 && web_measure(d.data(), "b", 1, &b) == 0
                && web_measure(d.data(), "ab", 2, &ab) == 0);
    LOKA_VERIFY(a + b == 17 && ab == 11);
    const AttributedString value = Styled("a", plain) + Styled("", Bold) + Styled("b", plain);
    Owner s(Build(value));
    Packed(*s, "ab");
    LOKA_VERIFY(s->fragmentCount() == 1 && s->fragment(0).width == 11);
    LOKA_VERIFY(webtest::measuredText.back() == "ab");
    const unsigned calls = webtest::measureCalls;
    LOKA_VERIFY(s->matches(value, BlockStyle(), 100));
    LOKA_VERIFY(s->matches(Styled("a", plain) + Styled("", Bold) + Styled("b", plain), BlockStyle(), 100));
    LOKA_VERIFY(webtest::measureCalls == calls);
    LOKA_VERIFY(!s->matches(Styled("ab", plain), BlockStyle(), 100));
    LOKA_VERIFY(!s->matches(Styled("a", plain) + Styled("b", plain), BlockStyle(), 100));
    LOKA_VERIFY(!s->matches(value, BlockStyle(), 101));
    LOKA_VERIFY(!s->matches(value, BlockStyle().wrap(TEXT_WRAP_WORD), 100));
    LOKA_VERIFY(!s->matches(value + Underline, BlockStyle(), 100));
    LOKA_VERIFY(!s->matches(value + TextStyle().color(TEXT_COLOR_SECONDARY), BlockStyle(), 100));
    LOKA_VERIFY(!s->matches(Linked("a", plain, link) + Styled("", Bold) + Styled("b", plain), BlockStyle(), 100));
    Owner sameLink(Build(Linked("a", plain, link) + Styled("", Bold) + Linked("b", plain, link)));
    LOKA_VERIFY(sameLink->fragmentCount() == 1 && sameLink->fragment(0).width == 11);
    Owner links(Build(Styled("a", plain) + Linked("b", plain, link)));
    LOKA_VERIFY(links->fragmentCount() == 2 && links->descriptorCount() == 1);
    LOKA_VERIFY(links->fragment(0).width == 9 && links->fragment(1).width == 8);
    Owner empty(Build(AttributedString()));
    LOKA_VERIFY(empty->matches(AttributedString(), BlockStyle(), 100));
    LOKA_VERIFY(!empty->matches(Styled("", plain), BlockStyle(), 100));
    LOKA_VERIFY(!empty->matches(AttributedString(), BlockStyle().align(TEXT_ALIGN_RIGHT), 100));
    {
      const loka::core::LokaAllocationSite site("WebAttributedSnapshot", "MemoPin");
      WebRangeWidthMemo memo("ab", site);
      webtest::rangeWidths[webtest::RangeKey(Key(Bold), "ab")] = 19;
      int normal = 0, bold = 0, again = 0;
      const unsigned before = webtest::measureCalls;
      LOKA_VERIFY(memo.width(0, 2, WebFontKey(plain), normal));
      LOKA_VERIFY(memo.width(0, 2, WebFontKey(Bold), bold));
      LOKA_VERIFY(memo.width(0, 2, WebFontKey(plain), again));
      LOKA_VERIFY(normal == 11 && bold == 19 && again == 11 && webtest::measureCalls == before + 2);
    }
    // WORD lookahead crosses a style boundary: it must move the complete word.
    webtest::reset();
    Owner word(Build(Styled("x a", plain) + Styled("bc", Bold), BlockStyle().wrap(TEXT_WRAP_WORD), 28));
    Packed(*word, "x abc");
    LOKA_VERIFY(word->fragmentCount() == 3 && word->fragment(0).byteEnd == 2);
    LOKA_VERIFY(word->fragment(1).top == 13 && word->fragment(2).top == 13);
  }
  /** Cost: group once; compares both builders in their shared representable domain. */
  void TextEquivalence()
  {
    webtest::reset();
    const char *texts[] = {"abc", "a\r\nb\n", "\n", "a\tb\fc", "\xef\xbb\xbf\xf0\x9f\x98\x80", "\xe2\x82"};
    for (unsigned t = 0; t < 6; ++t)
      for (int wrap = TEXT_WRAP_NONE; wrap <= TEXT_WRAP_CHAR; ++wrap)
      {
        const BlockStyle block = BlockStyle().wrap(static_cast<TextWrap>(wrap));
        Owner attributed(Build(Styled(texts[t], Bold), block, 21));
        WebTextSnapshot *plain = 0;
        LOKA_VERIFY(
            WebTextLayout::build(
                loka::core::String::Literal(texts[t]), WebTextConstraint(WebFontKey(Bold), block, 21), false, plain)
            == WEB_MEASURE_OK);
        WebTextSlot owner;
        owner.replace(plain);
        LOKA_VERIFY(attributed->height() == plain->extent().height);
        std::string packed;
        for (unsigned i = 0; i < plain->joinedLength(); ++i)
          if (plain->joined()[i] != '\n')
            packed += plain->joined()[i];
        Packed(*attributed, packed);
        unsigned fragment = 0, start = 0, line = 0;
        for (unsigned i = 0; i <= plain->joinedLength(); ++i)
          if (i == plain->joinedLength() || plain->joined()[i] == '\n')
          {
            if (i != start)
            {
              LOKA_VERIFY(fragment < attributed->fragmentCount());
              const Snapshot::Fragment &f = attributed->fragment(fragment++);
              LOKA_VERIFY(f.top == static_cast<int>(line * 13) && f.width == static_cast<int>((i - start) * 7));
              LOKA_VERIFY(f.byteEnd - f.byteStart == static_cast<int>(i - start));
              LOKA_VERIFY(std::memcmp(attributed->bytes() + f.byteStart, plain->joined() + start, i - start) == 0);
            }
            ++line;
            start = i + 1;
          }
        LOKA_VERIFY(fragment == attributed->fragmentCount());
      }
  }
  /** Switchable read refusal, solely owned by this test's String handle.
      Cost: test read once per collection; writes its caller's scratch only. */
  class Reading : public loka::platform::String
  {
  public:
    bool refused;
    Reading()
        : refused(false)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      if (this->refused)
        return false;
      out += "read";
      return true;
    }
  };
  /** Cost: refusal assertion per attempt; snapshots only the fixture's completed value. */
  void Refuses(const AttributedString &value,
               const BlockStyle &block,
               short width,
               Snapshot *old,
               int status = WEB_MEASURE_REFUSED)
  {
    Snapshot *out = old;
    const std::vector<int> words(old->words(), old->words() + old->wordCount());
    const std::string bytes(old->byteLength() ? old->bytes() : "", old->byteLength());
    LOKA_VERIFY(WebAttributedLayout::build(value, block, width, out) == status);
    LOKA_VERIFY(out == old && words == std::vector<int>(old->words(), old->words() + old->wordCount()));
    Packed(*old, bytes);
  }
  /** Cost: group once; boundary arithmetic and geometry fixtures, no huge allocation. */
  void BoundsAndFailures()
  {
    webtest::reset();
    std::size_t out = 123;
    LOKA_VERIFY(web_attributed::add(INT32_MAX - 1, 1, out) && out == INT32_MAX);
    LOKA_VERIFY(!web_attributed::add(INT32_MAX, 1, out) && out == INT32_MAX);
    LOKA_VERIFY(!web_attributed::add(static_cast<std::size_t>(INT32_MAX) + 1, 0, out));
    LOKA_VERIFY(!web_attributed::allocation(static_cast<std::size_t>(INT32_MAX) + 1, 1));
    LOKA_VERIFY(web_attributed::allocation(INT32_MAX, sizeof(int))
                == (static_cast<std::size_t>(INT32_MAX) <= (std::numeric_limits<std::size_t>::max)() / sizeof(int)));
    // 3 + 7*306783370 + 9*6 == INT32_MAX.
    LOKA_VERIFY(web_attributed::wordCount(306783370, 6, out) && out == INT32_MAX);
    LOKA_VERIFY(!web_attributed::wordCount(306783374, 3, out));
    LOKA_VERIFY(!web_attributed::wordCount(INT32_MAX, 0, out));
    Owner old(Build(Styled("old", TextStyle())));
    const AttributedString one = Styled("a", TextStyle());
    webtest::measuredWidth = SHRT_MAX;
    Owner edge(Build(one));
    LOKA_VERIFY(edge->fragment(0).width == SHRT_MAX);
    webtest::measuredWidth = SHRT_MAX + 1;
    Refuses(one, BlockStyle(), 100, old.get());
    webtest::measuredWidth = 16384;
    Refuses(one + Styled("b", Bold), BlockStyle(), 0, old.get());
    webtest::measuredWidth = -1;
    webtest::metricAscent = 32765;
    webtest::metricDescent = 2;
    Owner tall(Build(one));
    LOKA_VERIFY(tall->height() == SHRT_MAX);
    Refuses(Styled("a\n", TextStyle()), BlockStyle(), 100, old.get());
    webtest::metricAscent = 16381;
    webtest::metricDescent = 3;
    Refuses(Styled("a\n", TextStyle()), BlockStyle(), 100, old.get());
    webtest::descriptorMetrics[Key(TextStyle())] = std::make_pair(16380, 3);
    webtest::descriptorMetrics[Key(Bold)] = std::make_pair(16381, 3);
    Owner sumEdge(Build(Styled("a\n", TextStyle()) + Styled("b", Bold)));
    LOKA_VERIFY(sumEdge->height() == SHRT_MAX);
    webtest::descriptorMetrics[Key(TextStyle())] = std::make_pair(32760, 1);
    webtest::descriptorMetrics[Key(Bold)] = std::make_pair(1, 8);
    Refuses(one + Styled("b", Bold), BlockStyle(), 100, old.get());
    webtest::descriptorMetrics[Key(Bold)] = std::make_pair(1, 7);
    Owner crossEdge(Build(one + Styled("b", Bold)));
    LOKA_VERIFY(crossEdge->height() == SHRT_MAX);
    webtest::reset();
    for (int metric = 0; metric < 2; ++metric)
    {
      if (metric)
        webtest::failMetrics = 1;
      else
        webtest::failAdvance = 1;
      Refuses(one, BlockStyle(), 100, old.get());
      Owner recovery(Build(one));
    }
#ifdef NDEBUG
    webtest::measureStatus = WEB_MEASURE_PROTOCOL;
    webtest::failMetrics = 1;
    Refuses(one, BlockStyle(), 100, old.get(), WEB_MEASURE_PROTOCOL);
    webtest::failAdvance = 1;
    Refuses(one, BlockStyle(), 100, old.get(), WEB_MEASURE_PROTOCOL);
#else
    std::puts("[skip] attributed release PROTOCOL: debug death entry tested separately");
#endif
    webtest::reset();
    Reading *reading = new Reading;
    const loka::core::String text =
        loka::core::String::FromPlatform(loka::core::Managed<loka::platform::String>::Wrap(reading));
    const AttributedString readable = Styled(text, TextStyle());
    Owner accepted(Build(readable));
    reading->refused = true;
    LOKA_VERIFY(accepted->matches(readable, BlockStyle(), 100));
    Refuses(readable, BlockStyle(), 100, old.get());
    reading->refused = false;
    Owner recovered(Build(readable));
    Packed(*recovered, "read");
  }
  int failAt = 0, allocations = 0, live = 0;
  std::vector<std::string> sites;
  /** Cost: test allocation backend per allocation; observes only this balanced build scope. */
  void *Allocate(std::size_t n, const loka::core::LokaAllocationSite &site)
  {
    ++allocations;
    sites.push_back(std::string(site.ownerTag) + "/" + site.typeTag);
    if (failAt && allocations == failAt)
      return 0;
    void *p = ::operator new(n, std::nothrow);
    if (p)
      ++live;
    return p;
  }
  /** Cost: test backend per release; releases only its allocation, O(1). */
  void Free(void *p, const loka::core::LokaAllocationSite &)
  {
    --live;
    ::operator delete(p);
  }
  /** Cost: group once; injects every observed allocation in a >32-line build. */
  void AllocationFailures()
  {
    webtest::reset();
    std::string body;
    for (unsigned i = 0; i < 40; ++i)
      body += "abcdef\n";
    const AttributedString value = Styled(loka::core::String::Literal(body.c_str()), TextStyle());
    Owner old(Build(Styled("old", TextStyle())));
    int stages = 0;
    loka::core::LokaAllocSetBackend(&Allocate, &Free);
    {
      Owner s(Build(value));
      std::printf("allocation fixture H=%d bytes=%u fragments=%u\n", s->height(), s->byteLength(), s->fragmentCount());
      LOKA_VERIFY(s->height() == 533 && s->byteLength() == 240 && s->fragmentCount() == 40);
      std::string packed;
      for (unsigned i = 0; i < 40; ++i)
        packed += "abcdef";
      Packed(*s, packed);
    }
    stages = allocations;
    LOKA_VERIFY(live == 0);
    loka::core::LokaAllocSetBackend(0, 0);
    LOKA_VERIFY(sites.back() == "WebAttributedSnapshot/Snapshot");
    unsigned breaker = 0, buffers = 0;
    for (unsigned i = 0; i < sites.size(); ++i)
    {
      if (sites[i] == "TextLineBreaker/Table")
        ++breaker;
      if (sites[i] == "WebAttributedSnapshot/Buffer")
        ++buffers;
    }
    LOKA_VERIFY(breaker == 2 && buffers > 10);
    for (int stage = 1; stage <= stages; ++stage)
    {
      allocations = 0;
      failAt = stage;
      loka::core::LokaAllocSetBackend(&Allocate, &Free);
      Refuses(value, BlockStyle(), 100, old.get());
      {
        Owner retry(Build(value));
        LOKA_VERIFY(retry->height() == 533);
      }
      LOKA_VERIFY(live == 0);
      loka::core::LokaAllocSetBackend(0, 0);
    }
    failAt = 0;
    std::printf("attributed allocation stages: %d (breaker=%u, buffers=%u)\n", stages, breaker, buffers);
    // A refused Builder yields an invalid value, not a successful empty snapshot.
    allocations = 0;
    failAt = 1;
    loka::core::LokaAllocSetBackend(&Allocate, &Free);
    {
      AttributedString::Builder builder(1);
      const AttributedString invalid = builder.build();
      LOKA_VERIFY(!invalid.valid());
      Refuses(invalid, BlockStyle(), 100, old.get());
    }
    LOKA_VERIFY(live == 0);
    loka::core::LokaAllocSetBackend(0, 0);
    failAt = 0;
  }
} // namespace
/** Cost: native driver once; runs only direct builder fixtures. */
void WebAttributedBuildTests()
{
  GeometryAndPaint();
  BreaksAndPacking();
  SpansAndReuse();
  TextEquivalence();
  BoundsAndFailures();
  AllocationFailures();
  std::puts("Attributed build geometry/packing/reuse/refusal pins passed");
}
/** Cost: debug death subprocess once; one direct build with a protocol refusal. */
void WebAttributedProtocolDeath(bool advance)
{
  webtest::reset();
  webtest::measureStatus = WEB_MEASURE_PROTOCOL;
  if (advance)
    webtest::failAdvance = 1;
  else
    webtest::failMetrics = 1;
  Snapshot *out = 0;
  (void)WebAttributedLayout::build(Styled("a", TextStyle()), BlockStyle(), 100, out);
  delete out;
}
