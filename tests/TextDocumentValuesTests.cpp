#include "TextDocumentValuesTests.hpp"
#include "testing/app/TextPositionTestAccess.hpp"
#include "support/Headless.hpp"
#include "support/TestVerify.hpp"
#include <climits>

using namespace loka::app;
using namespace loka::app::scene;
using loka::app::testing::TextDocumentFactTestAccess;
using loka::app::testing::TextPositionTestAccess;

void testTextPositionIdentity()
{
  const TextPosition none;
  const TextPosition copy(none);
  LOKA_VERIFY(none.isNone() && copy.isNone());
  LOKA_VERIFY(none == TextPosition::None() && copy == none);
  LOKA_VERIFY(!(none != copy));
  const TextPosition zero = TextPositionTestAccess::make(0, 0);
  LOKA_VERIFY(!zero.isNone() && zero != none && none != zero);
  const TextPosition position = TextPositionTestAccess::make(12, 34);
  const TextPosition copied(position);
  LOKA_VERIFY(position == copied && !(position != copied));
  LOKA_VERIFY(TextPositionTestAccess::revision(copied) == 12);
  LOKA_VERIFY(TextPositionTestAccess::offset(copied) == 34);
  LOKA_VERIFY(position != TextPositionTestAccess::make(13, 34));
  LOKA_VERIFY(position != TextPositionTestAccess::make(12, 35));
  const TextPosition maximum = TextPositionTestAccess::make(ULONG_MAX, ULONG_MAX);
  LOKA_VERIFY(!maximum.isNone());
  LOKA_VERIFY(TextPositionTestAccess::revision(maximum) == ULONG_MAX);
  LOKA_VERIFY(TextPositionTestAccess::offset(maximum) == ULONG_MAX);
}

void testTextPlacementEquality()
{
  const TextPlacement start = TextPlacement::Start();
  const TextPlacement end = TextPlacement::End();
  const TextPlacement at = TextPlacement::AtUtf8Offset(17);
  const TextPlacement copy(at);
  LOKA_VERIFY(start.kind() == TextPlacement::START && start.utf8Offset() == 0);
  LOKA_VERIFY(end.kind() == TextPlacement::END && end.utf8Offset() == 0);
  LOKA_VERIFY(at.kind() == TextPlacement::AT_UTF8_OFFSET && at.utf8Offset() == 17);
  LOKA_VERIFY(start == TextPlacement::Start() && end == TextPlacement::End());
  LOKA_VERIFY(at == copy && !(at != copy));
  LOKA_VERIFY(start != end && start != TextPlacement::AtUtf8Offset(0));
  LOKA_VERIFY(end != TextPlacement::AtUtf8Offset(0));
  LOKA_VERIFY(at != TextPlacement::AtUtf8Offset(18));
}

void testTextDocumentFactValues()
{
  const TextDocumentFact initial;
  LOKA_VERIFY(initial.state() == TEXT_DOCUMENT_AWAITING_STORE);
  LOKA_VERIFY(initial.textRevision() == 0);
  LOKA_VERIFY(initial.caret().isNone() && initial.selectionAnchor().isNone());
  LOKA_VERIFY(initial == TextDocumentFact() && !(initial != TextDocumentFact()));
  TextChangedRange out(99, 98, 97);
  LOKA_VERIFY(!initial.queryChangedRange(out));
  LOKA_VERIFY(out == TextChangedRange(99, 98, 97));
  const TextPosition caret = TextPositionTestAccess::make(4, 10);
  const TextPosition anchor = TextPositionTestAccess::make(4, 3);
  const TextChangedRange range(3, 7, 8);
  const TextDocumentFact fact = TextDocumentFactTestAccess::make(TEXT_DOCUMENT_READY, 4, caret, anchor, &range);
  const TextDocumentFact copy(fact);
  LOKA_VERIFY(fact == copy && !(fact != copy));
  LOKA_VERIFY(fact.state() == TEXT_DOCUMENT_READY && fact.textRevision() == 4);
  LOKA_VERIFY(fact.caret() == caret && fact.selectionAnchor() == anchor);
  LOKA_VERIFY(fact.queryChangedRange(out));
  LOKA_VERIFY(out.start() == 3 && out.length() == 7 && out.insertedLength() == 8);
  LOKA_VERIFY(fact != TextDocumentFactTestAccess::make(TEXT_DOCUMENT_MARKED, 4, caret, anchor, &range));
  LOKA_VERIFY(fact != TextDocumentFactTestAccess::make(TEXT_DOCUMENT_UNAVAILABLE, 4, caret, anchor, &range));
  LOKA_VERIFY(fact != TextDocumentFactTestAccess::make(TEXT_DOCUMENT_AWAITING_STORE, 4, caret, anchor, &range));
  LOKA_VERIFY(fact != TextDocumentFactTestAccess::make(TEXT_DOCUMENT_READY, 5, caret, anchor, &range));
  LOKA_VERIFY(fact != TextDocumentFactTestAccess::make(TEXT_DOCUMENT_READY, 4, anchor, anchor, &range));
  LOKA_VERIFY(fact != TextDocumentFactTestAccess::make(TEXT_DOCUMENT_READY, 4, caret, caret, &range));
  const TextChangedRange variants[] = {TextChangedRange(4, 7, 8), TextChangedRange(3, 8, 8), TextChangedRange(3, 7, 9)};
  for (unsigned i = 0; i < 3; ++i)
  {
    LOKA_VERIFY(range != variants[i]);
    LOKA_VERIFY(fact != TextDocumentFactTestAccess::make(TEXT_DOCUMENT_READY, 4, caret, anchor, &variants[i]));
  }
  LOKA_VERIFY(fact != TextDocumentFactTestAccess::make(TEXT_DOCUMENT_READY, 4, caret, anchor));
  const TextChangedRange empty;
  const TextDocumentFact reportedEmpty = TextDocumentFactTestAccess::make(
      TEXT_DOCUMENT_AWAITING_STORE, 0, TextPosition::None(), TextPosition::None(), &empty);
  LOKA_VERIFY(reportedEmpty != initial);
  LOKA_VERIFY(reportedEmpty.queryChangedRange(out) && out == empty);
}

namespace
{
  class PositionRequestOwner : public HeadlessStateOwner
  {
  public:
    Request<TextPosition> request;
    PositionRequestOwner()
    {
      StateBatchBase::CreateImmediateState(this, this->request, TextPosition::None());
    }
  };
} // namespace

void testTextPositionRequest()
{
  PositionRequestOwner owner;
  LOKA_VERIFY(owner.request.isValid() && owner.request.get().isNone());
  const TextPosition first = TextPositionTestAccess::make(1, 2);
  const TextPosition next = TextPositionTestAccess::make(2, 2);
  {
    loka::core::StateTrackerGuard guard(owner.tracker());
    owner.request.set(first);
  }
  LOKA_VERIFY(owner.request.get() == first);
  {
    loka::core::StateTrackerGuard guard(owner.tracker());
    owner.request.set(next);
  }
  LOKA_VERIFY(owner.request.get() == next);
  LOKA_VERIFY(RequestTraits<TextPosition>::coalescable == 1);
  LOKA_VERIFY(RequestTraits<TextPosition>::accepted() == TEXT_REQUEST_OK);
  LOKA_VERIFY(RequestTraits<TextPosition>::unavailable() == TEXT_REQUEST_UNAVAILABLE);
  const Reply<TextPosition> stale = Reply<TextPosition>::Refused(first, TEXT_REQUEST_STALE);
  LOKA_VERIFY(stale.reason() == TEXT_REQUEST_STALE && stale.requested() == first);
}
