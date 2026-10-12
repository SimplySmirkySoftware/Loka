#include "Win32FilePathTests.hpp"
#include "support/TestVerify.hpp"

#include <windows.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "core/String.hpp"
#include "core/io/File.hpp"
#include "platform/Win32PathBridge.hpp"
#include "platform/StringUTF8.hpp"
#include "platform/file/FileHandle.hpp"
#include "platform/file/FileIO.hpp"

namespace
{
  // "テスト" and "画像.bin" — the payload shape from #15. Their UTF-8 bytes are
  // not a valid CP932 path, which is precisely why the pre-fix narrow open fails.
  const wchar_t kDirNameWide[] = {0x30C6, 0x30B9, 0x30C8, 0};                       // テスト
  const wchar_t kFileNameWide[] = {0x753B, 0x50CF, L'.', L'b', L'i', L'n', 0};      // 画像.bin

  const unsigned char kPayload[] = {0x4C, 0x4F, 0x4B, 0x41};

  bool WideToAcp(const std::wstring &wide, std::string &out)
  {
    out.clear();
    if (wide.empty())
      return true;
    const int needed =
        WideCharToMultiByte(CP_ACP, 0, wide.c_str(), static_cast<int>(wide.length()), NULL, 0, NULL, NULL);
    if (needed <= 0)
      return false;
    out.resize(static_cast<std::size_t>(needed));
    const int written =
        WideCharToMultiByte(CP_ACP, 0, wide.c_str(), static_cast<int>(wide.length()), &out[0], needed, NULL, NULL);
    return written == needed;
  }

  bool WideToUtf8(const std::wstring &wide, std::string &out)
  {
    out.clear();
    if (wide.empty())
      return true;
    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.length()), NULL, 0, NULL, NULL);
    if (needed <= 0)
      return false;
    out.resize(static_cast<std::size_t>(needed));
    const int written = WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.length()), &out[0], needed, NULL, NULL);
    return written == needed;
  }
} // namespace

void testWin32OpenReadAcceptsFullWidthPath()
{
  printf("\n==== [testWin32OpenReadAcceptsFullWidthPath] start ====\n");

  wchar_t tempDir[MAX_PATH];
  const DWORD tempLen = GetTempPathW(MAX_PATH, tempDir);
  assert(tempLen > 0 && tempLen < MAX_PATH);

  std::wstring dirWide(tempDir, tempDir + tempLen);
  dirWide += L"loka-fw15-";
  dirWide += kDirNameWide;
  RemoveDirectoryW(dirWide.c_str());
  const BOOL made = CreateDirectoryW(dirWide.c_str(), NULL);
  LOKA_VERIFY((made || GetLastError() == ERROR_ALREADY_EXISTS) &&
              "could not create the full-width fixture directory");

  std::wstring fileWide = dirWide;
  fileWide += L"\\";
  fileWide += kFileNameWide;

  FILE *seed = _wfopen(fileWide.c_str(), L"wb");
  assert(seed && "fixture write must use the wide CRT: the narrow one is the thing under test");
  LOKA_VERIFY(fwrite(kPayload, 1, sizeof(kPayload), seed) == sizeof(kPayload));
  fclose(seed);

  std::string utf8Path;
  LOKA_VERIFY(WideToUtf8(fileWide, utf8Path));
  const loka::core::String logicalPath((std::string(utf8Path)));

  // The fix: the seam names the file the way Windows names files.
  FILE *opened = loka::platform::file::OpenRead(logicalPath);
  assert(opened && "OpenRead must reach a path containing full-width characters (#15)");
  unsigned char read[sizeof(kPayload)] = {0};
  LOKA_VERIFY(fread(read, 1, sizeof(read), opened) == sizeof(read));
  fclose(opened);
  for (std::size_t i = 0; i < sizeof(kPayload); ++i)
  {
    assert(read[i] == kPayload[i] && "OpenRead must return the bytes of the file that was written");
  }

  // Documented pre-fix failure shape, kept as a contrast pin: flattening the
  // logical path to UTF-8 and handing the bytes to the narrow CRT open is what
  // BlobLoader and SimpleViewer used to do, and it cannot reach this file
  // because fopen decodes its argument in the process ANSI code page. If this
  // ever succeeds, either the machine is running the UTF-8 code page (excluded
  // below) or someone has re-introduced the bug's premise and this pin is stale.
  if (GetACP() != CP_UTF8)
  {
    std::string flattened;
    LOKA_VERIFY(loka::platform::CollectUtf8(logicalPath, flattened));
    assert(flattened == utf8Path && "the logical path must survive as UTF-8; the defect is in the open, not the String");
    FILE *narrow = std::fopen(flattened.c_str(), "rb");
    assert(!narrow && "the narrow open must still fail on a full-width path; that is why the seam exists");
    if (narrow)
      fclose(narrow);
  }
  else
  {
    printf("  [skip] ACP is UTF-8, so the narrow-open contrast pin cannot discriminate on this machine\n");
  }

  DeleteFileW(fileWide.c_str());
  RemoveDirectoryW(dirWide.c_str());

  printf("==== [testWin32OpenReadAcceptsFullWidthPath] end ====\n");
}

