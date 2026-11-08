#include "TextReplacementTests.hpp"
#include "app/text/TextStoreSource.hpp"
#include "support/BlobAllocationProbe.hpp"
#include "platform/file/FileLocatorAccess.hpp"
#include <string>
using namespace loka::app;
using namespace loka::app::scene;
using loka::core::resource::Blob;
using loka::file::File;
namespace
{
  Blob sealed(const char *text, std::size_t length)
  {
    Blob blob = Blob::Create();
    LOKA_VERIFY(blob.tryAssign(reinterpret_cast<const unsigned char *>(text), length));
    blob.sealBytes();
    return blob;
  }
  TextReplacement replacement(const char *text, std::size_t length, TextPlacement placement)
  {
    TextReplacement out;
    LOKA_VERIFY(TextReplacement::FromUtf8(sealed(text, length), placement, out) == TEXT_DOCUMENT_OK);
    return out;
  }
} // namespace
void testTextReplacementFactories()
{
  BlobAllocationProbe allocation;
  const int initial = allocation.attempts;
  const TextReplacement none;
  const TextReplacement empty = TextReplacement::Empty();
  LOKA_VERIFY(none.stage() == TextReplacement::NONE && empty.stage() == TextReplacement::EMPTY);
  LOKA_VERIFY(none.placement() == TextPlacement::Start() && empty.logicalLength() == 0);
  LOKA_VERIFY(allocation.attempts == initial);
  File sentinel("sentinel"), source(sentinel);
  LOKA_VERIFY(!empty.querySource(source) && source.sharesRepresentation(sentinel));
  TextReplacement out = TextReplacement::Empty(TextPlacement::End());
  const TextReplacement before(out);
  Blob scratch = Blob::Create();
  LOKA_VERIFY(scratch.tryAssign(reinterpret_cast<const unsigned char *>("ok"), 2));
  LOKA_VERIFY(TextReplacement::FromUtf8(scratch, TextPlacement::Start(), out) == TEXT_DOCUMENT_PREPARE_FAILED);
  LOKA_VERIFY(!(out != before));
  LOKA_VERIFY(TextReplacement::FromUtf8(Blob(), TextPlacement::Start(), out) == TEXT_DOCUMENT_PREPARE_FAILED);
  LOKA_VERIFY(!(out != before));
  const char *invalid[] = {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"};
  for (unsigned i = 0; i < 4; ++i)
  {
    LOKA_VERIFY(TextReplacement::FromUtf8(sealed(invalid[i], std::strlen(invalid[i])), TextPlacement::Start(), out)
                == TEXT_DOCUMENT_NOT_TEXT);
    LOKA_VERIFY(!(out != before));
  }
  const Blob body = sealed("\xef\xbb\xbf"
                           "a\r\nb\0",
                           8);
  LOKA_VERIFY(TextReplacement::FromUtf8(body, TextPlacement::End(), out) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(out.stage() == TextReplacement::BYTES && out.logicalLength() == 7);
  LOKA_VERIFY(out.bytes() == body && out.placement() == TextPlacement::End());
  LOKA_VERIFY(!out.querySource(source) && source.sharesRepresentation(sentinel));
  TextReplacement same;
  LOKA_VERIFY(TextReplacement::FromUtf8(body, TextPlacement::End(), same) == TEXT_DOCUMENT_OK);
  const int attempts = allocation.attempts;
  const TextReplacement copy(out);
  same = copy;
  LOKA_VERIFY(!(same != out) && allocation.attempts == attempts);
  LOKA_VERIFY(out
              != replacement("\xef\xbb\xbf"
                             "a\r\nb\0",
                             8,
                             TextPlacement::End()));
  LOKA_VERIFY(TextReplacement::FromUtf8(body, TextPlacement::Start(), same) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(same != out);
  LOKA_VERIFY(TextReplacement::Empty() != none);
  LOKA_VERIFY(RequestTraits<TextReplacement>::coalescable == 1);
  LOKA_VERIFY(RequestTraits<TextReplacement>::accepted() == TEXT_REPLACE_OK);
  LOKA_VERIFY(RequestTraits<TextReplacement>::unavailable() == TEXT_REPLACE_UNAVAILABLE);
}
void testTextReplacementReceipt()
{
  BlobAllocationProbe allocation;
  Reply<TextReplacement> reply;
  const int baseline = allocation.live;
  {
    const TextReplacement value = replacement("body", 4, TextPlacement::End());
    LOKA_VERIFY(allocation.live > baseline);
    reply = Reply<TextReplacement>::Granted(value);
    LOKA_VERIFY(reply.applied().stage() == TextReplacement::BYTES);
  }
  LOKA_VERIFY(allocation.live == baseline && allocation.byteFrees == 1);
  LOKA_VERIFY(reply.applied().logicalLength() == 4 && reply.applied().placement() == TextPlacement::End());
  TextReplacementReceipt receipt = reply.applied();
  TextReplacementReceipt copy(receipt);
  LOKA_VERIFY(!(copy != receipt));
  copy = TextReplacementReceipt();
  LOKA_VERIFY(copy != receipt);
}
void testTextReplacementOrigin()
{
  BlobAllocationProbe allocation;
  const File a(loka::core::String::Utf8("same", 4));
  const File b(loka::core::String::Utf8("same", 4));
  const File alias(a);
  const int attempts = allocation.attempts;
  LOKA_VERIFY(a.sharesRepresentation(alias) && !a.sharesRepresentation(b));
  LOKA_VERIFY(allocation.attempts == attempts);
  LOKA_VERIFY(!(a != b));
  LOKA_VERIFY(TextDocumentOrigin(a, 7) == TextDocumentOrigin(alias, 7));
  LOKA_VERIFY(TextDocumentOrigin(a, 7) != TextDocumentOrigin(b, 7));
  LOKA_VERIFY(TextDocumentOrigin(a, 7) != TextDocumentOrigin(a, 8));
  const TextDocumentOrigin none;
  File out(a);
  LOKA_VERIFY(none.revision() == 0 && !none.querySource(out) && out.sharesRepresentation(a));
  LOKA_VERIFY(TextDocumentOrigin(b, 7).querySource(out) && out.sharesRepresentation(b));
  File changed(a);
  changed.setKind(File::KIND_FOLDER);
  LOKA_VERIFY(!a.sharesRepresentation(changed));
  const unsigned char location[] = {1, 2, 3};
  const loka::core::String display = loka::core::String::Utf8("display", 7);
  File locatedA, locatedB;
  LOKA_VERIFY(loka::platform::file::FileLocatorAccess::capture(display, File::KIND_FILE, location, 3, locatedA));
  LOKA_VERIFY(loka::platform::file::FileLocatorAccess::capture(display, File::KIND_FILE, location, 3, locatedB));
  LOKA_VERIFY(!locatedA.sharesRepresentation(locatedB));
  LOKA_VERIFY(locatedA.sharesRepresentation(File(locatedA)));
}
void testTextStoreSource()
{
  const char body[] = "a\r\nb\r\n\n\r\r";
  TextStoreSource source(replacement(body, sizeof(body) - 1, TextPlacement::AtUtf8Offset(2)));
  LOKA_VERIFY(source.requestedPlacement() == TextPlacement::AtUtf8Offset(2));
  unsigned int scalar = 0;
  bool separator = false;
  LOKA_VERIFY(source.next(scalar, separator) && scalar == 'a' && !separator && source.logicalOffset() == 1);
  LOKA_VERIFY(source.next(scalar, separator) && scalar == '\n' && separator && source.logicalOffset() == 2);
  LOKA_VERIFY(source.next(scalar, separator) && scalar == 'b' && !separator);
  for (unsigned i = 0; i < 4; ++i)
    LOKA_VERIFY(source.next(scalar, separator) && scalar == '\n' && separator);
  LOKA_VERIFY(!source.next(scalar, separator) && source.logicalOffset() == 7);
  LOKA_VERIFY(!source.clamped() && source.appliedPlacement() == TextPlacement::AtUtf8Offset(2));
  const char utf8[] = "a\xc2\xa2\xe2\x82\xac\xf0\x9f\x98\x80";
  const unsigned long expected[] = {0, 1, 1, 3, 3, 3, 6, 6, 6, 6, 10, 10};
  for (unsigned long i = 0; i < 12; ++i)
  {
    TextStoreSource cursor(replacement(utf8, sizeof(utf8) - 1, TextPlacement::AtUtf8Offset(i)));
    while (cursor.next(scalar, separator))
      LOKA_VERIFY(!separator);
    LOKA_VERIFY(cursor.appliedPlacement() == TextPlacement::AtUtf8Offset(expected[i]));
    LOKA_VERIFY(cursor.clamped() == (i != expected[i]));
    LOKA_VERIFY(cursor.logicalOffset() == 10);
  }
  const TextPlacement placements[] = {TextPlacement::Start(), TextPlacement::End(), TextPlacement::AtUtf8Offset(~0UL)};
  for (unsigned i = 0; i < 3; ++i)
  {
    TextStoreSource empty(TextReplacement::Empty(placements[i]));
    LOKA_VERIFY(!empty.next(scalar, separator));
    LOKA_VERIFY(empty.clamped() == (i == 2));
    LOKA_VERIFY(empty.appliedPlacement() == (i == 2 ? TextPlacement::AtUtf8Offset(0) : placements[i]));
  }
  const char unicode[] = "\xe2\x80\xa8\xe2\x80\xa9\xc2\x85\0";
  TextStoreSource other(replacement(unicode, sizeof(unicode) - 1, TextPlacement::End()));
  unsigned count = 0;
  while (other.next(scalar, separator))
  {
    LOKA_VERIFY(!separator);
    ++count;
  }
  LOKA_VERIFY(count == 4 && other.logicalOffset() == 9 && !other.clamped());
}
