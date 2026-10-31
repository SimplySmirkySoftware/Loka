#include "support/TextBufferFixture.hpp"
#include "support/BlobAllocationProbe.hpp"
#include "ToolboxFileHost.hpp"
#include "ToolboxPlatformContext.hpp"
#include "ToolboxFileChoice.hpp"
#include "ToolboxByteSource.hpp"
#include "app/FileImageSource.hpp"
#include "app/TextDocumentFile.hpp"
#include "support/TestVerify.hpp"
#include "support/FileRefusalPin.hpp"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "Script.h"
#include "StandardFile.h"
#include "context/ToolboxOpenFileDialogContext.hpp"
#include "platform/file/FileLocatorAccess.hpp"
#include "platform/ToolboxPascalText.hpp"
#include "platform/StringUTF8.hpp"
#include "SimpleViewerFlowAdapters.hpp"
#include "support/LokaAllocFailure.hpp"
#include "ToolboxBusy.hpp"

static StandardFileReply reply;
static unsigned getCalls = 0, putCalls = 0;
static std::string savedPrompt, savedDefault;
static void (*duringModal)() = 0;
void StandardGetFile(void *, short count, void *, StandardFileReply *out)
{
  LOKA_VERIFY(count == -1);
  ++getCalls;
  if (duringModal) duringModal();
  *out = reply;
}
void StandardPutFile(const unsigned char *prompt, const unsigned char *name, StandardFileReply *out)
{
  ++putCalls;
  savedPrompt.assign(reinterpret_cast<const char *>(prompt + 1), prompt[0]);
  savedDefault.assign(reinterpret_cast<const char *>(name + 1), name[0]);
  if (duringModal) duringModal();
  *out = reply;
}

// Only unrelated UI/heap virtuals are substituted. openFile and capture
// are linked from the same production translation unit as Classic builds.
ToolboxPlatformContext::ToolboxPlatformContext() {}
ToolboxPlatformContext::~ToolboxPlatformContext() {}
App *ToolboxPlatformContext::createApp(AppConfigurable *, NativeModuleHandle, int) const { return 0; }
Window *ToolboxPlatformContext::createWindow(const WindowProps &) { return 0; }
loka::app::scene::NodeContext *ToolboxPlatformContext::createNodeContext(loka::app::scene::Node *) const { return 0; }
bool ToolboxPlatformContext::queryMainDisplayDepth(int &) const
{
  return false;
}
bool ToolboxPlatformContext::queryLargestContiguousAllocation(std::size_t &) const { return false; }
bool ToolboxPlatformContext::createImageFromBlob(const loka::core::resource::Blob &, std::size_t,
    std::size_t, loka::core::resource::Image &) { return false; }

using loka::file::File;
using loka::core::String;
using namespace loka::platform::file;
using namespace toolbox_file_host;

typedef char ClassicSpecSize[sizeof(FSSpec) == 70 ? 1 : -1];
typedef char ClassicParentOffset[offsetof(FSSpec, parID) == 2 ? 1 : -1];
typedef char ClassicNameOffset[offsetof(FSSpec, name) == 6 ? 1 : -1];

/** Native capture tests seal through the same Window ledger as presentation. */
static void RunFileToLedger(const loka::app::OpenFileDialogProps &props)
{
  ToolboxWindow window;
  ToolboxApp app(window);
  loka::app::DialogResultTransport::Registration *registration = window.results.reserve(props);
  LOKA_VERIFY(registration != 0);
  {
    loka::app::DialogResultTransport::ReturnPort port(registration);
    LOKA_VERIFY(port.seal(RunToolboxFileDialog(props.options_)) == &window);
  }
  app.flush();
  delete registration;
  app.flush();
}

// The production dialog consumes a fake Standard File reply and publishes
// through its real NodeState door; retained choices outlive every temporary.
static File Choose(const FSSpec &spec)
{
  typedef loka::app::FileChooserResult Result;
  loka::core::MutableState<Result> storage;
  loka::core::PushStateTracker tracker;
  tracker.addState(&storage);
  loka::app::scene::NodeState<Result> state(&storage, &tracker);
  loka::app::OpenFileDialogNode node(loka::app::OpenFileDialogProps().result(state));
  reply.sfGood = true;
  reply.sfFile = spec;
  RunFileToLedger(node.props);
  LOKA_VERIFY(storage.get().kind == Result::RESULT_FILE);
  const File chosen = storage.get().item;
  tracker.removeState(&storage);
  return chosen;
}
static std::string Read(ToolboxPlatformContext &context, const File &file)
{
  FileHandle handle;
  LOKA_VERIFY(context.openFile(file, handle));
  LOKA_VERIFY(handle.hasSpec);
  loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
  LOKA_VERIFY(ReadBytes(handle, bytes) == READ_OK);
  LOKA_VERIFY(OpenCount() == 0);
  return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}
