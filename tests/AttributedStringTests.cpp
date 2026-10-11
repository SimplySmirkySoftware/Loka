#include "AttributedStringTests.hpp"

// Include State first to pin equality lookup without an app include in core.
#include "core/State.hpp"
#include "app/style/AttributedString.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/String.hpp"
#include "core/StringAccess.hpp"
#include <cstdio>
#include "support/LokaAllocFailure.hpp"
#include "support/TestVerify.hpp"
#if defined(TEST_BUILD) && defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace
{
  class AllocationFailures
  {
  public:
    AllocationFailures()
    {
      loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 0);
    }
    ~AllocationFailures()
    {
      LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
      loka::core::testing::allowLokaAllocRaw();
    }

  private:
    AllocationFailures(const AllocationFailures &);
    AllocationFailures &operator=(const AllocationFailures &);
  };

  class RefusedString : public loka::platform::String
  {
  public:
    explicit RefusedString(int *calls)
        : calls_(calls)
    {
    }
    virtual bool appendUtf8(std::string &) const
    {
      ++*this->calls_;
      return false;
    }

  private:
    int *calls_;
  };

  class CollectedString : public loka::platform::String
  {
  public:
    CollectedString(const std::string &bytes, int *calls)
        : bytes_(bytes),
          calls_(calls)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      ++*this->calls_;
      out.append(this->bytes_);
      return true;
    }

  protected:
    const std::string bytes_;

  private:
    int *calls_;
  };

  class ViewedString : public CollectedString
  {
  public:
    ViewedString(const std::string &bytes, int *calls)
        : CollectedString(bytes, calls)
    {
    }
    virtual bool queryUtf8(loka::platform::Utf8View &out) const
    {
      out.bytes = this->bytes_.empty() ? 0 : this->bytes_.data();
      out.length = this->bytes_.size();
      return true;
    }
  };

  loka::core::String countedString(const std::string &bytes, bool view, int &calls)
  {
    loka::platform::String *text = view ? new ViewedString(bytes, &calls)
                                       : new CollectedString(bytes, &calls);
    return loka::core::String::FromPlatform(loka::core::Managed<loka::platform::String>::Wrap(text));
  }

  void countChange(void *context)
  {
    ++*static_cast<int *>(context);
  }
  void releaseInt(int *, void *context)
  {
    ++*static_cast<int *>(context);
  }
} // namespace

void testAttributedStringContentEquality()
{
  using namespace loka::app;
  const AttributedString joined = Styled("ab", Bold);
  const AttributedString split = Styled("a", Bold) + Styled("b", Bold);
  LOKA_VERIFY(joined.equals(split));
  LOKA_VERIFY(split == joined);
  LOKA_VERIFY(!(split != joined));
  LOKA_VERIFY(joined.compare(split) == 0);
  LOKA_VERIFY(split.segmentCount() == 2);
  LOKA_VERIFY(split.segment(0).text.equals(loka::core::String("a")));
  LOKA_VERIFY(split.segment(1).text.equals(loka::core::String("b")));
  LOKA_VERIFY(joined != (Styled("a", Bold) + Styled("b", Italic)));
  LOKA_VERIFY(joined != Styled("ac", Bold));
  LOKA_VERIFY(joined != Styled("a", Bold));
  LOKA_VERIFY(joined == (Styled("", Italic) + split + Styled("", FontSize<24>())));
  LOKA_VERIFY((Styled("a", Bold) + Styled("bc", Bold) + Styled("d", Italic))
              == (Styled("ab", Bold) + Styled("c", Bold) + Styled("d", Italic)));
  const char bytes[] = {'a', '\0', 'b'};
  const loka::core::String binary = loka::core::String::Utf8(bytes, sizeof(bytes));
  LOKA_VERIFY(Styled(binary, Bold) == (Styled(loka::core::String::Utf8(bytes, 2), Bold) + Styled("b", Bold)));
  // A combining sequence crosses a segment boundary; no shaping is done here.
  LOKA_VERIFY(Styled("a\xcc\x81", Bold) == (Styled("a", Bold) + Styled("\xcc\x81", Bold)));
}

