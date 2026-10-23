#include "support/TextBufferFixture.hpp"
#include "support/BlobAllocationProbe.hpp"
#include "TextDocumentFileTests.hpp"
#include "support/TestVerify.hpp"
#include "app/TextDocumentFile.hpp"
#include "support/TextEditorStateOwner.hpp"
#include "support/TextEditorAccess.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/file/FileLocatorAccess.hpp"
#include "platform/file/FileHandle.hpp"
#include "platform/file/FileIO.hpp"
#include "platform/StringUTF8.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#if !defined(_WIN32) && !defined(__APPLE__) && !defined(LOKA_RETRO68)
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
namespace
{
  using namespace loka::core;
  using namespace loka::app;
  using loka::file::File;

  // These integration pins exercise production generic stdio only. They make no
  // assertion about Toolbox stdio, catalog bridging, or native desktop writes.
  // Linux-only descriptor census observes actual stream cleanup without replacing IO.
  int openDescriptorCount()
  {
    DIR *directory = opendir("/proc/self/fd");
    if (!directory)
    {
      std::puts("[skip] /proc/self/fd unavailable: stream-close census");
      return -1;
    }
    int count = 0;
    while (readdir(directory))
      ++count;
    LOKA_VERIFY(closedir(directory) == 0);
    return count;
  }
  struct TempFile
  {
    std::string directory;
    std::string path;
    NullPlatformContext context;
    File file;
    TempFile()
        : directory(),
          path(),
          context(),
          file()
    {
      char pattern[] = "/tmp/loka-text-document-XXXXXX";
      char *created = mkdtemp(pattern);
      LOKA_VERIFY(created);
      this->directory = created;
      this->path = this->directory + "/document.txt";
      this->context.setApplicationDirectory(String(this->directory));
      this->file = File::Application() << File("document.txt");
    }
    ~TempFile()
    {
      std::remove(this->path.c_str());
      LOKA_VERIFY(rmdir(this->directory.c_str()) == 0);
    }
    void put(const std::string &bytes)
    {
      std::FILE *stream = std::fopen(this->path.c_str(), "wb");
      LOKA_VERIFY(stream);
      LOKA_VERIFY(std::fwrite(bytes.data(), 1, bytes.size(), stream) == bytes.size());
      LOKA_VERIFY(std::fclose(stream) == 0);
    }
    std::string bytes() const
    {
      std::FILE *stream = std::fopen(this->path.c_str(), "rb");
      LOKA_VERIFY(stream);
      std::string result;
      char buffer[1024];
      std::size_t n;
      while ((n = std::fread(buffer, 1, sizeof(buffer), stream)) != 0)
        result.append(buffer, n);
      LOKA_VERIFY(!std::ferror(stream));
      LOKA_VERIFY(std::fclose(stream) == 0);
      return result;
    }
  };
  struct Document : loka::app::testing::TextEditorStateOwner
  {
    loka::testing::TextBufferFixture lines;
    TextEditorNode editor;
    explicit Document(unsigned short capacity = 256)
        : lines(),
          editor(TextEditorProps(this->lines, this->cursor))
    {
      LOKA_VERIFY(this->lines.attach(&this->tracker, capacity) == ATTACH_OK);
    }
    void append(const std::string &bytes)
    {
      LOKA_VERIFY(this->lines.insert(this->lines.size(), String::Utf8(bytes.data(), bytes.size())) == EDIT_OK);
    }
    EditorResult availability()
    {
      return loka::app::testing::TextEditorAccess::document(this->editor).availability();
    }
  };
  std::string row(const loka::testing::TextBufferFixture &lines, unsigned short index)
  {
    std::string result;
    LOKA_VERIFY(loka::platform::CollectUtf8(lines.at(index).value, result));
    return result;
  }
  struct Snapshot
  {
    std::vector<ItemId> ids;
    std::vector<std::string> rows;
    ListRevision revision;
    explicit Snapshot(const loka::testing::TextBufferFixture &lines)
        : ids(),
          rows(),
          revision(lines.revision().get())
    {
      for (unsigned short i = 0; i < lines.size(); ++i)
      {
        this->ids.push_back(lines.at(i).id);
        this->rows.push_back(row(lines, i));
      }
    }
    void unchanged(const loka::testing::TextBufferFixture &lines) const
    {
      LOKA_VERIFY(lines.size() == this->rows.size());
      LOKA_VERIFY(!(lines.revision().get() != this->revision));
      for (unsigned short i = 0; i < lines.size(); ++i)
      {
        LOKA_VERIFY(lines.at(i).id == this->ids[i]);
        LOKA_VERIFY(row(lines, i) == this->rows[i]);
      }
    }
  };
  TextDocumentResult read(TempFile &source, Document &document, const std::string &bytes)
  {
    source.put(bytes);
    return ReadTextDocument(&source.context, source.file, document.lines);
  }
  std::string rows(unsigned count, const std::string &separator)
  {
    std::string result;
    for (unsigned i = 0; i < count; ++i)
    {
      if (i)
        result += separator;
      result += 'x';
    }
    return result;
  }
} // namespace

