#include "TextSpliceTests.hpp"
#include "app/text/TextSplice.hpp"
#include "testing/app/TextPositionTestAccess.hpp"
#include "support/BlobAllocationProbe.hpp"
using namespace loka::app;
using namespace loka::app::scene;
using loka::app::testing::TextPositionTestAccess;
namespace
{
  TextPosition position(unsigned long offset)
  {
    return TextPositionTestAccess::make(7, offset);
  }
  TextSplice
  splice(const TextPosition &from, const TextPosition &to, const char *bytes, unsigned long n, TextPlacement placement)
  {
    TextSplice out;
    LOKA_VERIFY(TextSplice::FromUtf8(from, to, bytes, n, placement, out) == TEXT_DOCUMENT_OK);
    return out;
  }
} // namespace
void testTextSpliceFactories()
{
  BlobAllocationProbe allocation;
  const TextSplice none;
  LOKA_VERIFY(none.isNone() && none.stage() == TextSplice::NONE && !(none != TextSplice::None()));
  LOKA_VERIFY(none.from().isNone() && none.to().isNone());
  LOKA_VERIFY(none.inserted().stage() == TextReplacement::NONE);
  LOKA_VERIFY(none.placement() == TextPlacement::Start() && allocation.attempts == 0);
  const TextSplice deletion = splice(position(1), position(3), 0, 0, TextPlacement::End());
  LOKA_VERIFY(allocation.attempts == 0);
  LOKA_VERIFY(!deletion.isNone() && deletion.stage() == TextSplice::SPLICE);
  LOKA_VERIFY(deletion.inserted().stage() == TextReplacement::EMPTY && deletion.inserted().logicalLength() == 0);
  LOKA_VERIFY(deletion.from() == position(1) && deletion.to() == position(3));
  LOKA_VERIFY(deletion.placement() == deletion.inserted().placement() && deletion.placement() == TextPlacement::End());
  char bytes[] = "a\r\nb";
  const TextSplice insertion = splice(position(1), position(1), bytes, 4, TextPlacement::AtUtf8Offset(2));
  bytes[0] = 'z';
  LOKA_VERIFY(insertion.inserted().stage() == TextReplacement::BYTES);
  LOKA_VERIFY(insertion.inserted().bytes().isCompleted());
  LOKA_VERIFY(insertion.inserted().bytes().size() == 4);
  LOKA_VERIFY(std::memcmp(insertion.inserted().bytes().data(), "a\r\nb", 4) == 0);
  LOKA_VERIFY(insertion.inserted().logicalLength() == 3);
  LOKA_VERIFY(insertion.from() == position(1) && insertion.to() == position(1));
  LOKA_VERIFY(insertion.placement() == TextPlacement::AtUtf8Offset(2));
  LOKA_VERIFY(insertion.placement() == insertion.inserted().placement());
  const TextSplice range = splice(position(1), position(3), "a\r\nb", 4, TextPlacement::End());
  LOKA_VERIFY(range.from() == position(1) && range.to() == position(3));
  const int attempts = allocation.attempts;
  TextSplice copy(range);
  LOKA_VERIFY(!(copy != range));
  copy = insertion;
  LOKA_VERIFY(!(copy != insertion) && allocation.attempts == attempts);
  LOKA_VERIFY(range != splice(position(1), position(3), "a\r\nb", 4, TextPlacement::End()));
  LOKA_VERIFY(none != deletion);
  LOKA_VERIFY(deletion != splice(position(0), position(3), 0, 0, TextPlacement::End()));
  LOKA_VERIFY(deletion != splice(position(1), position(4), 0, 0, TextPlacement::End()));
  LOKA_VERIFY(deletion != splice(position(1), position(3), 0, 0, TextPlacement::Start()));
  LOKA_VERIFY(RequestTraits<TextSplice>::coalescable == 0);
  LOKA_VERIFY(RequestTraits<TextSplice>::accepted() == TEXT_SPLICE_OK);
  LOKA_VERIFY(RequestTraits<TextSplice>::unavailable() == TEXT_SPLICE_UNAVAILABLE);
}
void testTextSpliceRefusals()
{
  BlobAllocationProbe allocation;
  TextSplice out = splice(position(1), position(3), "old", 3, TextPlacement::End());
  const TextSplice before(out);
  const TextPosition from[] = {TextPosition::None(), position(1), position(1), position(3)};
  const TextPosition to[] = {position(3), TextPosition::None(), TextPositionTestAccess::make(8, 3), position(1)};
  const int attempts = allocation.attempts;
  for (unsigned i = 0; i < 4; ++i)
  {
    LOKA_VERIFY(TextSplice::FromUtf8(from[i], to[i], "x", 1, TextPlacement::Start(), out)
                == TEXT_DOCUMENT_PREPARE_FAILED);
    LOKA_VERIFY(!(out != before));
  }
  LOKA_VERIFY(TextSplice::FromUtf8(position(1), position(3), 0, 1, TextPlacement::Start(), out)
              == TEXT_DOCUMENT_PREPARE_FAILED);
  LOKA_VERIFY(!(out != before));
  LOKA_VERIFY(TextSplice::FromUtf8(position(1), position(1), 0, 0, TextPlacement::Start(), out)
              == TEXT_DOCUMENT_PREPARE_FAILED);
  LOKA_VERIFY(!(out != before));
  LOKA_VERIFY(TextSplice::FromUtf8(position(1), position(1), "", 0, TextPlacement::Start(), out)
              == TEXT_DOCUMENT_PREPARE_FAILED);
  LOKA_VERIFY(!(out != before) && allocation.attempts == attempts);
  const char *invalid[] = {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"};
  for (unsigned i = 0; i < 4; ++i)
  {
    const unsigned long length = static_cast<unsigned long>(std::strlen(invalid[i]));
    LOKA_VERIFY(TextSplice::FromUtf8(position(1), position(3), invalid[i], length, TextPlacement::Start(), out)
                == TEXT_DOCUMENT_NOT_TEXT);
    LOKA_VERIFY(!(out != before));
  }
  // Allocation refusal precedes UTF-8 validation; a refused attempt owns no scratch.
  allocation.refuse("Blob", "Record");
  LOKA_VERIFY(TextSplice::FromUtf8(position(1), position(3), "\xc0\xaf", 2, TextPlacement::Start(), out)
              == TEXT_DOCUMENT_ALLOCATION);
  LOKA_VERIFY(!(out != before));
  const char *owners[] = {"Blob", "Managed", "Blob"};
  const char *types[] = {"Record", "ControlBlock", "Bytes"};
  for (unsigned i = 0; i < 3; ++i)
  {
    const int live = allocation.live;
    const int refusals = allocation.refusals;
    allocation.refuse(owners[i], types[i]);
    LOKA_VERIFY(TextSplice::FromUtf8(position(1), position(3), "x", 1, TextPlacement::Start(), out)
                == TEXT_DOCUMENT_ALLOCATION);
    LOKA_VERIFY(!(out != before));
    LOKA_VERIFY(allocation.live == live && allocation.refusals == refusals + 1);
  }
}
void testTextSpliceReceipt()
{
  BlobAllocationProbe allocation;
  Reply<TextSplice> reply;
  const TextSpliceReceipt empty;
  LOKA_VERIFY(empty.logicalLength() == 0 && empty.placement() == TextPlacement::Start());
  {
    const TextSplice value = splice(position(1), position(3), "a\r\nb", 4, TextPlacement::End());
    LOKA_VERIFY(allocation.live > 0);
    reply = Reply<TextSplice>::Granted(value);
  }
  LOKA_VERIFY(allocation.live == 0 && allocation.byteFrees == 1);
  const TextSpliceReceipt receipt = reply.applied();
  LOKA_VERIFY(receipt.logicalLength() == 3 && receipt.placement() == TextPlacement::End());
  const TextSpliceReceipt copy(receipt);
  LOKA_VERIFY(!(copy != receipt) && empty != receipt);
  const TextSpliceReceipt changedPlacement(splice(position(1), position(3), "abc", 3, TextPlacement::Start()));
  const TextSpliceReceipt changedLength(splice(position(1), position(3), "ab", 2, TextPlacement::End()));
  LOKA_VERIFY(changedPlacement != receipt && changedLength != receipt);
}