void testAttributedStringOrdering()
{
  using namespace loka::app;
  const AttributedString values[] = {AttributedString(),
                                     Styled("a", TextStyle()),
                                     Styled("a", Bold),
                                     Styled("ab", Bold),
                                     Styled("b", TextStyle()),
                                     Styled("\xc3\xa9", Bold)};
  for (std::size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
  {
    LOKA_VERIFY(values[i].compare(values[i]) == 0);
    for (std::size_t j = i + 1; j < sizeof(values) / sizeof(values[0]); ++j)
    {
      LOKA_VERIFY(values[i].compare(values[j]) < 0);
      LOKA_VERIFY(values[j].compare(values[i]) > 0);
    }
  }
  LOKA_VERIFY((Styled("a", Bold) + Styled("b", Bold)).compare(Styled("ac", Bold)) < 0);
}

void testAttributedStringStyleDirections()
{
  using namespace loka::app;
  const AttributedString colored = Styled("a", Red) + Styled("b", TextStyle());
  LOKA_VERIFY(colored != (Styled("a", Blue) + Styled("b", TextStyle())));
  LOKA_VERIFY(colored.compare(Styled("a", Blue) + Styled("b", TextStyle())) < 0);
  LOKA_VERIFY((Blue + colored).segment(0).style == Red);
  LOKA_VERIFY((Blue + colored).segment(1).style == Blue);
  LOKA_VERIFY((colored + Blue).segment(0).style == Blue);
  LOKA_VERIFY((colored + Blue).segment(1).style == Blue);
  const AttributedString small = Styled("a", FontSize<12>());
  LOKA_VERIFY((small + FontSize<24>()) == Styled("a", FontSize<24>()));
  LOKA_VERIFY((FontSize<24>() + small) == Styled("a", FontSize<12>()));
  const AttributedString source = small + Styled("b", Italic);
  const AttributedString defaults = (Bold + FontSize<24>()) + source;
  const AttributedString overrides = source + (Bold + FontSize<24>());
  LOKA_VERIFY(defaults.segment(0).style == (Bold + FontSize<12>()));
  LOKA_VERIFY(defaults.segment(1).style == (Bold + FontSize<24>() + Italic));
  LOKA_VERIFY(overrides.segment(0).style == (Bold + FontSize<24>()));
  LOKA_VERIFY(overrides.segment(1).style == (Bold + FontSize<24>() + Italic));
  LOKA_VERIFY(source.segment(0).style == FontSize<12>());
  LOKA_VERIFY(source.segment(1).style == Italic);
  LOKA_VERIFY((TextStyle() + source) == source);
  LOKA_VERIFY((source + TextStyle()) == source);
  LOKA_VERIFY((Styled("a", Bold) + TextStyle().weight(TEXT_WEIGHT_NORMAL))
              == Styled("a", TextStyle().weight(TEXT_WEIGHT_NORMAL)));
}

void testAttributedStringEmptyAndInvalid()
{
  using namespace loka::app;
  const loka::core::String text("a");
  const loka::core::String emptyText("");
  AllocationFailures failures;
  const AttributedString empty;
  LOKA_VERIFY(empty.valid());
  LOKA_VERIFY(empty.empty());
  LOKA_VERIFY(empty.segmentCount() == 0);
  LOKA_VERIFY(Styled(emptyText, Bold).empty());
  LOKA_VERIFY(Styled(loka::core::String(), Bold) == empty);
  LOKA_VERIFY((empty + Bold) == empty);
  LOKA_VERIFY((Bold + empty) == empty);
  LOKA_VERIFY((empty + empty) == empty);
  LOKA_VERIFY(!Styled(text, Bold).empty());
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  const AttributedString invalid = Styled(text, Bold);
  LOKA_VERIFY(!invalid.valid());
  LOKA_VERIFY(invalid.empty());
  LOKA_VERIFY(invalid != empty);
  LOKA_VERIFY(invalid.compare(empty) < 0);
  LOKA_VERIFY(empty.compare(invalid) > 0);
  LOKA_VERIFY(!(invalid + empty).valid());
  LOKA_VERIFY(!(empty + invalid).valid());
  LOKA_VERIFY(!(invalid + Bold).valid());
  LOKA_VERIFY(!(Bold + invalid).valid());
}

void testAttributedStringStateContentEquality()
{
  using namespace loka::app;
  loka::core::PushStateTracker tracker;
  loka::core::MutableState<AttributedString> state(Styled("ab", Bold));
  tracker.addState(&state);
  int changes = 0;
  state.bind(&countChange, &changes, false);
  {
    loka::core::StateTrackerGuard guard(&tracker);
    state.set(Styled("a", Bold) + Styled("b", Bold));
  }
  LOKA_VERIFY(changes == 0);
  LOKA_VERIFY(state.get().segmentCount() == 1);
  {
    loka::core::StateTrackerGuard guard(&tracker);
    state.set(Styled("a", Bold) + Styled("b", Italic));
  }
  LOKA_VERIFY(changes == 1);
  LOKA_VERIFY(state.get().segmentCount() == 2);
  state.unbind(&countChange, &changes);
  tracker.removeState(&state);
}

void testAttributedStringAllocationFailures()
{
  using namespace loka::app;
  using namespace loka::core::testing;
  const loka::core::String text("a");
  AllocationFailures failures;
  const AttributedString original = Styled(text, Bold);
  LOKA_VERIFY(lokaAllocRawAttempts() == 2);
  LOKA_VERIFY(lokaAllocRawLive() == 2);
  {
    const AttributedString copy = original;
    LOKA_VERIFY(copy == original);
    LOKA_VERIFY(lokaAllocRawLive() == 2);
    LOKA_VERIFY(lokaAllocRawAttempts() == 2);
  }
  const loka::core::LokaAllocationSite sites[] = {
      loka::core::LokaAllocationSite("AttributedString", "Segments"),
      loka::core::ManagedControlBlockSite(),
  };
  for (int site = 0; site < 2; ++site)
  {
    for (int operation = 0; operation < 4; ++operation)
    {
      for (int attempt = 0; attempt < 2; ++attempt)
      {
        failLokaAllocRaw(sites[site].ownerTag, sites[site].typeTag, site + 1);
        {
          // The first Managed allocation succeeds; only the selected nth one refuses.
          int payload = 7;
          const loka::core::Managed<int> preceding = loka::core::Managed<int>::Wrap(&payload, 0);
          LOKA_VERIFY(preceding.isValid());
        }
        const AttributedString result = operation == 0   ? Styled(text, Italic)
                                        : operation == 1 ? original + original
                                        : operation == 2 ? Italic + original
                                                         : original + Italic;
        LOKA_VERIFY(!result.valid());
        LOKA_VERIFY(result.empty());
        LOKA_VERIFY(lokaAllocRawLive() == 2);
        LOKA_VERIFY(lokaAllocRawAttempts() == site + 2);
      }
      LOKA_VERIFY(original.valid());
      LOKA_VERIFY(original.segment(0).style == Bold);
      LOKA_VERIFY(Styled(text, Bold) == original);
    }
  }
}

void testAttributedStringRefusedBuffersAndSharedFastPath()
{
  using namespace loka::app;
  int calls = 0;
  const loka::core::String refused =
      loka::core::String::FromPlatform(loka::core::Managed<loka::platform::String>::Wrap(new RefusedString(&calls)));
  const AttributedString left = Styled(refused, Bold);
  const AttributedString shared = left;
  LOKA_VERIFY(left == shared);
  LOKA_VERIFY(left.compare(shared) == 0);
  LOKA_VERIFY(calls == 0);
  const AttributedString rebuilt = Styled(refused, Bold);
  LOKA_VERIFY(left != rebuilt);
  LOKA_VERIFY(calls > 0);
  LOKA_VERIFY(left.compare(rebuilt) == -rebuilt.compare(left));
  LOKA_VERIFY(!left.empty());
  LOKA_VERIFY(left != AttributedString());
  LOKA_VERIFY(left.compare(Styled("a", Bold)) < 0);
  LOKA_VERIFY((Styled("a", Bold) + left) != Styled("a", Bold));
}

void testManagedTryWrapFailureAndRelease()
{
  using namespace loka::core;
#if defined(TEST_BUILD) && defined(__linux__) && !defined(__SANITIZE_ADDRESS__)
  // Like the existing death pins, require SIGABRT rather than any crash.
  const pid_t child = fork();
  LOKA_VERIFY(child >= 0);
  if (child == 0)
  {
    int payload = 7;
    int releases = 0;
    testing::failLokaAllocRaw("Managed", "ControlBlock", 1);
    Managed<int>::Wrap(&payload, &releaseInt, &releases);
    _exit(0);
  }
  int status = 0;
  LOKA_VERIFY(waitpid(child, &status, 0) == child);
  LOKA_VERIFY(WIFSIGNALED(status));
  LOKA_VERIFY(WTERMSIG(status) == SIGABRT);
#else
  std::printf("[skip] Managed Wrap refusal death pin requires TEST_BUILD Linux without ASan.\n");
#endif
  AllocationFailures failures;
  const LokaAllocationSite site = ManagedControlBlockSite();
  int payload = 7;
  int releases = 0;
  testing::failLokaAllocRaw(site.ownerTag, site.typeTag, 1);
  Managed<int> refused = Managed<int>::TryWrap(&payload, &releaseInt, &releases);
  LOKA_VERIFY(!refused.isValid());
  LOKA_VERIFY(releases == 0);
  LOKA_VERIFY(testing::lokaAllocRawLive() == 0);
  {
    Managed<int> owner = Managed<int>::TryWrap(&payload, &releaseInt, &releases);
    LOKA_VERIFY(owner.isValid());
    LOKA_VERIFY(testing::lokaAllocRawLive() == 1);
    Managed<int> copy = owner;
    owner.reset();
    LOKA_VERIFY(releases == 0);
    LOKA_VERIFY(*copy == 7);
    Managed<int> assigned;
    assigned = copy;
    copy.reset();
    LOKA_VERIFY(releases == 0);
    LOKA_VERIFY(*assigned == 7);
  }
  LOKA_VERIFY(releases == 1);
  LOKA_VERIFY(testing::lokaAllocRawLive() == 0);
  LOKA_VERIFY(!Managed<int>::TryWrap(0, &releaseInt, &releases).isValid());
  testing::failLokaAllocRaw(site.ownerTag, site.typeTag, 2);
  {
    Managed<int> legacy = Managed<int>::Wrap(&payload, &releaseInt, &releases);
    LOKA_VERIFY(legacy.isValid());
    LOKA_VERIFY(testing::lokaAllocRawLive() == 1);
  }
  LOKA_VERIFY(releases == 2);
  LOKA_VERIFY(testing::lokaAllocRawLive() == 0);
  LOKA_VERIFY(!Managed<int>::TryWrap(&payload, &releaseInt, &releases).isValid());
  LOKA_VERIFY(releases == 2);
  testing::failLokaAllocRaw("Managed", "ControlBlock", 1);
  LOKA_VERIFY(!Managed<int>::Wrap(0).isValid());
  LOKA_VERIFY(testing::lokaAllocRawAttempts() == 0);
}

void testAttributedStringBuilderAllocations()
{
  using namespace loka::app;
  using namespace loka::core::testing;
  const loka::core::String text[4] = {
      loka::core::String("a"), loka::core::String("b"), loka::core::String("c"), loka::core::String("d")};
  AllocationFailures failures;
  {
    AttributedString::Builder builder(4);
    for (int i = 0; i < 4; ++i)
      LOKA_VERIFY(builder.append(text[i], Bold));
    const AttributedString value = builder.build();
    LOKA_VERIFY(value.valid());
    LOKA_VERIFY(value.segmentCount() == 4);
    LOKA_VERIFY(lokaAllocRawAttempts() == 2);
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
  failLokaAllocRaw("AttributedString", "Segments", 0);
  {
    const AttributedString chain =
        Styled(text[0], Bold) + Styled(text[1], Bold) + Styled(text[2], Bold) + Styled(text[3], Bold);
    LOKA_VERIFY(chain.segmentCount() == 4);
    LOKA_VERIFY(lokaAllocRawAttempts() == 4 * 4 - 2);
  }
}

void testAttributedStringBuilderGrowth()
{
  using namespace loka::app;
  using namespace loka::core::testing;
  loka::core::String text[10];
  for (int i = 0; i < 10; ++i)
    text[i] = loka::core::String::FromInt(i);
  AllocationFailures failures;
  {
    AttributedString::Builder builder(1);
    for (int i = 0; i < 10; ++i)
      LOKA_VERIFY(builder.append(text[i], i % 2 ? Italic : Bold));
    const AttributedString value = builder.build();
    // Capacities 1, 2, 4, 8, 16, each with one array and one control block.
    LOKA_VERIFY(lokaAllocRawAttempts() == 10);
    LOKA_VERIFY(lokaAllocRawLive() == 2);
    LOKA_VERIFY(value.valid());
    LOKA_VERIFY(value.segmentCount() == 10);
    for (int i = 0; i < 10; ++i)
    {
      LOKA_VERIFY(value.segment(i).text.equals(text[i]));
      LOKA_VERIFY(value.segment(i).style == (i % 2 ? Italic : Bold));
    }
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
}

void testAttributedStringBuilderRefusal()
{
  using namespace loka::app;
  using namespace loka::core::testing;
  const loka::core::String first("a");
  const loka::core::String second("b");
  AllocationFailures failures;
  const loka::core::LokaAllocationSite sites[] = {
      loka::core::LokaAllocationSite("AttributedString", "Segments"),
      loka::core::ManagedControlBlockSite(),
  };
  for (int site = 0; site < 2; ++site)
  {
    for (int retry = 0; retry < 2; ++retry)
    {
      {
        AttributedString::Builder builder(1);
        LOKA_VERIFY(builder.append(first, Bold));
        failLokaAllocRaw(sites[site].ownerTag, sites[site].typeTag, 1);
        LOKA_VERIFY(!builder.append(second, Italic));
        LOKA_VERIFY(lokaAllocRawAttempts() == site + 1);
        LOKA_VERIFY(lokaAllocRawLive() == 2);
        if (retry)
        {
          // Keep the backend installed while its storage is live; allow() requires zero live.
          failLokaAllocRaw("AttributedString", "Segments", 0);
          LOKA_VERIFY(builder.append(second, Italic));
        }
        const AttributedString value = builder.build();
        LOKA_VERIFY(value.valid());
        LOKA_VERIFY(value.segmentCount() == (retry ? 2u : 1u));
        LOKA_VERIFY(value.segment(0).text.equals(first));
        LOKA_VERIFY(value.segment(0).style == Bold);
        if (retry)
        {
          LOKA_VERIFY(value.segment(1).text.equals(second));
          LOKA_VERIFY(value.segment(1).style == Italic);
        }
      }
      LOKA_VERIFY(lokaAllocRawLive() == 0);
      allowLokaAllocRaw();
      failLokaAllocRaw("AttributedString", "Segments", 0);
    }
    failLokaAllocRaw(sites[site].ownerTag, sites[site].typeTag, 1);
    {
      AttributedString::Builder refused(1);
      LOKA_VERIFY(!refused.append(first, Bold));
      LOKA_VERIFY(!refused.build().valid());
    }
    LOKA_VERIFY(lokaAllocRawLive() == 0);
  }
}

void testAttributedStringBuilderLifetime()
{
  using namespace loka::app;
  using namespace loka::core::testing;
  const loka::core::String text("a");
  AllocationFailures failures;
  {
    AttributedString value;
    {
      AttributedString::Builder builder(4);
      LOKA_VERIFY(builder.append(text, Bold));
      value = builder.build();
      LOKA_VERIFY(!builder.build().valid());
      LOKA_VERIFY(!builder.append(text, Italic));
    }
    LOKA_VERIFY(value.valid());
    LOKA_VERIFY(value.segmentCount() == 1);
    LOKA_VERIFY(value.segment(0).text.equals(text));
    LOKA_VERIFY(value.segment(0).style == Bold);
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
  for (int hint = 0; hint < 3; ++hint)
  {
    AttributedString::Builder builder(hint);
    const AttributedString empty = builder.build();
    LOKA_VERIFY(empty.valid());
    LOKA_VERIFY(empty.empty());
    LOKA_VERIFY(empty.segmentCount() == 0);
    LOKA_VERIFY(empty.equals(AttributedString()));
    LOKA_VERIFY(!builder.build().valid());
    LOKA_VERIFY(!builder.append(text, Bold));
  }
  {
    AttributedString::Builder abandoned(4);
    LOKA_VERIFY(abandoned.append(text, Bold));
    LOKA_VERIFY(lokaAllocRawLive() == 2);
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
  {
    AttributedString::Builder zeroHint(0);
    LOKA_VERIFY(zeroHint.append(text, Bold));
    LOKA_VERIFY(zeroHint.append(text, Italic));
    LOKA_VERIFY(zeroHint.build().segmentCount() == 2);
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
}

void testAttributedStringBuilderEquality()
{
  using namespace loka::app;
  AttributedString::Builder builder(4);
  LOKA_VERIFY(builder.append(loka::core::String("ab"), Bold));
  LOKA_VERIFY(builder.append(loka::core::String(), Italic));
  LOKA_VERIFY(builder.append(loka::core::String("c"), Italic));
  const AttributedString value = builder.build();
  const AttributedString chain = Styled("a", Bold) + Styled("b", Bold) + Styled("c", Italic);
  LOKA_VERIFY(value.equals(chain));
  LOKA_VERIFY(value.compare(chain) == 0);
  LOKA_VERIFY(&value.segment(0) != &chain.segment(0));
}

void testAttributedStringUtf8Routes()
{
  using namespace loka::app;
  for (int route = 0; route < 4; ++route)
  {
    int calls = 0;
    const bool leftView = (route & 1) == 0;
    const bool rightView = (route & 2) == 0;
    const AttributedString left = Styled(countedString("ab", leftView, calls), Bold)
                                  + Styled(countedString("c", leftView, calls), Bold);
    const AttributedString right = Styled(countedString("a", rightView, calls), Bold)
                                   + Styled(countedString("bc", rightView, calls), Bold);
    LOKA_VERIFY(left.equals(right));
    const int expected = (leftView ? 0 : 2) + (rightView ? 0 : 2);
    std::fprintf(stderr, "UTF-8 route %d: collects=%d expected=%d\n", route, calls, expected);
    LOKA_VERIFY(calls == expected);
  }
}

void testAttributedStringUtf8Edges()
{
  using namespace loka::app;
  using loka::core::String;
  int calls = 0;
  const AttributedString split = Styled("a", Bold) + Styled("", Italic)
                                 + Styled(countedString("", true, calls), Italic)
                                 + Styled(String(), Italic) + Styled("b", Bold);
  LOKA_VERIFY(split.equals(Styled("ab", Bold)));
  const AttributedString mixed = Styled(countedString("a", false, calls), Bold)
                                 + Styled(countedString("b", true, calls), Bold)
                                 + Styled(countedString("c", false, calls), Bold);
  calls = 0;
  LOKA_VERIFY(mixed.equals(Styled(countedString("abc", true, calls), Bold)));
  LOKA_VERIFY(calls == 2);
  LOKA_VERIFY((Styled("a", Bold) + Styled("b", Bold)).compare(Styled("ab", Bold)) == 0);
  const char bytes[] = {'a', '\0', static_cast<char>(0xff)};
  const std::string binary(bytes, sizeof(bytes));
  const AttributedString view = Styled(countedString(binary, true, calls), Bold);
  const AttributedString collected = Styled(countedString(binary, false, calls), Bold);
  LOKA_VERIFY(view.compare(collected) == 0);
  LOKA_VERIFY(collected.compare(view) == 0);
  const AttributedString lower = Styled(countedString(std::string("a\0\x7f", 3), false, calls), Bold);
  LOKA_VERIFY(view.compare(lower) > 0);
  LOKA_VERIFY(collected.compare(lower) > 0);
  LOKA_VERIFY(lower.compare(view) < 0);
  // Bytes win over styles at the first differing position, including a boundary.
  LOKA_VERIFY((Styled("a", Bold) + Styled("b", Italic)).compare(Styled("ac", Bold)) < 0);
  LOKA_VERIFY((Styled("a", Bold) + Styled("b", Italic)).compare(Styled("ab", Bold)) != 0);

  loka::platform::Utf8View out = {bytes, sizeof(bytes)};
  CollectedString fallback("abc", &calls);
  LOKA_VERIFY(!fallback.queryUtf8(out));
  LOKA_VERIFY(out.bytes == bytes && out.length == sizeof(bytes));
  const String concat = String("a") + String("b");
  LOKA_VERIFY(!loka::core::StringAccess::handle(concat)->queryUtf8(out));
  LOKA_VERIFY(out.bytes == bytes && out.length == sizeof(bytes));
  const String native("abc");
#if defined(_WIN32) || defined(__APPLE__)
  std::printf("[skip] Verbatim UTF-8 factory query requires the Null or Toolbox rail.\n");
  LOKA_VERIFY(!loka::core::StringAccess::handle(native)->queryUtf8(out));
  LOKA_VERIFY(out.bytes == bytes && out.length == sizeof(bytes));
#else
  std::printf("[skip] Native declining factory query requires Win32 or macOS.\n");
  LOKA_VERIFY(loka::core::StringAccess::handle(native)->queryUtf8(out));
  LOKA_VERIFY(out.length == 3 && std::string(out.bytes, out.length) == "abc");
#endif
}

void testAttributedStringStyleBoundaries()
{
  using namespace loka::app;
  // check-attributed-string-style-cost.py counts comparisons at entry positions 0, 1, 2.
  const AttributedString left = Styled("ab", Bold) + Styled("cd", Bold);
  const AttributedString right = Styled("a", Bold) + Styled("", Italic) + Styled("bcd", Bold);
  LOKA_VERIFY(left.compare(right) == 0);
}