void testTextDocumentSplitMatrix()
{
  struct Case
  {
    const char *input;
    const char *expected[6];
    unsigned count;
  };
  const Case cases[] = {{"", {""}, 1},
                        {"a", {"a"}, 1},
                        {"a\rb", {"a", "b"}, 2},
                        {"a\nb", {"a", "b"}, 2},
                        {"a\r\nb", {"a", "b"}, 2},
                        {"a\rb\nc\r\nd", {"a", "b", "c", "d"}, 4},
                        {"a\r", {"a", ""}, 2},
                        {"a\n", {"a", ""}, 2},
                        {"a\r\n", {"a", ""}, 2},
                        {"\r\n", {"", ""}, 2},
                        {"\r\r\n\n", {"", "", "", ""}, 4},
                        {"\n\r", {"", "", ""}, 3},
                        {"\t\177\001\013\014\037", {"\t\177\001\013\014\037"}, 1}};
  TempFile source;
  Document document;
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
  {
    LOKA_VERIFY(read(source, document, cases[i].input) == TEXT_DOCUMENT_OK);
    LOKA_VERIFY(document.lines.size() == cases[i].count);
    for (unsigned short j = 0; j < document.lines.size(); ++j)
      LOKA_VERIFY(row(document.lines, j) == cases[i].expected[j]);
    LOKA_VERIFY(document.availability() == EDITOR_OK);
  }
  const char bad[][3] = {{'a', 0, 'b'}, {'a', static_cast<char>(0x80), 'b'}};
  for (unsigned i = 0; i < 2; ++i)
  {
    Snapshot before(document.lines);
    LOKA_VERIFY(read(source, document, std::string(bad[i], 3)) == TEXT_DOCUMENT_NON_ASCII);
    before.unchanged(document.lines);
  }
}

