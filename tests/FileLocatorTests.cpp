#include "FileLocatorTests.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include "platform/file/FileLocatorAccess.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "app/OpenFileDialog.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace
{
  using loka::file::File;
  using loka::platform::file::FileLocatorAccess;

  File Located(const void *bytes, std::size_t size)
  {
    File result;
    LOKA_VERIFY(FileLocatorAccess::capture(loka::core::String::Literal("same"), File::KIND_FILE,
                                          bytes, size, result));
    return result;
  }

  void VerifyRefused(const File &value)
  {
    // Deliberately always-on: release builds must discriminate refusal too.
    LOKA_VERIFY(value.base() == File::BASE_REFUSED);
    LOKA_VERIFY(value.kind() == File::KIND_UNKNOWN);
    LOKA_VERIFY(value.locator().empty());
    LOKA_VERIFY(value.relativePath().empty());
    LOKA_VERIFY(value.toString().empty());
    LOKA_VERIFY(value.name().empty());
    LOKA_VERIFY(value != File());
    LOKA_VERIFY(!loka::platform::file::ApplicationRelativeIsOpenable(value));
  }
}

void testFileLocatorCountedEqualityAndCopies()
{
  unsigned char source[] = {0, 0x80, 0xff, 7};
  unsigned char independent[] = {0, 0x80, 0xff, 7};
  File saved;
  const unsigned char *shared = 0;
  std::size_t count = 0;
  {
    const File first = Located(source, sizeof(source));
    const File equal = Located(independent, sizeof(independent));
    LOKA_VERIFY(!(first != equal));
    LOKA_VERIFY(first.locator() == equal.locator());
    saved = first;
    LOKA_VERIFY(FileLocatorAccess::query(first, shared, count));
    const unsigned char *second = 0;
    LOKA_VERIFY(FileLocatorAccess::query(equal, second, count));
    LOKA_VERIFY(second != shared);
    source[1] = 3;
    LOKA_VERIFY(!(first != equal));
    LOKA_VERIFY(first != Located(source, sizeof(source)));
    LOKA_VERIFY(first != Located(independent, sizeof(independent) - 1));
    LOKA_VERIFY(Located(independent, sizeof(independent) - 1) != first);
  }
  const unsigned char *copy = 0;
  LOKA_VERIFY(FileLocatorAccess::query(saved, copy, count));
  LOKA_VERIFY(copy == shared && count == sizeof(independent));
  LOKA_VERIFY(std::memcmp(copy, independent, count) == 0);
  File unlocated("same");
  unlocated.setKind(File::KIND_FILE);
  LOKA_VERIFY(unlocated != saved);
  const File zero = Located(0, 0);
  LOKA_VERIFY(!zero.locator().empty());
  LOKA_VERIFY(zero != unlocated);
  LOKA_VERIFY(!(zero != Located(0, 0)));
  LOKA_VERIFY(!FileLocatorAccess::query(unlocated, copy, count));
  LOKA_VERIFY(copy == shared && count == sizeof(independent));
}