static void Tuple()
{
  const FSSpec specs[] = { Spec(-7, 0x12345678, "Photo.PICT"),
    Spec(-7, 0x12345679, "Photo.PICT"), Spec(-8, 0x12345678, "Photo.PICT"),
    Spec(-7, 0x12345678, std::string("Photo\0PICT", 10)) };
  for (unsigned i = 0; i < 4; ++i) Put(specs[i], std::string(3, static_cast<char>('A' + i)));
  for (unsigned i = 0; i < 4; ++i)
  {
    FSSpec poisoned = specs[i];
    std::memset(poisoned.name + poisoned.name[0] + 1, 0xAD, 63 - poisoned.name[0]);
    loka::toolbox::ToolboxByteSource source;
    LOKA_VERIFY(source.open(poisoned));
    std::size_t size = 0;
    LOKA_VERIFY(source.size(size));
    LOKA_VERIFY(size == 3);
    unsigned char byte = 0;
    LOKA_VERIFY(source.readAt(0, &byte, 1));
    LOKA_VERIFY(byte == 'A' + i);
    LOKA_VERIFY(source.readAt(2, &byte, 1));
    LOKA_VERIFY(!source.readAt(3, &byte, 1));
    source.close();
    LOKA_VERIFY(OpenCount() == 0);
  }
  {
    loka::toolbox::ToolboxByteSource first;
    loka::toolbox::ToolboxByteSource second;
    LOKA_VERIFY(first.open(specs[0]));
    LOKA_VERIFY(second.open(specs[1]));
    LOKA_VERIFY(OpenCount() == 2);
    unsigned char bytes[2] = {};
    LOKA_VERIFY(first.readAt(1, bytes, 2));
    LOKA_VERIFY(bytes[0] == 'A' && bytes[1] == 'A');
    LOKA_VERIFY(second.readAt(1, bytes, 2));
    LOKA_VERIFY(bytes[0] == 'B' && bytes[1] == 'B');
  }
  LOKA_VERIFY(OpenCount() == 0); // Destruction closes both independent marks.
  short ref = 0;
  LOKA_VERIFY(FSpOpenDF(&specs[0], fsRdPerm, &ref) == noErr);
  unsigned char partial[4] = {};
  long count = 4;
  LOKA_VERIFY(FSRead(ref, &count, partial) == eofErr);
  LOKA_VERIFY(count == 3 && partial[0] == 'A' && partial[2] == 'A');
  count = 1;
  LOKA_VERIFY(FSRead(ref, &count, partial) == eofErr);
  LOKA_VERIFY(count == 0);
  LOKA_VERIFY(FSClose(ref) == noErr);
  LOKA_VERIFY(FSClose(ref) == paramErr);
  loka::toolbox::ToolboxByteSource source;
  LOKA_VERIFY(!source.open(Spec(-7, 999, "Photo.PICT")));
}
static void Single()
{
  ToolboxPlatformContext context;
  const FSSpec spec = Spec(-7, 0x12345678, "Single.PICT");
  Put(spec, "chosen contents");
  const File chosen = Choose(spec);
  LOKA_VERIFY(Read(context, chosen) == "chosen contents");
  FileHandle handle;
  LOKA_VERIFY(context.openFile(chosen, handle));
  LOKA_VERIFY(handle.spec.vRefNum == spec.vRefNum && handle.spec.parID == spec.parID);
  LOKA_VERIFY(handle.kind == File::KIND_FILE);
}
static void Application(const bool viewer, const bool smirky)
{
  ToolboxPlatformContext context;
  SetApplication(Spec(-2, 0x23456789, "App"));
  const File chosen("Fixture.PICT");
  Put(Spec(-2, 0x23456789, "Fixture.PICT"), "application contents");
  const File item = File::Application() << chosen;
  LOKA_VERIFY(Read(context, item) == "application contents");
  FileHandle handle;
  LOKA_VERIFY(context.openFile(item, handle));
  LOKA_VERIFY(handle.spec.vRefNum == -2 && handle.spec.parID == 0x23456789);
  if (viewer)
  {
    // SimpleViewerScenarioDriver::openImage probes the data fork first.
    short ref = 0;
    LOKA_VERIFY(FSpOpenDF(&handle.spec, fsRdPerm, &ref) == noErr);
    long size = 0;
    LOKA_VERIFY(GetEOF(ref, &size) == noErr);
    LOKA_VERIFY(FSClose(ref) == noErr);
    LOKA_VERIFY(size == 20);
  }
  if (viewer || smirky)
  {
    // Same resolve/capture sequence as both scenario stand-ins. Their
    // controller/JS delivery is covered elsewhere, not compiled here.
    File captured;
    LOKA_VERIFY(ToolboxCaptureChosenFile(handle.spec, captured));
    LOKA_VERIFY(Read(context, captured) == "application contents");
  }
  LOKA_VERIFY(!context.openFile(File::Application(), handle));
  LOKA_VERIFY(!handle.hasSpec);
}
static void Collision(bool reverse)
{
  ToolboxPlatformContext context;
  const FSSpec a = Spec(-7, 101, "Photo.PICT");
  const FSSpec b = Spec(-7, 202, "Photo.PICT");
  Put(a, "A contents");
  Put(b, "B contents");
  const File retained = Choose(reverse ? b : a);
  Choose(reverse ? a : b);
  const std::string actual = Read(context, retained);
  const std::string expected = reverse ? "B contents" : "A contents";
  std::fprintf(stderr, "retained choice: expected '%s', read '%s'\n", expected.c_str(), actual.c_str());
  LOKA_VERIFY(actual == expected);
}
static void Decoy()
{
  // Private temporary directory prevents collisions or overwriting user files.
  char directory[] = "/tmp/loka-file-choice-XXXXXX";
  LOKA_VERIFY(mkdtemp(directory) != 0);
  const std::string path = std::string(directory) + "/Photo.PICT";
  std::FILE *stream = std::fopen(path.c_str(), "wb");
  LOKA_VERIFY(stream != 0);
  LOKA_VERIFY(std::fwrite("decoy", 1, 5, stream) == 5);
  LOKA_VERIFY(std::fclose(stream) == 0);
  char cwd[4096];
  LOKA_VERIFY(getcwd(cwd, sizeof(cwd)) != 0);
  LOKA_VERIFY(chdir(directory) == 0);
  ToolboxPlatformContext context;
  const File chosen = Choose(Spec(-7, 999, "Photo.PICT")); // No native file installed.
  FileHandle handle;
  LOKA_VERIFY(context.openFile(chosen, handle));
  loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
  LOKA_VERIFY(ReadBytes(handle, bytes) == READ_NATIVE_OPEN_FAILED);
  loka::core::resource::Blob blob;
  const ReadResult result = loka::app::ReadFileImageBlob(&context, chosen, blob);
  const std::string actual(blob.size() == 0 ? "" :
      reinterpret_cast<const char *>(blob.data()), blob.size());
  const FSSpec live = Spec(-7, 999, "Photo.PICT");
  Put(live, "native");
  FailRead(SizeFailure);
  LOKA_VERIFY(loka::app::ReadFileImageBlob(&context, chosen, blob) == READ_NATIVE_SIZE_FAILED);
  FailRead(DataFailure);
  LOKA_VERIFY(loka::app::ReadFileImageBlob(&context, chosen, blob) == READ_NATIVE_READ_FAILED);
  FailRead(NoFailure);
  Remove(live);
  File malformed;
  const unsigned char bad[] = {1};
  LOKA_VERIFY(FileLocatorAccess::capture(chosen.toString(), File::KIND_FILE, bad, sizeof(bad), malformed));
  LOKA_VERIFY(loka::app::ReadFileImageBlob(&context, malformed, blob) == READ_NO_NATIVE_SPEC);
  const File refused = chosen << File("child");
  LOKA_VERIFY(loka::app::ReadFileImageBlob(&context, refused, blob) == READ_NO_NATIVE_SPEC);
  LOKA_VERIFY(loka::app::ReadFileImageBlob(&context, File("Photo.PICT"), blob) == READ_OK);
  LOKA_VERIFY(std::string(blob.size() == 0 ? "" :
      reinterpret_cast<const char *>(blob.data()), blob.size()) == "decoy");
  // Both failed native entries must stay terminal through the real viewer client.
  const File failures[] = {chosen, malformed, refused};
  for (unsigned i = 0; i < 3; ++i)
  {
    simpleviewer::ChooserProjection projection;
    projection.request.setFilePath(String::Literal("Photo.PICT"));
    projection.hasFileItem = true;
    projection.fileItem = failures[i];
    loka::dsl::FlowError error;
    LOKA_VERIFY(simpleviewer::ProjectionToBlobAdapter(&context).run(projection, blob, error)
                == loka::dsl::FLOW_STEP_FAILED);
    LOKA_VERIFY(error.code == (i == 0 ? 1008 : 1007));
  }
  LOKA_VERIFY(chdir(cwd) == 0);
  LOKA_VERIFY(std::remove(path.c_str()) == 0);
  LOKA_VERIFY(rmdir(directory) == 0);
  std::fprintf(stderr, "native open failed; fallback result=%s contents='%s'\n",
      loka::app::FileImageReadResultName(result), actual.c_str());
  LOKA_VERIFY(result == READ_NATIVE_OPEN_FAILED);
  LOKA_VERIFY(actual != "decoy");
}

