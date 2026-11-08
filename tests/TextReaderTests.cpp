#include "TextReaderTests.hpp"
#include "support/TextDocumentAccess.hpp"
#include "testing/app/TextPositionTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/TestVerify.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/text/TextDocumentPublisher.hpp"
#include <cstring>
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::app::testing;
  class ReaderRoot : public BoundaryNodeFor<ReaderRoot>
  {
  public:
    TextDocument *text;
    explicit ReaderRoot(const BoundaryPropsFor<ReaderRoot> &p)
        : BoundaryNodeFor<ReaderRoot>(p),
          text(new TextDocument)
    {
      this->document(*this->text);
    }
    ~ReaderRoot()
    {
      delete this->text;
    }
    virtual void composeNode(NodeComposition &) {}
    void destroyDocument()
    {
      delete this->text;
      this->text = 0;
    }
    void reclaimRegistration()
    {
      TextDocumentRegistration *row = TextDocumentAccess::registration(*this->text);
      this->unlinkParticipant(*row);
      row->reclaim();
    }
  };
  WindowProps readerProps()
  {
    WindowProps p;
    p.scene(new Scene(Boundary<ReaderRoot>()));
    return p;
  }
  class ReaderController : public NullScenePlatformController
  {
  public:
    unsigned reads, commits, chunks;
    bool unsupported;
    ReaderController()
        : reads(0),
          commits(0),
          chunks(0),
          unsupported(false)
    {
    }
    virtual bool readTextStore(TextStoreRow &row, TextStoreReading &out)
    {
      ++this->reads;
      return NullScenePlatformController::readTextStore(row, out);
    }
    virtual void commitMarkedText(TextStoreRow &row)
    {
      ++this->commits;
      NullScenePlatformController::commitMarkedText(row);
    }
    virtual TextChunkResult readTextChunk(TextStoreRow &row,
                                          unsigned long &offset,
                                          unsigned long end,
                                          char *out,
                                          unsigned long capacity,
                                          unsigned long &n)
    {
      ++this->chunks;
      LOKA_VERIFY(capacity > 0);
      if (this->unsupported)
        return IPlatformController::readTextChunk(row, offset, end, out, capacity, n);
      return NullScenePlatformController::readTextChunk(row, offset, end, out, capacity, n);
    }
  };
  struct ReaderFixture
  {
    ReaderController controller;
    NullPlatformContext context;
    NullWindow window;
    WindowAdmissionTestApp app;
    ReaderFixture()
        : window(&this->context, readerProps(), &this->controller),
          app(this->window)
    {
      this->app.flush();
      this->app.reconcileAtCompletion();
    }
    ReaderRoot &root()
    {
      return *static_cast<ReaderRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*this->window.scene()));
    }
    TextDocument &text()
    {
      return *this->root().text;
    }
    void body(const std::string &body)
    {
      TextDocumentAccess::body(this->text(), body);
      this->app.reconcileAtCompletion();
    }
  };
  std::string readAll(TextReader &reader, unsigned long capacity)
  {
    char out[512];
    unsigned long n = 99;
    std::string result;
    TextReadResult r;
    while ((r = reader.read(out, capacity, n)) == TEXT_READ_MORE)
    {
      LOKA_VERIFY(n > 0 && n <= capacity);
      std::size_t scalarOffset = 0;
      unsigned int scalar;
      while (scalarOffset < n)
        LOKA_VERIFY(loka::core::detail::DecodeUtf8Codepoint(out, n, scalarOffset, scalar));
      result.append(out, n);
    }
    LOKA_VERIFY(r == TEXT_READ_END && n == 0);
    LOKA_VERIFY(reader.read(out, capacity, n) == TEXT_READ_END && n == 0);
    return result;
  }
  void refused(TextReader &reader, TextReadRefusal reason)
  {
    char out[8];
    unsigned long n = 99;
    LOKA_VERIFY(reader.read(out, sizeof out, n) == TEXT_READ_REFUSED && n == 0);
    LOKA_VERIFY(reader.refusal() == reason);
    LOKA_VERIFY(reader.read(out, 0, n) == TEXT_READ_REFUSED && n == 0);
    LOKA_VERIFY(reader.refusal() == reason);
  }
  TextPosition native(TextDocument &doc, unsigned long offset)
  {
    return TextPositionTestAccess::make(doc.fact()->get().textRevision(), offset);
  }
} // namespace
void testTextReaderChunks()
{
  ReaderFixture f;
  const std::string body = "a\r\r\nb\nc\r\xf0\x9f\x98\x80\xc3\xa9";
  const std::string logical = "a\n\nb\nc\n\xf0\x9f\x98\x80\xc3\xa9";
  f.body(body);
  const unsigned long capacities[] = {4, 5, 7, 512, 13};
  for (unsigned i = 0; i < 5; ++i)
  {
    TextReader reader(f.text());
    LOKA_VERIFY(readAll(reader, capacities[i]) == logical);
  }
  unsigned long length = 99;
  LOKA_VERIFY(f.text().queryLength(length) && length == logical.size());
}
void testTextReaderCapacity()
{
  ReaderFixture f;
  f.body("abcd");
  for (unsigned long capacity = 0; capacity < 4; ++capacity)
  {
    TextReader reader(f.text());
    char out[4];
    unsigned long n = 99;
    LOKA_VERIFY(reader.read(out, capacity, n) == TEXT_READ_REFUSED && n == 0);
    refused(reader, TEXT_READ_RANGE);
  }
}
void testTextReaderRanges()
{
  ReaderFixture f;
  f.body("a\r\nb\xc3\xa9");
  TextReader empty(f.text(), native(f.text(), 3), native(f.text(), 3));
  LOKA_VERIFY(readAll(empty, 4).empty());
  TextReader backward(f.text(), native(f.text(), 3), native(f.text(), 0));
  refused(backward, TEXT_READ_RANGE);
  TextReader splitStart(f.text(), native(f.text(), 2), native(f.text(), 3));
  refused(splitStart, TEXT_READ_RANGE);
  TextReader splitEnd(f.text(), native(f.text(), 0), native(f.text(), 5));
  refused(splitEnd, TEXT_READ_RANGE);
  TextReader past(f.text(), native(f.text(), 0), native(f.text(), 99));
  refused(past, TEXT_READ_RANGE);
  TextReader stale(f.text(), TextPositionTestAccess::make(0, 0), native(f.text(), 3));
  refused(stale, TEXT_READ_NOT_READY);
}
void testTextReaderNotReady()
{
  TextDocument doc;
  TextReader reader(doc);
  refused(reader, TEXT_READ_NOT_READY);
  ReaderFixture f;
  f.controller.unsupported = true;
  TextReader unsupported(f.text());
  refused(unsupported, TEXT_READ_NOT_READY);
}
void testTextReaderPending()
{
  ReaderFixture f;
  TextReader reader(f.text());
  TextDocumentAccess::body(f.text(), "pending");
  refused(reader, TEXT_READ_NOT_READY);
}
void testTextReaderMarked()
{
  ReaderFixture f;
  TextDocumentAccess::uncommitted(f.text(), "marked");
  f.app.reconcileAtCompletion();
  TextReader reader(f.text());
  refused(reader, TEXT_READ_NOT_READY);
}
void testTextReaderRevision()
{
  ReaderFixture f;
  f.body("abcdefgh");
  TextReader reader(f.text());
  char out[4];
  unsigned long n;
  LOKA_VERIFY(reader.read(out, 4, n) == TEXT_READ_MORE && n == 4);
  f.body("new body");
  refused(reader, TEXT_READ_NOT_READY);
}
void testTextReaderWithdraw()
{
  ReaderFixture f;
  TextReader reader(f.text());
  TextDocumentAccess::withdraw(f.text());
  LOKA_VERIFY(!TextDocumentAccess::connected(reader));
  refused(reader, TEXT_READ_NOT_READY);
}
void testTextReaderDirectReclaim()
{
  ReaderFixture f;
  TextReader reader(f.text());
  f.root().reclaimRegistration();
  refused(reader, TEXT_READ_NOT_READY);
}
void testTextReaderSceneReplacement()
{
  ReaderFixture f;
  TextReader reader(f.text());
  Scene *candidate = new Scene(Boundary<ReaderRoot>());
  LOKA_VERIFY(f.window.sceneManager()->commitTransaction(0, candidate));
  f.app.flush();
  f.app.reconcileAtCompletion();
  refused(reader, TEXT_READ_NOT_READY);
}
void testTextReaderDocumentFirst()
{
  ReaderFixture f;
  TextReader reader(f.text());
  f.root().destroyDocument();
  refused(reader, TEXT_READ_NOT_READY);
  const unsigned reads = f.controller.reads, commits = f.controller.commits;
  f.app.reconcileAtCompletion();
  f.app.commitTextInput();
  LOKA_VERIFY(f.controller.reads == reads && f.controller.commits == commits);
}
void testTextReaderRegistrationFirst()
{
  ReaderFixture f;
  TextReader reader(f.text());
  f.root().reclaimRegistration();
  unsigned long n = 99;
  LOKA_VERIFY(!f.text().queryLength(n) && n == 99);
  refused(reader, TEXT_READ_NOT_READY);
}
void testTextReaderDestroyAfterCut()
{
  ReaderFixture f;
  TextReader *reader = new TextReader(f.text());
  f.root().reclaimRegistration();
  delete reader;
}
void testTextReaderStoreRetired()
{
  ReaderFixture f;
  TextReader reader(f.text());
  f.controller.destroy();
  f.controller.drainNativeRetirements();
  refused(reader, TEXT_READ_NOT_READY);
}
void testTextReaderRefusalReasons()
{
  ReaderFixture f;
  const char *invalid[] = {"\x80", "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82"};
  for (unsigned i = 0; i < 5; ++i)
  {
    f.body(invalid[i]);
    TextReader reader(f.text());
    refused(reader, TEXT_READ_MALFORMED);
  }
  f.body("abc");
  TextReader resource(f.text());
  failTextChunkReads(1);
  refused(resource, TEXT_READ_RESOURCE);
  TextReader valid(f.text());
  LOKA_VERIFY(readAll(valid, 4) == "abc");
}
void testTextReaderLineQueries()
{
  ReaderFixture f;
  const std::string bodies[] = {"", "\n", "\n\r\n\r", "a\r\r\nb", std::string(256, 'a') + "\n\nend\n"};
  for (unsigned b = 0; b < 5; ++b)
  {
    f.body(bodies[b]);
    TextReader all(f.text());
    const std::string logical = readAll(all, 7);
    unsigned long count = 0, expected = 1;
    for (std::size_t i = 0; i < logical.size(); ++i)
      if (logical[i] == '\n')
        ++expected;
    LOKA_VERIFY(f.text().queryLineCount(count) && count == expected);
    std::size_t begin = 0;
    for (unsigned long line = 0; line < count; ++line)
    {
      TextPosition start, end;
      LOKA_VERIFY(f.text().queryLineRange(line, start, end));
      std::size_t stop = logical.find('\n', begin);
      if (stop == std::string::npos)
        stop = logical.size();
      TextReader part(f.text(), start, end);
      LOKA_VERIFY(readAll(part, 5) == logical.substr(begin, stop - begin));
      begin = stop + 1;
    }
    TextPosition start, end;
    LOKA_VERIFY(!f.text().queryLineRange(count, start, end));
  }
}
void testTextReaderBridges()
{
  ReaderFixture f;
  f.body("a\r\r\nb\xf0\x9f\x98\x80\xc3\xa9");
  const unsigned long logical[] = {0, 1, 2, 3, 4, 8, 10};
  const unsigned long nativeOffsets[] = {0, 1, 2, 4, 5, 9, 11};
  const unsigned long utf16[] = {0, 1, 2, 3, 4, 6, 7};
  for (unsigned i = 0; i < 7; ++i)
  {
    TextPosition pos;
    unsigned long result = 99;
    LOKA_VERIFY(f.text().queryPositionAtUtf8(logical[i], pos));
    LOKA_VERIFY(pos == native(f.text(), nativeOffsets[i]));
    LOKA_VERIFY(f.text().queryUtf8Offset(pos, result) && result == logical[i]);
    LOKA_VERIFY(f.text().queryUtf16Offset(pos, result) && result == utf16[i]);
  }
  const unsigned long invalidNative[] = {3, 6, 7, 8, 10, 12, kTextNativeEnd};
  for (unsigned i = 0; i < 7; ++i)
  {
    unsigned long result = 99;
    LOKA_VERIFY(!f.text().queryUtf8Offset(native(f.text(), invalidNative[i]), result) && result == 99);
    LOKA_VERIFY(!f.text().queryUtf16Offset(native(f.text(), invalidNative[i]), result) && result == 99);
  }
  const unsigned long invalidLogical[] = {5, 6, 7, 9, 11};
  for (unsigned i = 0; i < 5; ++i)
  {
    TextPosition pos;
    LOKA_VERIFY(!f.text().queryPositionAtUtf8(invalidLogical[i], pos) && pos.isNone());
  }
  unsigned long result = 99;
  LOKA_VERIFY(!f.text().queryUtf8Offset(TextPositionTestAccess::make(0, 0), result));
  LOKA_VERIFY(!f.text().queryUtf16Offset(TextPositionTestAccess::make(0, 0), result));
  const unsigned calls = f.controller.chunks;
  TextPosition zero;
  LOKA_VERIFY(f.text().queryPositionAtUtf8(0, zero) && f.controller.chunks == calls);
}
void testTextChunkDoor()
{
  ReaderFixture f;
  f.body("\xf0\x9f\x98\x80\r\na");
  TextStoreRow &row = *TextDocumentAccess::store(f.text());
  char out[8];
  unsigned long offset = 0, n = 99;
  for (unsigned long capacity = 1; capacity < 4; ++capacity)
    LOKA_VERIFY(f.controller.readTextChunk(row, offset, kTextNativeEnd, out, capacity, n) == TEXT_CHUNK_NO_ROOM
                && n == 0 && offset == 0);
  LOKA_VERIFY(f.controller.readTextChunk(row, offset, kTextNativeEnd, out, 5, n) == TEXT_CHUNK_PROGRESS && n == 5
              && offset == 6);
  LOKA_VERIFY(std::string(out, n) == "\xf0\x9f\x98\x80\n");
  LOKA_VERIFY(f.controller.readTextChunk(row, offset, kTextNativeEnd, out, 1, n) == TEXT_CHUNK_PROGRESS && n == 1
              && offset == 7);
  LOKA_VERIFY(f.controller.readTextChunk(row, offset, kTextNativeEnd, out, 1, n) == TEXT_CHUNK_END && n == 0
              && offset == 7);
  offset = 8;
  LOKA_VERIFY(f.controller.readTextChunk(row, offset, kTextNativeEnd, out, 4, n) == TEXT_CHUNK_REFUSED_RANGE && n == 0
              && offset == 8);
  offset = 0;
  LOKA_VERIFY(f.controller.readTextChunk(row, offset, 5, out, 8, n) == TEXT_CHUNK_REFUSED_RANGE && n == 0
              && offset == 0);
  offset = 1;
  LOKA_VERIFY(f.controller.readTextChunk(row, offset, 4, out, 8, n) == TEXT_CHUNK_REFUSED_RANGE && n == 0
              && offset == 1);
  f.body("\xc3\xa9x");
  offset = 0;
  LOKA_VERIFY(f.controller.readTextChunk(row, offset, kTextNativeEnd, out, 2, n) == TEXT_CHUNK_PROGRESS && n == 2
              && offset == 2);
  offset = 0;
  LOKA_VERIFY(f.controller.readTextChunk(row, offset, kTextNativeEnd, out, 3, n) == TEXT_CHUNK_PROGRESS && n == 3
              && offset == 3);
}
namespace
{
  void destroyDuringCommit(void *data)
  {
    static_cast<ReaderRoot *>(data)->destroyDocument();
  }
  struct MembershipValue
  {
    MemberHook<MembershipValue> hook;
    MembershipValue()
        : hook(*this)
    {
    }
  };
  struct LeavingVisitor
  {
    unsigned visited;
    LeavingVisitor()
        : visited(0)
    {
    }
    void visit(MembershipValue &value)
    {
      ++this->visited;
      value.hook.leave();
    }
  };
} // namespace
void testTextPublisherDocumentDestroyedDuringCommit()
{
  ReaderFixture f;
  TextDocumentAccess::onCommit(f.text(), &destroyDuringCommit, &f.root());
  const unsigned reads = f.controller.reads;
  f.app.commitTextInput();
  LOKA_VERIFY(f.root().text == 0 && f.controller.reads == reads);
}
void testTextMembershipPrimitives()
{
  MembershipValue a, b;
  {
    MemberList<MembershipValue> list;
    list.join(a.hook);
    list.join(a.hook);
    list.join(b.hook);
    LeavingVisitor visitor;
    list.visit(visitor);
    LOKA_VERIFY(visitor.visited == 2 && !a.hook.owner() && !b.hook.owner());
    list.join(a.hook);
    list.join(b.hook);
    list.cutAll();
    list.cutAll();
    LOKA_VERIFY(!a.hook.owner() && !b.hook.owner());
    list.join(a.hook);
  }
  LOKA_VERIFY(!a.hook.owner());
  int left = 0;
  long right = 0;
  TwinLink<int, long> link(&left);
  {
    TwinLink<long, int> other(&right);
    link.connect(other);
    LOKA_VERIFY(link.remote() == &right && other.remote() == &left);
    link.cut();
    link.cut();
    LOKA_VERIFY(!link.remote() && !other.remote());
    link.connect(other);
  }
  LOKA_VERIFY(!link.remote());
}
void testTextReaderEndedRechecks()
{
  ReaderFixture f;
  TextReader reader(f.text());
  LOKA_VERIFY(readAll(reader, 4).empty());
  f.body("changed");
  refused(reader, TEXT_READ_NOT_READY);
}