void testWin32FileFromWidePathSurvivesToOpen()
{
  printf("\n==== [testWin32FileFromWidePathSurvivesToOpen] start ====\n");

  wchar_t tempDir[MAX_PATH];
  const DWORD tempLen = GetTempPathW(MAX_PATH, tempDir);
  assert(tempLen > 0 && tempLen < MAX_PATH);

  std::wstring dirWide(tempDir, tempDir + tempLen);
  dirWide += L"loka-bridge-";
  dirWide += kDirNameWide;
  RemoveDirectoryW(dirWide.c_str());
  const BOOL made = CreateDirectoryW(dirWide.c_str(), NULL);
  LOKA_VERIFY(made || GetLastError() == ERROR_ALREADY_EXISTS);

  std::wstring fileWide = dirWide;
  fileWide += L"\\";
  fileWide += kFileNameWide;

  FILE *seed = _wfopen(fileWide.c_str(), L"wb");
  assert(seed);
  LOKA_VERIFY(fwrite(kPayload, 1, sizeof(kPayload), seed) == sizeof(kPayload));
  fclose(seed);

  // The producer under test: what a `W` Win32 entry point hands us must reach
  // an open unchanged. This covers everything between "the OS gave us UTF-16"
  // and "the file is open" -- every line of it is ours, and it is where #15
  // was lost.
  const loka::file::File item = loka::win32::FileFromWidePath(fileWide.c_str(), fileWide.size());
  LOKA_VERIFY(item.kind() == loka::file::File::KIND_FILE);
  FILE *opened = loka::platform::file::OpenRead(item.toString());
  assert(opened && "a File built from a UTF-16 path must still name that path at the open");
  unsigned char read[sizeof(kPayload)] = {0};
  LOKA_VERIFY(fread(read, 1, sizeof(read), opened) == sizeof(read));
  fclose(opened);
  for (std::size_t i = 0; i < sizeof(kPayload); ++i)
  {
    assert(read[i] == kPayload[i]);
  }

  // Contrast pin: the ANSI producer. GetOpenFileNameA would have handed us
  // these bytes, and loka::core::String reads them as UTF-8, so the path
  // becomes different text and the open cannot find the file. If this ever
  // succeeds, either the machine runs the UTF-8 code page (excluded) or the
  // dialog has been switched back to the `A` variant and this pin is stale.
  if (GetACP() != CP_UTF8)
  {
    std::string acpBytes;
    assert(WideToAcp(fileWide, acpBytes));
    const loka::core::String fromAcp((std::string(acpBytes)));
    FILE *viaAcp = loka::platform::file::OpenRead(fromAcp);
    assert(!viaAcp && "ANSI bytes read as UTF-8 must not name the same file; that is the #15 mechanism");
    if (viaAcp)
      fclose(viaAcp);
  }
  else
  {
    printf("  [skip] ACP is UTF-8, so the ANSI-producer contrast pin cannot discriminate on this machine\n");
  }

  DeleteFileW(fileWide.c_str());
  RemoveDirectoryW(dirWide.c_str());

  printf("==== [testWin32FileFromWidePathSurvivesToOpen] end ====\n");
}

