#include "Win32OpenURL.hpp"
#include <objbase.h>
#include <vector>

loka::app::URLOpenKind Win32OpenWebURL(const loka::app::WebURL &url, BOOL(WINAPI *execute)(SHELLEXECUTEINFOW *info))
{
  const loka::core::StringBuffer bytes = url.text().bufferWithEncoding(loka::core::StringEncodingUtf8);
  std::vector<wchar_t> wide(bytes.length() + 1, 0);
  for (std::size_t i = 0; i < bytes.length(); ++i)
  {
    const unsigned char byte = static_cast<unsigned char>(bytes.data()[i]);
    if (byte > 127)
      return loka::app::URL_NOT_OPENED;
    wide[i] = static_cast<wchar_t>(byte);
  }
  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_FLAG_NO_UI;
  info.hwnd = NULL;
  info.lpVerb = L"open";
  info.lpFile = &wide[0];
  info.nShow = SW_SHOWNORMAL;

  // Deliberate twin of Win32PlatformContext::createImageFromBlob: own only
  // successful initialization; an already-MTA host gets best-effort execution.
  const HRESULT initialized = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
  if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
    return loka::app::URL_NOT_OPENED;
  const BOOL accepted = execute(&info);
  // Uninitialization can pump too: it remains inside the door's LAUNCHING phase.
  if (SUCCEEDED(initialized))
    CoUninitialize();
  return accepted ? loka::app::URL_OPENED : loka::app::URL_NOT_OPENED;
}