static void Canonical()
{
  ToolboxPlatformContext context;
  FSSpec spec = Spec(-7, 0x76543210, std::string("A\0\x8e", 3));
  const File first = Choose(spec);
  const unsigned char expected[] = {0xff, 0xf9, 0x76, 0x54, 0x32, 0x10, 3, 'A', 0, 0x8e};
  const unsigned char *bytes = 0;
  std::size_t size = 0;
  LOKA_VERIFY(FileLocatorAccess::query(first, bytes, size));
  LOKA_VERIFY(size == sizeof(expected) && !std::memcmp(bytes, expected, size));
  std::memset(spec.name + 4, 0xad, 60);
  const File equal = Choose(spec);
  LOKA_VERIFY(!(first != equal) && first != File(first.toString()));
  Put(spec, "original");
  FSSpec decoded;
  std::memset(&decoded, 0xcd, sizeof(decoded));
  LOKA_VERIFY(QueryToolboxSpec(equal, decoded));
  LOKA_VERIFY(decoded.vRefNum == -7 && decoded.parID == 0x76543210);
  LOKA_VERIFY(!std::memcmp(decoded.name, spec.name, 4));
  for (unsigned i = 4; i < sizeof(decoded.name); ++i) LOKA_VERIFY(decoded.name[i] == 0);
  spec.vRefNum = -8;
  LOKA_VERIFY(first != Choose(spec));
  spec.vRefNum = -7;
  spec.parID++;
  LOKA_VERIFY(first != Choose(spec));
  spec = Spec(-32768, static_cast<int32_t>(-1985229329), "signed");
  LOKA_VERIFY(QueryToolboxSpec(Choose(spec), decoded));
  LOKA_VERIFY(decoded.vRefNum == spec.vRefNum && decoded.parID == spec.parID);
  spec.name[1] = 'X';
  LOKA_VERIFY(Read(context, first) == "original");
}
static void Validation()
{
  ToolboxPlatformContext context;
  const File original = Choose(Spec(-1, 9, "kept"));
  FSSpec before;
  std::memset(&before, 0xa5, sizeof(before));
  unsigned char bytes[72] = {};
  const unsigned lengths[] = {0, 1, 6, 7, 8, 9, 70, 71, 72};
  const unsigned names[] = {0, 1, 63, 64, 255};
  for (unsigned i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i)
    for (unsigned j = 0; j < sizeof(names)/sizeof(names[0]); ++j)
    {
      bytes[6] = static_cast<unsigned char>(names[j]);
      if (names[j] && names[j] <= 63 && lengths[i] == 7 + names[j]) continue;
      File malformed;
      LOKA_VERIFY(FileLocatorAccess::capture(String::Literal("kept"), File::KIND_FILE, bytes, lengths[i], malformed));
      FSSpec out = before;
      LOKA_VERIFY(!QueryToolboxSpec(malformed, out));
      LOKA_VERIFY(!std::memcmp(&out, &before, sizeof(out)));
      FileHandle handle;
      LOKA_VERIFY(context.openFile(original, handle));
      LOKA_VERIFY(!context.openFile(malformed, handle));
      LOKA_VERIFY(!handle.hasSpec && handle.displayPath.empty());
    }
  FSSpec out = before;
  LOKA_VERIFY(!QueryToolboxSpec(File("kept"), out));
  LOKA_VERIFY(!std::memcmp(&out, &before, sizeof(out)));
  FSSpec invalid = Spec(1, 2, "kept");
  File kept = original;
  invalid.name[0] = 0;
  LOKA_VERIFY(!ToolboxCaptureChosenFile(invalid, kept) && !(kept != original));
  invalid.name[0] = 64;
  LOKA_VERIFY(!ToolboxCaptureChosenFile(invalid, kept) && !(kept != original));
}
static void Allocation()
{
  using namespace loka::core::testing;
  const File original = Choose(Spec(-1, 9, "kept"));
  const FSSpec spec = Spec(-2, 10, "new");
  File out = original;
  failLokaAllocRaw("FileLocator", "Payload", 1);
  LOKA_VERIFY(!ToolboxCaptureChosenFile(spec, out));
  LOKA_VERIFY(!(out != original) && lokaAllocRawLive() == 0);
  allowLokaAllocRaw();
  // Display construction also uses Managed; permit it, refuse locator adoption.
  failLokaAllocRaw("Managed", "ControlBlock", 2);
  LOKA_VERIFY(!ToolboxCaptureChosenFile(spec, out));
  LOKA_VERIFY(!(out != original) && lokaAllocRawLive() == 0);
  allowLokaAllocRaw();
}
static void Display()
{
  ToolboxPlatformContext context;
  toolbox_host::systemScript = smRoman;
  for (unsigned i = 128; i < 256; ++i)
  {
    const unsigned char byte = static_cast<unsigned char>(i);
    String strict;
    LOKA_VERIFY(ToolboxDecodeNative(&byte, 1, strict));
    const unsigned reads = toolbox_host::scriptReads;
    LOKA_VERIFY(ToolboxChosenFileDisplayName(&byte, 1).equals(strict));
    LOKA_VERIFY(toolbox_host::scriptReads == reads + 1);
  }
  LOKA_VERIFY(Choose(Spec(1, 2, "Caf\x8e")).toString().equals(String::Literal("Caf\xc3\xa9")));
  toolbox_host::systemScript = 1;
  const unsigned char sjis[] = {'A', 0, 0x83, 0x5c, 0x8e, 0x9a};
  const std::string expected("A\0?\\??", 6);
  std::string actual;
  LOKA_VERIFY(loka::platform::CollectUtf8(ToolboxChosenFileDisplayName(sjis, sizeof(sjis)), actual));
  LOKA_VERIFY(actual == expected);
  String unchanged("kept");
  LOKA_VERIFY(!ToolboxDecodeNative(sjis, sizeof(sjis), unchanged));
  LOKA_VERIFY(unchanged.equals(String::Literal("kept")));
  const FSSpec a = Spec(-1, 2, "\x80.PICT"), b = Spec(-1, 2, "\x81.PICT");
  Put(a, "A"); Put(b, "B");
  const File first = Choose(a), second = Choose(b);
  LOKA_VERIFY(first.toString().equals(second.toString()) && first != second);
  LOKA_VERIFY(Read(context, first) == "A" && Read(context, second) == "B");
  toolbox_host::systemScript = smRoman;
}
static void CountDelivery(void *data) { ++*static_cast<unsigned *>(data); }
static void Copies()
{
  using loka::app::FileChooserResult;
  ToolboxPlatformContext context;
  const FSSpec a = Spec(-1, 1, "same"), b = Spec(-1, 2, "same");
  Put(a, "A"); Put(b, "B");
  FileChooserResult saved;
  { const File temporary = Choose(a); saved = FileChooserResult::File(temporary); }
  LOKA_VERIFY(Read(context, saved.item) == "A");
  loka::core::MutableState<FileChooserResult> storage(saved);
  loka::core::PushStateTracker tracker;
  tracker.addState(&storage);
  loka::app::scene::NodeState<FileChooserResult> state(&storage, &tracker);
  unsigned notifications = 0;
  storage.bind(&CountDelivery, &notifications, false);
  state.set(FileChooserResult::File(Choose(b)), true);
  LOKA_VERIFY(notifications == 1);
  state.set(FileChooserResult::File(Choose(b)), true);
  LOKA_VERIFY(notifications == 2);
  LOKA_VERIFY(Read(context, state.get().item) == "B");
  simpleviewer::ChooserContext chooser;
  simpleviewer::ChooserProjection projection;
  loka::dsl::FlowError error;
  LOKA_VERIFY(simpleviewer::ChooserToContextAdapter().run(state.get(), chooser, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(Read(context, chooser.result.item) == "B");
  LOKA_VERIFY(simpleviewer::ContextToProjectionAdapter().run(chooser, projection, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(Read(context, projection.fileItem) == "B");
  loka::core::resource::Blob blob;
  LOKA_VERIFY(simpleviewer::ProjectionToBlobAdapter(&context).run(projection, blob, error) == loka::dsl::FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(blob.size() == 1 && blob.data()[0] == 'B');
  storage.unbind(&CountDelivery, &notifications);
  tracker.removeState(&storage);
}
static void Validity()
{
  ToolboxPlatformContext context;
  // Each event invalidates the captured address, not the immutable value.
  // The fake models catalog facts after the operation, not OS event dispatch.
  const char *events[] = {"eject", "delete", "rename", "move"};
  for (unsigned event = 0; event < 4; ++event)
  {
    const FSSpec old = Spec(-7, 100, "Photo.PICT");
    Put(old, "original");
    const File saved = Choose(old);
    Remove(old);
    FSSpec replacement = old;
    if (event == 0) replacement.vRefNum = -8;
    if (event == 2) replacement.name[1] = 'X';
    if (event == 3) replacement.parID = 101;
    if (event != 1) Put(replacement, "moved");
    loka::core::resource::Blob blob;
    LOKA_VERIFY(loka::app::ReadFileImageBlob(&context, saved, blob) == READ_NATIVE_OPEN_FAILED);
    std::fprintf(stderr, "%s: old address failed terminally\n", events[event]);
    if (event != 1) Remove(replacement);
    // Replacement/reused volume reference is deliberately not object identity.
    Put(old, "replacement");
    LOKA_VERIFY(Read(context, saved) == "replacement");
    Remove(old);
  }
}
static void Dialog()
{
  using loka::app::FileChooserResult;
  using namespace loka::core::testing;
  ToolboxPlatformContext platform;
  loka::core::MutableState<FileChooserResult> storage;
  loka::core::PushStateTracker tracker;
  tracker.addState(&storage);
  loka::app::scene::NodeState<FileChooserResult> state(&storage, &tracker);
  loka::app::OpenFileDialogNode node(loka::app::OpenFileDialogProps().result(state));
  reply.sfGood = true;
  reply.sfFile = Spec(-1, 10, "dialog");
  Put(reply.sfFile, "dialog contents");
  {
    RunFileToLedger(node.props);
  }
  LOKA_VERIFY(storage.get().kind == FileChooserResult::RESULT_FILE);
  LOKA_VERIFY(Read(platform, storage.get().item) == "dialog contents");
  state.set(FileChooserResult());
  {
    failLokaAllocRaw("FileLocator", "Payload", 1);
    RunFileToLedger(node.props);
    LOKA_VERIFY(storage.get().kind == FileChooserResult::RESULT_ERROR);
    LOKA_VERIFY(storage.get().item.locator().empty());
    allowLokaAllocRaw();
  }
  tracker.removeState(&storage);
}
static void SaveDialog()
{
  using namespace loka::app;
  using namespace loka::core::testing;
  loka::core::MutableState<FileChooserResult> storage;
  loka::core::PushStateTracker tracker;
  tracker.addState(&storage);
  loka::app::scene::NodeState<FileChooserResult> state(&storage, &tracker);
  unsigned notifications = 0;
  storage.bind(&CountDelivery, &notifications, false);
  OpenFileDialogDefinition definition = SaveFileDialog(String::Literal("default")).result(state);
  OpenFileDialogNode node(definition.props);
  reply.sfGood = true;
  reply.sfFile = Spec(-7, 123, std::string(31, 'n'));
  File first;
  for (unsigned i = 0; i != 2; ++i)
  {
    RunFileToLedger(node.props);
    LOKA_VERIFY(storage.get().kind == FileChooserResult::RESULT_FILE);
    if (!i) first = storage.get().item;
    else LOKA_VERIFY(!(first != storage.get().item));
  }
  LOKA_VERIFY(notifications == 2 && putCalls == 2 && getCalls == 0);
  LOKA_VERIFY(savedPrompt == "Save as:" && savedDefault == "default");
  FSSpec captured;
  LOKA_VERIFY(QueryToolboxSpec(first, captured));
  LOKA_VERIFY(captured.vRefNum == -7 && captured.parID == 123 && captured.name[0] == 31);
  ToolboxPlatformContext platform;
  FileHandle handle;
  LOKA_VERIFY(platform.openFile(first, handle));
  loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
  LOKA_VERIFY(ReadBytes(handle, bytes) == READ_NATIVE_OPEN_FAILED); // No file created.

  const unsigned invalidLengths[] = {0, 32, 63};
  for (unsigned i = 0; i != 3; ++i)
  {
    reply.sfFile = Spec(-7, 123, std::string(invalidLengths[i], 'n'));
    RunFileToLedger(node.props);
    LOKA_VERIFY(storage.get().kind == FileChooserResult::RESULT_ERROR && storage.get().errorCode == paramErr);
  }
  reply.sfGood = false;
  {
    RunFileToLedger(node.props);
    LOKA_VERIFY(storage.get().kind == FileChooserResult::RESULT_CANCELED);
  }
  reply.sfGood = true;
  reply.sfFile = Spec(-7, 123, "n");
  {
    failLokaAllocRaw("FileLocator", "Payload", 1);
    RunFileToLedger(node.props);
    LOKA_VERIFY(storage.get().kind == FileChooserResult::RESULT_ERROR && storage.get().errorCode == memFullErr);
    allowLokaAllocRaw();
  }
  const String names[] = {String(std::string(31, 'a')), String(std::string(32, 'a')),
      String::Literal("\xf0\x9f\x98\x80"), String::Literal("Caf\xc3\xa9"), String()};
  for (unsigned i = 0; i != 5; ++i)
  {
    node.props.options_ = FileDialogOptions(FILE_DIALOG_SAVE, names[i]);
    const unsigned before = putCalls;
    RunFileToLedger(node.props);
    if (i == 1 || i == 2)
    {
      LOKA_VERIFY(putCalls == before);
      LOKA_VERIFY(storage.get().kind == FileChooserResult::RESULT_ERROR && storage.get().errorCode == paramErr);
    }
    else
    {
      LOKA_VERIFY(putCalls == before + 1 && storage.get().kind == FileChooserResult::RESULT_FILE);
      if (i == 0) LOKA_VERIFY(savedDefault == std::string(31, 'a'));
      if (i == 3) LOKA_VERIFY(savedDefault == "Caf\x8e");
      if (i == 4) LOKA_VERIFY(savedDefault.empty());
    }
  }
  toolbox_host::systemScript = 1;
  node.props.options_ = FileDialogOptions(FILE_DIALOG_SAVE, String::Literal("Caf\xc3\xa9"));
  {
    const unsigned before = putCalls;
    RunFileToLedger(node.props);
    LOKA_VERIFY(putCalls == before && storage.get().kind == FileChooserResult::RESULT_ERROR);
  }
  toolbox_host::systemScript = smRoman;
  // OPEN's locator envelope remains 63 bytes; SAVE alone has the HFS cap.
  reply.sfFile = Spec(-7, 123, std::string(32, 'n'));
  node.props.options_ = FileDialogOptions();
  {
    RunFileToLedger(node.props);
    LOKA_VERIFY(getCalls == 1 && storage.get().kind == FileChooserResult::RESULT_FILE);
  }
  storage.unbind(&CountDelivery, &notifications);
  tracker.removeState(&storage);
}
static void Refused()
{
  ToolboxPlatformContext context;
  VerifyFileRefusal(context);
}
static void Prepare()
{
  FileHandle file;
  FailPrepare(noErr, noErr);
  LOKA_VERIFY(PrepareTextDocumentDestination(file) == PREPARE_NO_NATIVE_SPEC);
  LOKA_VERIFY(CatalogCalls() == 0 && CreateCalls() == 0);
  file.hasSpec = true;
  file.spec = Spec(13, 42, "text");
  Remove(file.spec);
  LOKA_VERIFY(PrepareTextDocumentDestination(file) == PREPARE_OK);
  LOKA_VERIFY(CatalogCalls() == 1 && CreateCalls() == 1);
  LOKA_VERIFY(Metadata(file.spec).fdType == 0x54455854UL);
  LOKA_VERIFY(Metadata(file.spec).fdCreator == 0x74747874UL);
  LOKA_VERIFY(CreatedScript() == smSystemScript);

  SetMetadata(file.spec, 0x54455854UL, 0x4F544852UL);
  FailPrepare(noErr, noErr);
  LOKA_VERIFY(PrepareTextDocumentDestination(file) == PREPARE_OK);
  LOKA_VERIFY(CatalogCalls() == 1 && CreateCalls() == 0);
  LOKA_VERIFY(Metadata(file.spec).fdCreator == 0x4F544852UL);

  SetMetadata(file.spec, 0x42494E41UL, 0x4F544852UL);
  LOKA_VERIFY(PrepareTextDocumentDestination(file) == PREPARE_NOT_TEXT);
  LOKA_VERIFY(CreateCalls() == 0);
  LOKA_VERIFY(Metadata(file.spec).fdType == 0x42494E41UL);

  Remove(file.spec);
  FailPrepare(noErr, paramErr);
  LOKA_VERIFY(PrepareTextDocumentDestination(file) == PREPARE_CREATE_FAILED);
  LOKA_VERIFY(CatalogCalls() == 1 && CreateCalls() == 1);
  FailPrepare(paramErr, noErr);
  LOKA_VERIFY(PrepareTextDocumentDestination(file) == PREPARE_CATALOG_FAILED);
  LOKA_VERIFY(CatalogCalls() == 1 && CreateCalls() == 0);
  FailPrepare(noErr, noErr);
}
static void PrepareRefusesWrite()
{
  const FSSpec spec = Spec(13, 42, "non-text-document");
  Put(spec, "original binary contents");
  SetMetadata(spec, 0x42494E41UL, 0x4F544852UL); // BINA / OTHR
  const FInfo before = Metadata(spec);
  File file;
  LOKA_VERIFY(ToolboxCaptureChosenFile(spec, file));
  LOKA_VERIFY(!file.locator().empty());
  ToolboxPlatformContext context;
  loka::core::PushStateTracker tracker;
  loka::testing::TextBufferFixture lines;
  LOKA_VERIFY(lines.attach(&tracker, 1) == loka::core::ATTACH_OK);
  LOKA_VERIFY(lines.insert(0, String::Literal("replacement text")) == loka::core::EDIT_OK);
  FailPrepare(noErr, noErr);

  // Removing the prepare-refusal guard reaches production OpenWriteTruncate,
  // whose folder entry fails with paramErr from the unchanged HGetVol stub.
  // The mutant therefore returns OPEN_FAILED instead of NOT_TEXT, and cannot
  // reach host fopen. Never make HGetVol/HSetVol/FlushVol succeed for this pin.
  const loka::app::TextDocumentResult result = loka::app::WriteTextDocument(&context, file, lines);
  if (result != loka::app::TEXT_DOCUMENT_NOT_TEXT)
    std::fprintf(stderr, "prepare-refusal write result: %d\n", static_cast<int>(result));
  LOKA_VERIFY(result == loka::app::TEXT_DOCUMENT_NOT_TEXT);
  LOKA_VERIFY(CatalogCalls() == 1 && CreateCalls() == 0);
  LOKA_VERIFY(Metadata(spec).fdType == before.fdType);
  LOKA_VERIFY(Metadata(spec).fdCreator == before.fdCreator);
  LOKA_VERIFY(Read(context, file) == "original binary contents");
}
namespace
{
  class CountingBusyOwner : public ToolboxBusyOwner
  {
  public:
    CountingBusyOwner() : depth(0), entries(0), exits(0) {}
    int depth, entries, exits;

  private:
    virtual bool openBatch() { return false; }
    virtual void closeBatch() { LOKA_VERIFY(false); }
    virtual void enterBusy()
    {
      ++this->depth;
      ++this->entries;
    }
    virtual void exitBusy()
    {
      --this->depth;
      ++this->exits;
    }
  };
}
// A whole-file read borrows the registered busy owner once and returns it on
// every exit after the open; with no registration the borrow is inert (#1066).
static void Busy()
{
  BlobAllocationProbe allocation;
  const FSSpec spec = Spec(-7, 0x12345678, "Photo.PICT");
  Put(spec, "abc");
  ToolboxPlatformContext context;
  FileHandle live;
  LOKA_VERIFY(context.openFile(Choose(spec), live));
  FileHandle missing;
  LOKA_VERIFY(context.openFile(Choose(Spec(-7, 999, "Gone.PICT")), missing));
  const FileHandle none;
  loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
  CountingBusyOwner owner;
  LOKA_VERIFY(RegisteredToolboxBusyOwner() == 0);
  LOKA_VERIFY(ReadBytes(live, bytes) == READ_OK);
  LOKA_VERIFY(owner.entries == 0);
  allocation.refuse("Blob", "Bytes");
  LOKA_VERIFY(ReadBytes(live, bytes) == READ_ALLOCATION_REFUSED);
  LOKA_VERIFY(bytes.size() == 0 && OpenCount() == 0);

  {
    const ToolboxBusyOwnerRegistration registration(owner);
    LOKA_VERIFY(RegisteredToolboxBusyOwner() == &owner);
    LOKA_VERIFY(ReadBytes(live, bytes) == READ_OK);
    LOKA_VERIFY(std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()) == "abc");
    LOKA_VERIFY(owner.entries == 1 && owner.exits == 1 && owner.depth == 0);
    LOKA_VERIFY(ReadBytes(missing, bytes) == READ_NATIVE_OPEN_FAILED);
    LOKA_VERIFY(owner.entries == 2 && owner.exits == 2 && owner.depth == 0);
    LOKA_VERIFY(ReadBytes(none, bytes) == READ_NO_NATIVE_SPEC);
    LOKA_VERIFY(owner.entries == 2 && owner.exits == 2);
    // The stdio read behind logical paths borrows the same owner, including
    // a failed open.
    std::FILE *stream = std::fopen("busy-path.txt", "wb");
    LOKA_VERIFY(stream && std::fwrite("xyz", 1, 3, stream) == 3 && std::fclose(stream) == 0);
    LOKA_VERIFY(ReadBytes(loka::core::String("busy-path.txt"), bytes) == READ_OK);
    LOKA_VERIFY(std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size()) == "xyz");
    LOKA_VERIFY(owner.entries == 3 && owner.exits == 3 && owner.depth == 0);
    LOKA_VERIFY(std::remove("busy-path.txt") == 0);
    LOKA_VERIFY(ReadBytes(loka::core::String("busy-path.txt"), bytes) == READ_STDIO_OPEN_FAILED);
    LOKA_VERIFY(owner.entries == 4 && owner.exits == 4 && owner.depth == 0);
  }
  LOKA_VERIFY(RegisteredToolboxBusyOwner() == 0);
}
#include "ToolboxDialogPresentHostTests.hpp"

int main(int argc, char **argv)
{
  ToolboxPlatformContext capacityRail;
  loka::app::TextCapacity maximum;
  LOKA_VERIFY(capacityRail.queryTextEditorCapacity(maximum));
  LOKA_VERIFY(maximum.lines == 256 && maximum.bytes == 8192);
  LOKA_VERIFY(argc == 2);
  if (!std::strncmp(argv[1], "confirm-", 8) || !std::strcmp(argv[1], "mixed-fifo-order")
      || !std::strcmp(argv[1], "close-during-pending-revokes-both-payloads")) ConfirmPinTest(argv[1]);
  else if (!std::strncmp(argv[1], "present-", 8)) PresentPin(argv[1] + 8);
  else if (!std::strcmp(argv[1], "tuple")) Tuple();
  else if (!std::strcmp(argv[1], "single")) Single();
  else if (!std::strcmp(argv[1], "application")) Application(false, false);
  else if (!std::strcmp(argv[1], "viewer")) Application(true, false);
  else if (!std::strcmp(argv[1], "smirky")) Application(false, true);
  else if (!std::strcmp(argv[1], "collision-ab")) Collision(false);
  else if (!std::strcmp(argv[1], "collision-ba")) Collision(true);
  else if (!std::strcmp(argv[1], "decoy")) Decoy();
  else if (!std::strcmp(argv[1], "refused")) Refused();
  else if (!std::strcmp(argv[1], "canonical")) Canonical();
  else if (!std::strcmp(argv[1], "validation")) Validation();
  else if (!std::strcmp(argv[1], "allocation")) Allocation();
  else if (!std::strcmp(argv[1], "display")) Display();
  else if (!std::strcmp(argv[1], "copies")) Copies();
  else if (!std::strcmp(argv[1], "dialog")) Dialog();
  else if (!std::strcmp(argv[1], "save")) SaveDialog();
  else if (!std::strcmp(argv[1], "save-retarget")) PresentPin("props");
  else if (!std::strcmp(argv[1], "validity")) Validity();
  else if (!std::strcmp(argv[1], "prepare")) Prepare();
  else if (!std::strcmp(argv[1], "prepare-refuses-write")) PrepareRefusesWrite();
  else if (!std::strcmp(argv[1], "busy")) Busy();
  else return 2;
  LOKA_VERIFY(OpenCount() == 0);
  return 0;
}