void testFileLocatorCaptureRefusalIsAtomic()
{
  using namespace loka::core::testing;
  const unsigned char bytes[] = {0, 0xff};
  const loka::core::String display = loka::core::String::Literal("new");
  const File original = Located(bytes, sizeof(bytes));
  File out = original;
  const int live = lokaAllocRawLive();
  failLokaAllocRaw("FileLocator", "Payload", 1);
  const bool payloadAccepted = FileLocatorAccess::capture(display, File::KIND_FOLDER, bytes, sizeof(bytes), out);
  allowLokaAllocRaw();
  LOKA_VERIFY(!payloadAccepted && !(out != original));
  LOKA_VERIFY(lokaAllocRawLive() == live);
  failLokaAllocRaw("Managed", "ControlBlock", 1);
  const bool blockAccepted = FileLocatorAccess::capture(display, File::KIND_FOLDER, bytes, sizeof(bytes), out);
  allowLokaAllocRaw();
  LOKA_VERIFY(!blockAccepted && !(out != original));
  LOKA_VERIFY(lokaAllocRawLive() == live);
  LOKA_VERIFY(!FileLocatorAccess::capture(display, File::KIND_FOLDER, 0, 1, out));
  LOKA_VERIFY(!FileLocatorAccess::capture(display, File::KIND_FOLDER, bytes, static_cast<std::size_t>(-1), out));
  LOKA_VERIFY(!(out != original));
  out = File::Application() << File("old");
  LOKA_VERIFY(FileLocatorAccess::capture(display, File::KIND_FOLDER, bytes, sizeof(bytes), out));
  LOKA_VERIFY(out.base() == File::BASE_NONE && out.kind() == File::KIND_FOLDER);
  LOKA_VERIFY(out.toString().equals(display) && !out.locator().empty());

  // Successful ownership uses exactly payload + control block, and every
  // copy is a retain. Keep the whole allocation lifetime inside this backend.
  failLokaAllocRaw("FileLocator", "Payload", 0);
  {
    File owned;
    LOKA_VERIFY(FileLocatorAccess::capture(display, File::KIND_FILE, bytes, sizeof(bytes), owned));
    LOKA_VERIFY(lokaAllocRawLive() == live + 2 && lokaAllocRawAttempts() == 2);
    {
      const File copy(owned);
      owned = File();
      LOKA_VERIFY(lokaAllocRawLive() == live + 2 && lokaAllocRawAttempts() == 2);
      const unsigned char *view = 0;
      std::size_t size = 0;
      LOKA_VERIFY(FileLocatorAccess::query(copy, view, size));
      LOKA_VERIFY(size == sizeof(bytes) && std::memcmp(view, bytes, size) == 0);
    }
    LOKA_VERIFY(lokaAllocRawLive() == live);
  }
  allowLokaAllocRaw();
}

void testFileLocatorCompositionPrecedence()
{
  using loka::core::String;
  LOKA_VERIFY(File().name().empty());
  LOKA_VERIFY(File("unlocated").name().empty());
  LOKA_VERIFY(File::Root().name().empty());
  LOKA_VERIFY(File::Application().name().empty());
  LOKA_VERIFY(File::Desktop().name().empty());
  LOKA_VERIFY(File::Documents().name().empty());
  LOKA_VERIFY((File::Application() << File("assets") << File("leaf")).name().equals(String("leaf")));
  LOKA_VERIFY((File::Desktop() << File("leaf")).name().equals(String("leaf")));
  LOKA_VERIFY((File::Documents() << File("leaf")).name().equals(String("leaf")));
#if defined(LOKA_RETRO68)
  LOKA_VERIFY((File::Root() << File("folder:leaf::")).name().equals(String("leaf")));
  LOKA_VERIFY((File::Root() << File("notes/2026.txt")).name().equals(String("notes/2026.txt")));
#else
  LOKA_VERIFY(File::FromPath("/tmp/folder/leaf").name().equals(String("leaf")));
  LOKA_VERIFY(File::FromPath("/tmp/notes:2026.txt").name().equals(String("notes:2026.txt")));
#if defined(_WIN32)
  // Win32 seals dialog paths with backslashes, so a backslash ends a component there.
  LOKA_VERIFY(File::FromPath("/tmp/notes\\2026.txt").name().equals(String("2026.txt")));
#else
  LOKA_VERIFY(File::FromPath("/tmp/notes\\2026.txt").name().equals(String("notes\\2026.txt")));
#endif
  LOKA_VERIFY(File::FromPath("/tmp/leaf///").name().equals(String("leaf")));
  LOKA_VERIFY(File::FromPath("/").name().empty());
  LOKA_VERIFY(File::FromPath("").name().empty());
  LOKA_VERIFY((File::FromPath("/tmp/base") << File("folder/child/")).name().equals(String("child")));
#endif
  File chosen;
#if defined(LOKA_RETRO68)
  const String chosenName("notes/2026.txt");
#else
  const String chosenName("notes:2026.txt");
#endif
  LOKA_VERIFY(FileLocatorAccess::capture(chosenName, File::KIND_FILE, 0, 0, chosen));
  LOKA_VERIFY(chosen.name().equals(chosenName));
  LOKA_VERIFY(Located(0, 0).name().equals(String("same")));
  const unsigned char bytes[] = {0xff};
  const File located = Located(bytes, sizeof(bytes));
  File refused = located << File("child");
  VerifyRefused(refused);
  refused.setKind(File::KIND_FILE);
  VerifyRefused(refused);
  VerifyRefused(refused << located);
  VerifyRefused(refused << File::Application());
  VerifyRefused(located << located);
  VerifyRefused((refused << File("next")) << File::Root());
  LOKA_VERIFY(!(File("relative") << located != located));
  LOKA_VERIFY(!(File::Application() << located != located));
  const File bases[] = {File(), File::Application(), File::Root(), File::Desktop(), File::Documents()
#if !defined(LOKA_RETRO68)
                        , File::FromPath("/tmp")
#endif
  };
  for (std::size_t i = 0; i < sizeof(bases) / sizeof(bases[0]); ++i)
  {
    VerifyRefused(bases[i] << refused);
    VerifyRefused(refused << bases[i]);
    VerifyRefused(located << bases[i]);
    LOKA_VERIFY(!(bases[i] << located != located));
    const File child = bases[i] << File("leaf");
    LOKA_VERIFY(child.base() == bases[i].base());
    LOKA_VERIFY(child.relativePath().equals(loka::core::String::Literal("leaf")));
    LOKA_VERIFY(loka::platform::file::ApplicationRelativeIsOpenable(child) ==
                (bases[i].base() == File::BASE_APPLICATION));
  }
#if defined(LOKA_RETRO68)
  LOKA_VERIFY((File("a") << File("b")).toString().equals(loka::core::String::Literal("a:b")));
  LOKA_VERIFY((File::Root() << File("b")).toString().equals(loka::core::String::Literal(":b")));
  LOKA_VERIFY(File::Root().toString().equals(loka::core::String::Literal(":")));
  LOKA_VERIFY(File::Desktop().toString().equals(loka::core::String::Literal("Desktop")));
  LOKA_VERIFY(File::Documents().toString().equals(loka::core::String::Literal("Documents")));
#else
  LOKA_VERIFY((File("a") << File("b")).toString().equals(loka::core::String::Literal("a/b")));
  LOKA_VERIFY((File::Root() << File("b")).toString().equals(loka::core::String::Literal("/b")));
  LOKA_VERIFY(File::Root().toString().equals(loka::core::String::Literal("/")));
  LOKA_VERIFY(File::Desktop().toString().equals(loka::core::String::Literal("~/Desktop")));
  LOKA_VERIFY(File::Documents().toString().equals(loka::core::String::Literal("~/Documents")));
  LOKA_VERIFY((File::FromPath("/tmp") << File("b")).toString().equals(loka::core::String::Literal("/tmp/b")));
#endif
}

