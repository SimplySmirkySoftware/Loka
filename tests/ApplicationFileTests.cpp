#include "ApplicationFileTests.hpp"
#include "support/TestVerify.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileHandle.hpp"
#include "platform/file/FileIO.hpp"
#include "platform/null/NullPlatformContext.hpp"

namespace
{
  bool CreateDirectoryForTest(const char *path)
  {
#if defined(_WIN32)
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0755) == 0;
#endif
  }

  bool RemoveDirectoryForTest(const char *path)
  {
#if defined(_WIN32)
    return _rmdir(path) == 0;
#else
    return rmdir(path) == 0;
#endif
  }

  void WriteBytes(const char *path, const unsigned char *bytes, std::size_t size)
  {
    std::FILE *file = std::fopen(path, "wb");
    assert(file);
    LOKA_VERIFY(std::fwrite(bytes, 1, size, file) == size);
    LOKA_VERIFY(std::fclose(file) == 0);
  }
} // namespace

void testApplicationFileCompositionKeepsExplicitBase()
{
  loka::file::File asset = loka::file::File::Application() << loka::file::File("ASSETS.LRP");
  assert(asset.base() == loka::file::File::BASE_APPLICATION);
  assert(asset.relativePath().equals(loka::core::String::Literal("ASSETS.LRP")));

  loka::file::File desktopChild = loka::file::File::Desktop() << loka::file::File("manual.pdf");
  loka::file::File childWins = loka::file::File::Application() << desktopChild;
  assert(childWins.base() == loka::file::File::BASE_DESKTOP);
  assert(childWins.relativePath().equals(loka::core::String::Literal("manual.pdf")));
}

void testApplicationRelativePredicateEnforcesTwoComponents()
{
  using loka::platform::file::ApplicationRelativeIsOpenable;

  LOKA_VERIFY(ApplicationRelativeIsOpenable(loka::file::File::Application() << loka::file::File("ASSETS.LRP")));
  LOKA_VERIFY(!ApplicationRelativeIsOpenable(loka::file::File::Application()));
  LOKA_VERIFY(ApplicationRelativeIsOpenable(loka::file::File::Application() << loka::file::File("A")));
  LOKA_VERIFY(
      ApplicationRelativeIsOpenable(loka::file::File::Application() << loka::file::File("A") << loka::file::File("B")));
  const char *refused[] = {"A/B/C", "../B", "A/..", "A/", "/A", "A//B", "./B", "A/."};
  for (std::size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    LOKA_VERIFY(!ApplicationRelativeIsOpenable(loka::file::File::Application() << loka::file::File(refused[i])));
  LOKA_VERIFY(!ApplicationRelativeIsOpenable(loka::file::File::Application() << loka::file::File("a:b")));
  LOKA_VERIFY(!ApplicationRelativeIsOpenable(loka::file::File::Application() << loka::file::File("sub\\a.bin")));
  LOKA_VERIFY(!ApplicationRelativeIsOpenable(loka::file::File::Application() << loka::file::File("..\\..\\secret")));
  LOKA_VERIFY(!ApplicationRelativeIsOpenable(loka::file::File::Application()
                                             << loka::file::File(loka::core::String::Utf8("a\0b", 3))));
  LOKA_VERIFY(!ApplicationRelativeIsOpenable(loka::file::File::Application() << loka::file::File(".")));
  LOKA_VERIFY(!ApplicationRelativeIsOpenable(loka::file::File::Application() << loka::file::File("..")));
  LOKA_VERIFY(!ApplicationRelativeIsOpenable(loka::file::File("ASSETS.LRP")));

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(LOKA_RETRO68)
  loka::platform::file::FileHandle handle;
  LOKA_VERIFY(!loka::platform::file::ResolveApplicationItem(
      loka::file::File::Application() << loka::file::File("ASSETS.LRP"), handle));
#endif
}

void testNullApplicationFileUsesOnlyInjectedDirectory()
{
  const char *directory = "_loka_application_fixture";
  const char *name = "_loka_application_asset.bin";
  const char *injectedPath = "_loka_application_fixture/_loka_application_asset.bin";
  const unsigned char cwdBytes[] = {0x43, 0x57, 0x44};
  const unsigned char injectedBytes[] = {0x41, 0x50, 0x50};

  std::remove(injectedPath);
  RemoveDirectoryForTest(directory);
  std::remove(name);
  LOKA_VERIFY(CreateDirectoryForTest(directory));
  WriteBytes(name, cwdBytes, sizeof(cwdBytes));
  WriteBytes(injectedPath, injectedBytes, sizeof(injectedBytes));

  const loka::file::File item = loka::file::File::Application() << loka::file::File(name);
  NullPlatformContext noInjection;
  loka::platform::file::FileHandle handle;
  LOKA_VERIFY(!noInjection.openFile(item, handle));

  NullPlatformContext context;
  context.setApplicationDirectory(loka::core::String::Literal(directory));
  LOKA_VERIFY(!context.openFile(loka::file::File::Application(), handle));
  LOKA_VERIFY(!context.openFile(loka::file::File::Application() << loka::file::File("a/b/c"), handle));
  LOKA_VERIFY(!context.openFile(loka::file::File::Application() << loka::file::File(".."), handle));
  LOKA_VERIFY(context.openFile(loka::file::File::Application() << loka::file::File("Pictures") << loka::file::File("X"),
                               handle));
  LOKA_VERIFY(handle.displayPath.equals(loka::core::String::Literal("_loka_application_fixture/Pictures/X")));
  LOKA_VERIFY(context.openFile(item, handle));
  LOKA_VERIFY(handle.displayPath.equals(loka::core::String::Literal(injectedPath)));

  std::FILE *opened = loka::platform::file::OpenRead(handle.displayPath);
  assert(opened);
  unsigned char actual[sizeof(injectedBytes)] = {0};
  LOKA_VERIFY(std::fread(actual, 1, sizeof(actual), opened) == sizeof(actual));
  LOKA_VERIFY(std::fclose(opened) == 0);
  assert(std::memcmp(actual, injectedBytes, sizeof(actual)) == 0);

  LOKA_VERIFY(std::remove(injectedPath) == 0);
  LOKA_VERIFY(RemoveDirectoryForTest(directory));
  LOKA_VERIFY(std::remove(name) == 0);
}

void testGenericFlushWriteRefusesClosedDurabilityDoor()
{
#if !defined(_WIN32) && !defined(LOKA_RETRO68)
  std::FILE *stream = std::tmpfile();
  LOKA_VERIFY(stream != 0);
  LOKA_VERIFY(std::fputs("durable", stream) >= 0);
  LOKA_VERIFY(std::fflush(stream) == 0);
  const int descriptor = fileno(stream);
  LOKA_VERIFY(descriptor >= 0);
  LOKA_VERIFY(close(descriptor) == 0);

  // A second stdio flush has no buffered bytes and succeeds, but the closed
  // descriptor cannot cross the platform durability boundary. This is the
  // pre-fix loss mechanism: fflush alone incorrectly reported success.
  loka::platform::file::FileHandle destination;
  LOKA_VERIFY(!loka::platform::file::FlushWrite(stream, destination));
  std::fclose(stream);
#endif
}
