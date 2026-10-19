#include "Win32AppLocationTests.hpp"
#include "support/TestVerify.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "core/io/File.hpp"
#include "platform/Win32String.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

namespace
{
  bool ReadModulePath(std::wstring &path)
  {
    DWORD capacity = MAX_PATH;
    DWORD length = 0;
    std::vector<wchar_t> buffer;
    for (;;)
    {
      buffer.resize(static_cast<std::size_t>(capacity));
      length = GetModuleFileNameW(NULL, &buffer[0], capacity);
      if (length == 0)
      {
        return false;
      }
      if (length < capacity)
      {
        path.assign(&buffer[0], &buffer[0] + length);
        return true;
      }
      if (capacity > 32768)
      {
        return false;
      }
      capacity *= 2;
    }
  }
} // namespace

void testWin32ApplicationItemNamesExecutableDirectory()
{
  const wchar_t fileNameWide[] = {L'l', L'o', L'k', L'a', L'-', 0xFF21, L'.', L'b', L'i', L'n', 0};
  const char fileNameUtf8[] = "loka-\xEF\xBC\xA1.bin";
  const unsigned char expected[] = {0x19, 0x9A, 0x01};

  std::wstring modulePath;
  LOKA_VERIFY(ReadModulePath(modulePath));
  const std::wstring::size_type separator = modulePath.find_last_of(L'\\');
  assert(separator != std::wstring::npos);
  const std::wstring executableDirectory = modulePath.substr(0, separator);
  const std::wstring fixturePath = executableDirectory + L"\\" + fileNameWide;

  DeleteFileW(fixturePath.c_str());
  std::FILE *seed = _wfopen(fixturePath.c_str(), L"wb");
  assert(seed);
  LOKA_VERIFY(std::fwrite(expected, 1, sizeof(expected), seed) == sizeof(expected));
  LOKA_VERIFY(std::fclose(seed) == 0);

  wchar_t originalDirectory[32768];
  const DWORD originalLength = GetCurrentDirectoryW(32768, originalDirectory);
  assert(originalLength > 0 && originalLength < 32768);
  wchar_t tempDirectory[MAX_PATH];
  const DWORD tempLength = GetTempPathW(MAX_PATH, tempDirectory);
  assert(tempLength > 0 && tempLength < MAX_PATH);
  std::wstring alternateDirectory(tempDirectory, tempDirectory + tempLength);
  alternateDirectory += L"loka-application-cwd-probe";
  RemoveDirectoryW(alternateDirectory.c_str());
  const BOOL madeAlternate = CreateDirectoryW(alternateDirectory.c_str(), NULL);
  LOKA_VERIFY(madeAlternate || GetLastError() == ERROR_ALREADY_EXISTS);
  assert(alternateDirectory != executableDirectory);
  LOKA_VERIFY(SetCurrentDirectoryW(alternateDirectory.c_str()));

  const loka::file::File item = loka::file::File::Application()
                                << loka::file::File(loka::core::String::Utf8(fileNameUtf8, sizeof(fileNameUtf8) - 1));
  loka::platform::file::FileHandle handle;
  const bool resolved = loka::platform::file::ResolveApplicationItem(item, handle);
  LOKA_VERIFY(SetCurrentDirectoryW(originalDirectory));
  LOKA_VERIFY(RemoveDirectoryW(alternateDirectory.c_str()));
  assert(resolved);

  std::wstring resolvedPath;
  LOKA_VERIFY(loka::win32::MaterializeWideString(handle.displayPath, resolvedPath));
  const std::wstring::size_type resolvedSeparator = resolvedPath.find_last_of(L'\\');
  assert(resolvedSeparator != std::wstring::npos);
  assert(resolvedPath.substr(0, resolvedSeparator) == executableDirectory);
  assert(resolvedPath == fixturePath);

  std::FILE *opened = loka::platform::file::OpenRead(handle.displayPath);
  assert(opened);
  unsigned char actual[sizeof(expected)] = {0};
  LOKA_VERIFY(std::fread(actual, 1, sizeof(actual), opened) == sizeof(actual));
  LOKA_VERIFY(std::fclose(opened) == 0);
  for (std::size_t i = 0; i < sizeof(expected); ++i)
  {
    assert(actual[i] == expected[i]);
  }

  const loka::file::File nested = loka::file::File::Application()
                                  << loka::file::File("Pictures") << loka::file::File("X");
  LOKA_VERIFY(loka::platform::file::ResolveApplicationItem(nested, handle));
  LOKA_VERIFY(loka::win32::MaterializeWideString(handle.displayPath, resolvedPath));
  LOKA_VERIFY(resolvedPath == executableDirectory + L"\\Pictures/X");
  const char *refused[] = {"A/B/C", "../B", "A/..", "A/", "/A", "A//B", ".", "A/."};
  for (std::size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i)
    LOKA_VERIFY(!loka::platform::file::ApplicationRelativeIsOpenable(loka::file::File::Application()
                                                                     << loka::file::File(refused[i])));

  LOKA_VERIFY(DeleteFileW(fixturePath.c_str()));
}