void testFileLocatorChooserStateDelivery()
{
  using loka::app::FileChooserResult;
  unsigned char a[] = {0, 0xff}, b[] = {0, 0xfe}, equal[] = {0, 0xff};
  const FileChooserResult first = FileChooserResult::File(Located(a, sizeof(a)));
  const FileChooserResult same = FileChooserResult::File(Located(equal, sizeof(equal)));
  const FileChooserResult other = FileChooserResult::File(Located(b, sizeof(b)));
  LOKA_VERIFY(!(first != same));
  LOKA_VERIFY(first != other);
  loka::core::PushStateTracker tracker;
  loka::core::MutableState<FileChooserResult> state(first);
  {
    loka::core::StateTrackerGuard transaction(&tracker);
    state.set(same);
  }
  LOKA_VERIFY(!(state.get() != first));
  {
    loka::core::StateTrackerGuard transaction(&tracker);
    state.set(other, true);
  }
  LOKA_VERIFY(!(state.get() != other));
  const unsigned char *actual = 0;
  std::size_t size = 0;
  LOKA_VERIFY(FileLocatorAccess::query(state.get().item, actual, size));
  LOKA_VERIFY(size == sizeof(b) && std::memcmp(actual, b, size) == 0);
}

void testFileLocatorNullRefusal()
{
  const unsigned char bytes[] = {1};
  const File refused = Located(bytes, sizeof(bytes)) << File("child");
  NullPlatformContext context;
  context.setApplicationDirectory(loka::core::String::Literal("/tmp"));
  loka::platform::file::FileHandle out;
  out.displayPath = loka::core::String::Literal("stale");
  out.kind = File::KIND_FILE;
  LOKA_VERIFY(!context.openFile(refused, out));
  LOKA_VERIFY(out.displayPath.empty() && out.kind == File::KIND_UNKNOWN);
}
