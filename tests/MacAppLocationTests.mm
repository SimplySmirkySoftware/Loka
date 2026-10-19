#include "MacAppLocationTests.hpp"
#include "support/TestVerify.hpp"

#include <Foundation/Foundation.h>
#include <objc/runtime.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"
#include "platform/StringUTF8.hpp"

@interface NSFileManager (LokaSidecarTests)
- (BOOL)loka_testRefuseWritableFileAtPath:(NSString *)path;
@end

@implementation NSFileManager (LokaSidecarTests)
- (BOOL)loka_testRefuseWritableFileAtPath:(NSString *)path
{
  (void)path;
  return NO;
}
@end

namespace
{
  class ScopedMethodExchange
  {
  public:
    ScopedMethodExchange(Class owner, SEL first, SEL second)
        : first_(class_getInstanceMethod(owner, first)), second_(class_getInstanceMethod(owner, second)), active_(false)
    {
      if (this->first_ && this->second_)
      {
        method_exchangeImplementations(this->first_, this->second_);
        this->active_ = true;
      }
    }

    ~ScopedMethodExchange()
    {
      if (this->active_)
      {
        method_exchangeImplementations(this->first_, this->second_);
      }
    }

    bool active() const { return this->active_; }

  private:
    ScopedMethodExchange(const ScopedMethodExchange &);
    ScopedMethodExchange &operator=(const ScopedMethodExchange &);

    Method first_;
    Method second_;
    bool active_;
  };
} // namespace

void testMacApplicationItemNamesResourceDirectory()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSString *executablePath = [[NSBundle mainBundle] executablePath];
  // Mac OS X 10.9 joins the launch directory and argv[0] without normalizing
  // ("/private/tmp/./sub/tool" for "./sub/tool") while resourcePath comes back
  // normalized; later releases normalize both. Paths compare normalized.
  NSString *executableDirectory = [[executablePath stringByDeletingLastPathComponent] stringByStandardizingPath];
  NSString *resourcePath = [[NSBundle mainBundle] resourcePath];
  assert(executablePath);
  assert(executableDirectory);
  assert([[resourcePath stringByStandardizingPath] isEqualToString:executableDirectory]);

  NSString *name = @"loka-application-location.bin";
  NSString *fixturePath = [executableDirectory stringByAppendingPathComponent:name];
  const unsigned char expected[] = {0x19, 0x9A, 0x02};
  NSData *payload = [NSData dataWithBytes:expected length:sizeof(expected)];
  LOKA_VERIFY([payload writeToFile:fixturePath atomically:NO]);

  const char *nameUtf8 = [name UTF8String];
  assert(nameUtf8);
  const loka::file::File item = loka::file::File::Application()
                                << loka::file::File(loka::core::String::Utf8(nameUtf8, std::strlen(nameUtf8)));
  loka::platform::file::FileHandle handle;
  LOKA_VERIFY(loka::platform::file::ResolveApplicationItem(item, handle));

  std::string displayUtf8;
  LOKA_VERIFY(loka::platform::CollectUtf8(handle.displayPath, displayUtf8));
  NSString *displayPath = [NSString stringWithUTF8String:displayUtf8.c_str()];
  assert(displayPath);
  assert([[displayPath stringByStandardizingPath] isEqualToString:[fixturePath stringByStandardizingPath]]);

  std::FILE *opened = loka::platform::file::OpenRead(handle.displayPath);
  assert(opened);
  unsigned char actual[sizeof(expected)] = {0};
  LOKA_VERIFY(std::fread(actual, 1, sizeof(actual), opened) == sizeof(actual));
  LOKA_VERIFY(std::fclose(opened) == 0);
  assert(std::memcmp(actual, expected, sizeof(actual)) == 0);

  const loka::file::File nested = loka::file::File::Application()
                                  << loka::file::File("Pictures") << loka::file::File("X");
  LOKA_VERIFY(loka::platform::file::ResolveApplicationItem(nested, handle));
  displayUtf8.clear(); // CollectUtf8 appends
  LOKA_VERIFY(loka::platform::CollectUtf8(handle.displayPath, displayUtf8));
  NSString *nestedPath = [NSString stringWithUTF8String:displayUtf8.c_str()];
  LOKA_VERIFY([[nestedPath stringByStandardizingPath]
      isEqualToString:[[resourcePath stringByAppendingPathComponent:@"Pictures/X"] stringByStandardizingPath]]);
  const char *refused[] = {"A/B/C", "../B", "A/..", "A/", "/A", "A//B", ".", "A/."};
  for (std::size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    LOKA_VERIFY(!loka::platform::file::ApplicationRelativeIsOpenable(loka::file::File::Application()
                                                                     << loka::file::File(refused[i])));

  LOKA_VERIFY([[NSFileManager defaultManager] removeItemAtPath:fixturePath error:nil]);
  [pool drain];
}

void testMacApplicationSidecarNamesBundleParent()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
  assert(bundlePath);
  NSString *name = @"_loka-sidecar-write-test.tmp";
  NSString *expected = [[bundlePath stringByDeletingLastPathComponent] stringByAppendingPathComponent:name];
  [[NSFileManager defaultManager] removeItemAtPath:expected error:nil];

  loka::platform::file::FileHandle handle;
  LOKA_VERIFY(loka::platform::file::ResolveApplicationSidecar(
      loka::file::File::Application() << loka::file::File("_loka-sidecar-write-test.tmp"), handle));
  const char *expectedUtf8 = [expected UTF8String];
  assert(expectedUtf8);
  assert(handle.displayPath.equals(loka::core::String::Utf8(expectedUtf8, std::strlen(expectedUtf8))));

  const unsigned char payload[] = {0x4C, 0x4F, 0x4B, 0x41};
  std::FILE *opened = loka::platform::file::OpenWriteTruncate(handle);
  assert(opened);
  LOKA_VERIFY(std::fwrite(payload, 1, sizeof(payload), opened) == sizeof(payload));
  LOKA_VERIFY(loka::platform::file::FlushWrite(opened, handle));
  LOKA_VERIFY(std::fclose(opened) == 0);
  NSData *actual = [NSData dataWithContentsOfFile:expected];
  assert(actual);
  assert([actual length] == sizeof(payload));
  assert(std::memcmp([actual bytes], payload, sizeof(payload)) == 0);
  LOKA_VERIFY([[NSFileManager defaultManager] removeItemAtPath:expected error:nil]);
  [pool drain];
}

void testMacApplicationSidecarDeclinesWhenParentReportsReadOnly()
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  NSFileManager *manager = [NSFileManager defaultManager];
  ScopedMethodExchange exchange([manager class],
                                @selector(isWritableFileAtPath:),
                                @selector(loka_testRefuseWritableFileAtPath:));
  const bool exchangeInstalled = exchange.active();
  LOKA_VERIFY(exchangeInstalled);

  loka::platform::file::FileHandle handle;
  LOKA_VERIFY(!loka::platform::file::ResolveApplicationSidecar(
      loka::file::File::Application() << loka::file::File("LOG.TXT"), handle));
  assert(handle.displayPath.empty());
  [pool drain];
}