void testTextDocumentLimits()
{
  TempFile source;
  Document document;
  LOKA_VERIFY(read(source, document, rows(256, "\n")) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(document.lines.size() == 256);
  Snapshot full(document.lines);
  // The former global row limit is now this buffer's admitted line capacity.
  LOKA_VERIFY(read(source, document, rows(257, "\n")) == TEXT_DOCUMENT_CAPACITY);
  full.unchanged(document.lines);
  LOKA_VERIFY(read(source, document, std::string(8192, 'a')) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(row(document.lines, 0).size() == 8192);
  LOKA_VERIFY(read(source, document, std::string(4096, 'a') + "\n" + std::string(4095, 'b')) == TEXT_DOCUMENT_OK);
  Snapshot maxBytes(document.lines);
  LOKA_VERIFY(read(source, document, std::string(8193, 'a')) == TEXT_DOCUMENT_TOO_LARGE);
  maxBytes.unchanged(document.lines);
  LOKA_VERIFY(read(source, document, std::string(4096, 'a') + "\n" + std::string(4096, 'b'))
              == TEXT_DOCUMENT_TOO_LARGE);
  maxBytes.unchanged(document.lines);
  std::string raw(7937, 'a');
  for (unsigned i = 0; i < 255; ++i)
    raw += "\r\n";
  LOKA_VERIFY(raw.size() == 8447);
  LOKA_VERIFY(read(source, document, raw) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(document.lines.size() == 256);
  LOKA_VERIFY(row(document.lines, 0).size() == 7937);
  LOKA_VERIFY(row(document.lines, 255).empty());
  Snapshot rawLimit(document.lines);
  raw += 'a';
  LOKA_VERIFY(read(source, document, raw) == TEXT_DOCUMENT_TOO_LARGE);
  rawLimit.unchanged(document.lines);
}

void testTextDocumentReplacement()
{
  TempFile source;
  Document document;
  for (unsigned i = 0; i < 256; ++i)
    document.append("old");
  const ItemId original = document.lines.at(0).id;
  LOKA_VERIFY(read(source, document, rows(256, "\n")) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(document.lines.size() == 256);
  LOKA_VERIFY(document.lines.at(0).id != original);
  for (unsigned short i = 0; i < 256; ++i)
    LOKA_VERIFY(row(document.lines, i) == "x");
  LOKA_VERIFY(read(source, document, "one") == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(document.lines.size() == 1 && row(document.lines, 0) == "one");
  LOKA_VERIFY(read(source, document, rows(256, "\r\n")) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(document.lines.size() == 256);
  for (unsigned short i = 0; i < 256; ++i)
    LOKA_VERIFY(row(document.lines, i) == "x");
}

void testTextDocumentCommitRefusals()
{
  TempFile source;
  Document small(1);
  small.append("kept");
  Snapshot before(small.lines);
  LOKA_VERIFY(read(source, small, "a\nb") == TEXT_DOCUMENT_CAPACITY);
  before.unchanged(small.lines);
  // Exercise the real bounded identity contract without adding production accessors.
  for (unsigned i = 1; i < 65535; ++i)
  {
    LOKA_VERIFY(small.lines.remove(small.lines.at(0).id) == EDIT_OK);
    small.append("kept");
  }
  LOKA_VERIFY(small.lines.at(0).id.seq == 65535);
  Snapshot exhausted(small.lines);
  LOKA_VERIFY(read(source, small, "new") == TEXT_DOCUMENT_ID_EXHAUSTED);
  exhausted.unchanged(small.lines);
  loka::testing::TextBufferFixture unattached;
  const Snapshot detached(unattached);
  LOKA_VERIFY(ReadTextDocument(&source.context, source.file, unattached) == TEXT_DOCUMENT_UNATTACHED);
  detached.unchanged(unattached);
}

void testTextDocumentPublication()
{
  struct Probe
  {
    TempFile &source;
    Document &document;
    unsigned calls;
    Probe(TempFile &s, Document &d)
        : source(s),
          document(d),
          calls(0)
    {
    }
    static void changed(void *data)
    {
      Probe &self = *static_cast<Probe *>(data);
      ++self.calls;
      Snapshot before(self.document.lines);
      LOKA_VERIFY(ReadTextDocument(&self.source.context, self.source.file, self.document.lines)
                  == TEXT_DOCUMENT_REENTRANT);
      before.unchanged(self.document.lines);
    }
  };
  TempFile source;
  Document document;
  document.append("old");
  Probe probe(source, document);
  State<ListRevision> &revision = const_cast<State<ListRevision> &>(document.lines.revision());
  const ListRevision before = revision.get();
  revision.bind(&Probe::changed, &probe, false);
  LOKA_VERIFY(read(source, document, "one\ntwo\n") == TEXT_DOCUMENT_OK);
  revision.unbind(&Probe::changed, &probe);
  LOKA_VERIFY(probe.calls == 1);
  LOKA_VERIFY(revision.get().structure == before.structure + 1);
  LOKA_VERIFY(revision.get().content == before.content);
}

void testTextDocumentRoundTrip()
{
  TempFile target;
  Document source;
  source.append("first\t\177");
  source.append("");
  source.append("last");
  source.append("");
  LOKA_VERIFY(std::string(kTextDocumentNewline) == "\n");
  LOKA_VERIFY(WriteTextDocument(&target.context, target.file, source.lines) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(target.bytes() == "first\t\177\n\nlast\n");
  Document copy;
  LOKA_VERIFY(ReadTextDocument(&target.context, target.file, copy.lines) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(copy.lines.size() == source.lines.size());
  for (unsigned short i = 0; i < source.lines.size(); ++i)
    LOKA_VERIFY(row(copy.lines, i) == row(source.lines, i));
  // Generic preparation is an observation-free no-op, for absent and existing paths.
  loka::platform::file::FileHandle handle;
  LOKA_VERIFY(target.context.openFile(target.file, handle));
  const std::string saved = target.bytes();
  LOKA_VERIFY(loka::platform::file::PrepareTextDocumentDestination(handle) == loka::platform::file::PREPARE_OK);
  LOKA_VERIFY(target.bytes() == saved);
  LOKA_VERIFY(std::remove(target.path.c_str()) == 0);
  LOKA_VERIFY(loka::platform::file::PrepareTextDocumentDestination(handle) == loka::platform::file::PREPARE_OK);
  LOKA_VERIFY(access(target.path.c_str(), F_OK) != 0);
  Document empty;
  LOKA_VERIFY(WriteTextDocument(&target.context, target.file, empty.lines) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(target.bytes().empty());
  LOKA_VERIFY(ReadTextDocument(&target.context, target.file, empty.lines) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(empty.lines.size() == 1 && row(empty.lines, 0).empty());
  LOKA_VERIFY(WriteTextDocument(&target.context, target.file, empty.lines) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(target.bytes().empty());
}

void testTextDocumentEditorParity()
{
  struct Case
  {
    std::string bytes;
    unsigned count;
    bool accepted;
  };
  const Case cases[] = {{"", 1, true},
                        {"\t\177\001\013", 1, true},
                        {std::string("a\0b", 3), 1, false},
                        {std::string(1, static_cast<char>(0x80)), 1, false},
                        {"a\rb", 1, false},
                        {"a\nb", 1, false},
                        {std::string(8192, 'x'), 1, true},
                        {std::string(8193, 'x'), 1, false},
                        {"", 256, true},
                        {std::string(32, 'x'), 257, false},
                        {std::string(4096, 'x'), 2, false}};
  TempFile target;
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
  {
    Document document(257);
    for (unsigned n = 0; n < cases[i].count; ++n)
      document.append(cases[i].bytes);
    target.put("sentinel");
    const Snapshot before(document.lines);
    const bool available = document.availability() == EDITOR_OK;
    LOKA_VERIFY(available == cases[i].accepted);
    const TextDocumentResult result = WriteTextDocument(&target.context, target.file, document.lines);
    LOKA_VERIFY((result == TEXT_DOCUMENT_OK) == available);
    before.unchanged(document.lines);
    if (!available)
      LOKA_VERIFY(target.bytes() == "sentinel");
    else
    {
      Document loaded;
      LOKA_VERIFY(ReadTextDocument(&target.context, target.file, loaded.lines) == TEXT_DOCUMENT_OK);
      LOKA_VERIFY(loaded.availability() == EDITOR_OK);
      LOKA_VERIFY(loaded.lines.size() == document.lines.size());
      for (unsigned short j = 0; j < document.lines.size(); ++j)
        LOKA_VERIFY(row(loaded.lines, j) == row(document.lines, j));
    }
  }
}

void testTextDocumentResolution()
{
  TempFile source;
  source.put("decoy");
  Document document;
  document.append("kept");
  const Snapshot before(document.lines);
  const unsigned char locator[] = {1, 2, 3};
  File located;
  LOKA_VERIFY(loka::platform::file::FileLocatorAccess::capture(
      String(source.path), File::KIND_FILE, locator, sizeof(locator), located));
  LOKA_VERIFY(ReadTextDocument(&source.context, located, document.lines) == TEXT_DOCUMENT_READ_FAILED);
  before.unchanged(document.lines);
  struct MissingResolvedContext : NullPlatformContext
  {
    std::string missingPath;
    mutable unsigned calls;
    explicit MissingResolvedContext(const std::string &path)
        : missingPath(path),
          calls(0)
    {
    }
    virtual bool openFile(const File &, loka::platform::file::FileHandle &out) const
    {
      ++this->calls;
      out = loka::platform::file::FileHandle();
      out.displayPath = String(this->missingPath);
      return true;
    }
  } missingResolved(source.directory + "/absent.txt");
  LOKA_VERIFY(ReadTextDocument(&missingResolved, located, document.lines) == TEXT_DOCUMENT_READ_FAILED);
  before.unchanged(document.lines);
  const File refused = located << File("child");
  missingResolved.calls = 0;
  LOKA_VERIFY(ReadTextDocument(&missingResolved, refused, document.lines) == TEXT_DOCUMENT_READ_FAILED);
  LOKA_VERIFY(missingResolved.calls == 0);
  LOKA_VERIFY(ReadTextDocument(&source.context, refused, document.lines) == TEXT_DOCUMENT_READ_FAILED);
  before.unchanged(document.lines);
  LOKA_VERIFY(ReadTextDocument(&source.context, File(source.path.c_str()), document.lines) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(row(document.lines, 0) == "decoy");
  LOKA_VERIFY(ReadTextDocument(&missingResolved, File(source.path.c_str()), document.lines) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(row(document.lines, 0) == "decoy");
  LOKA_VERIFY(std::remove(source.path.c_str()) == 0);
  const Snapshot missing(document.lines);
  LOKA_VERIFY(ReadTextDocument(&source.context, source.file, document.lines) == TEXT_DOCUMENT_READ_FAILED);
  missing.unchanged(document.lines);
  // An application-relative File that no context resolved has no path to
  // flatten: File::toString() asserts on BASE_APPLICATION, so it must refuse.
  LOKA_VERIFY(ReadTextDocument(0, File::Application() << File("absent.txt"), document.lines)
              == TEXT_DOCUMENT_READ_FAILED);
  missing.unchanged(document.lines);
}

void testTextDocumentWriteFailures()
{
  TempFile target;
  Document document;
  document.append("new contents");
  const Snapshot original(document.lines);
  NullPlatformContext missingContext;
  missingContext.setApplicationDirectory(String(target.directory + "/missing"));
  const File missing = File::Application() << File("document.txt");
  loka::platform::file::FileHandle missingHandle;
  LOKA_VERIFY(missingContext.openFile(missing, missingHandle));
  LOKA_VERIFY(WriteTextDocument(&missingContext, missing, document.lines) == TEXT_DOCUMENT_OPEN_FAILED);
  original.unchanged(document.lines);

  // Invalid content must be refused before truncation, even with a valid path.
  target.put("untouched");
  Document invalid;
  invalid.append(std::string("a\0b", 3));
  LOKA_VERIFY(WriteTextDocument(&target.context, target.file, invalid.lines) == TEXT_DOCUMENT_NON_ASCII);
  LOKA_VERIFY(target.bytes() == "untouched");
  LOKA_VERIFY(WriteTextDocument(&missingContext, missing, invalid.lines) == TEXT_DOCUMENT_NON_ASCII);

  struct stat status;
  if (stat("/dev/full", &status) != 0)
  {
    LOKA_VERIFY(errno == ENOENT);
    std::puts("[skip] /dev/full absent: real stdio WRITE/FLUSH failure pins");
    return;
  }
  LOKA_VERIFY(S_ISCHR(status.st_mode));
  Document maximum;
  maximum.append(std::string(8192, 'x'));
  const Snapshot maxOriginal(maximum.lines);
  NullPlatformContext deviceContext;
  deviceContext.setApplicationDirectory(String("/dev"));
  const File full = File::Application() << File("full");
  // Empirical generic-rail probe: small buffered writes fail at fflush; the
  // maximum document reaches the device during fwrite and preserves WRITE_FAILED.
  const int descriptors = openDescriptorCount();
  LOKA_VERIFY(WriteTextDocument(&deviceContext, full, document.lines) == TEXT_DOCUMENT_FLUSH_FAILED);
  if (descriptors >= 0)
    LOKA_VERIFY(openDescriptorCount() == descriptors);
  LOKA_VERIFY(WriteTextDocument(&deviceContext, full, maximum.lines) == TEXT_DOCUMENT_WRITE_FAILED);
  if (descriptors >= 0)
    LOKA_VERIFY(openDescriptorCount() == descriptors);
  original.unchanged(document.lines);
  maxOriginal.unchanged(maximum.lines);
}

void testTextDocumentRawAdmission()
{
  BlobAllocationProbe allocation;
  struct CapacityContext : NullPlatformContext
  {
    mutable unsigned queries;
    CapacityContext()
        : queries(0)
    {
    }
    virtual bool queryLargestContiguousAllocation(std::size_t &out) const
    {
      ++this->queries;
      out = 1024 * 1024;
      return true;
    }
  } context;
  TempFile source;
  context.setApplicationDirectory(String(source.directory));
  Document document;
  std::string raw(7937, 'a');
  for (unsigned i = 0; i < 255; ++i)
    raw += "\r\n";
  source.put(raw);
  LOKA_VERIFY(ReadTextDocument(&context, source.file, document.lines) == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(context.queries == 0);
  const Snapshot allocatedBefore(document.lines);
  const char *sites[][2] = {{"Blob", "Record"}, {"Blob", "Bytes"}};
  for (unsigned i = 0; i < 2; ++i)
  {
    allocation.refuse(sites[i][0], sites[i][1]);
    LOKA_VERIFY(ReadTextDocument(&context, source.file, document.lines) == TEXT_DOCUMENT_ALLOCATION);
    allocatedBefore.unchanged(document.lines);
  }
  context.queries = 0;
  raw += 'a';
  source.put(raw);
  const Snapshot before(document.lines);
  LOKA_VERIFY(ReadTextDocument(&context, source.file, document.lines) == TEXT_DOCUMENT_TOO_LARGE);
  LOKA_VERIFY(context.queries == 0);
  before.unchanged(document.lines);
}

#else
#define LOKA_GENERIC_TEXT_SKIP(name)                                                                                   \
  void name()                                                                                                          \
  {                                                                                                                    \
    std::puts("SKIP: generic Linux text document integration");                                                        \
  }
LOKA_GENERIC_TEXT_SKIP(testTextDocumentSplitMatrix)
LOKA_GENERIC_TEXT_SKIP(testTextDocumentLimits)
LOKA_GENERIC_TEXT_SKIP(testTextDocumentReplacement)
LOKA_GENERIC_TEXT_SKIP(testTextDocumentCommitRefusals)
LOKA_GENERIC_TEXT_SKIP(testTextDocumentPublication)
LOKA_GENERIC_TEXT_SKIP(testTextDocumentRoundTrip)
LOKA_GENERIC_TEXT_SKIP(testTextDocumentEditorParity)
LOKA_GENERIC_TEXT_SKIP(testTextDocumentResolution)
LOKA_GENERIC_TEXT_SKIP(testTextDocumentWriteFailures)
LOKA_GENERIC_TEXT_SKIP(testTextDocumentRawAdmission)
#undef LOKA_GENERIC_TEXT_SKIP
#endif