void testWin32OpenWriteTruncateAcceptsFullWidthPath()
{
  printf("\n==== [testWin32OpenWriteTruncateAcceptsFullWidthPath] start ====\n");

  wchar_t tempDir[MAX_PATH];
  const DWORD tempLen = GetTempPathW(MAX_PATH, tempDir);
  LOKA_VERIFY(tempLen > 0 && tempLen < MAX_PATH);

  std::wstring dirWide(tempDir, tempDir + tempLen);
  dirWide += L"loka-write-";
  dirWide += kDirNameWide;
  RemoveDirectoryW(dirWide.c_str());
  const BOOL made = CreateDirectoryW(dirWide.c_str(), NULL);
  const DWORD createError = made ? ERROR_SUCCESS : GetLastError();
  LOKA_VERIFY(made || createError == ERROR_ALREADY_EXISTS);

  std::wstring fileWide = dirWide;
  fileWide += L"\\";
  fileWide += kFileNameWide;
  std::string utf8Path;
  LOKA_VERIFY(WideToUtf8(fileWide, utf8Path));
  loka::platform::file::FileHandle destination;
  destination.displayPath = loka::core::String((std::string(utf8Path)));

  FILE *output = loka::platform::file::OpenWriteTruncate(destination);
  LOKA_VERIFY(output != 0);
  LOKA_VERIFY(fwrite(kPayload, 1, sizeof(kPayload), output) == sizeof(kPayload));
  LOKA_VERIFY(loka::platform::file::FlushWrite(output, destination));
  LOKA_VERIFY(fclose(output) == 0);

  FILE *input = _wfopen(fileWide.c_str(), L"rb");
  LOKA_VERIFY(input != 0);
  unsigned char read[sizeof(kPayload)] = {0};
  LOKA_VERIFY(fread(read, 1, sizeof(read), input) == sizeof(read));
  fclose(input);
  for (std::size_t i = 0; i < sizeof(kPayload); ++i)
  {
    LOKA_VERIFY(read[i] == kPayload[i]);
  }

  DeleteFileW(fileWide.c_str());
  RemoveDirectoryW(dirWide.c_str());
  printf("==== [testWin32OpenWriteTruncateAcceptsFullWidthPath] end ====\n");
}

void testWin32FileNameIsTheLastBackslashComponent()
{
  printf("\n==== [testWin32FileNameIsTheLastBackslashComponent] start ====\n");
  // Win32 dialogs hand back backslash paths; FileFromWidePath seals them unchanged.
  const wchar_t kPath[] = L"C:\\loka\\folder\\leaf.txt";
  const loka::file::File item = loka::win32::FileFromWidePath(kPath, sizeof(kPath) / sizeof(kPath[0]) - 1);
  LOKA_VERIFY(item.name().equals(loka::core::String::Literal("leaf.txt")));
  const wchar_t kTrailing[] = L"C:\\loka\\folder\\";
  LOKA_VERIFY(loka::win32::FileFromWidePath(kTrailing, sizeof(kTrailing) / sizeof(kTrailing[0]) - 1)
                  .name()
                  .equals(loka::core::String::Literal("folder")));
  const wchar_t kRoot[] = L"C:\\";
  LOKA_VERIFY(loka::win32::FileFromWidePath(kRoot, sizeof(kRoot) / sizeof(kRoot[0]) - 1).name().empty());
  printf("==== [testWin32FileNameIsTheLastBackslashComponent] PASSED ====\n");
}
