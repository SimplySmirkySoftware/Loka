#include "Win32DisplayScale.hpp"
#include <cassert>
#include <climits>

namespace
{
  const UINT kDefaultDpi = 96;

  typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);
  typedef UINT(WINAPI *GetDpiForSystemFn)();
  typedef HRESULT(WINAPI *GetDpiForMonitorFn)(HMONITOR, int, UINT *, UINT *);
  typedef BOOL(WINAPI *AdjustWindowRectExForDpiFn)(LPRECT, DWORD, BOOL, DWORD, UINT);

  /** Process-lifetime capability box that keeps Shcore loaded for as long as
      its resolved GetDpiForMonitor entry point can be called. */
  class MonitorDpiQuery
  {
  public:
    MonitorDpiQuery()
        : module_(0),
          function_(0)
    {
      // XP rejects this flag and follows the DC fallback. Windows 8.1 can
      // load Shcore only from System32 without opening a DLL search path.
      const DWORD kLoadLibrarySearchSystem32 = 0x00000800;
      this->module_ = LoadLibraryExW(
          L"shcore.dll", NULL, kLoadLibrarySearchSystem32);
      if (this->module_)
      {
        this->function_ = reinterpret_cast<GetDpiForMonitorFn>(
            GetProcAddress(this->module_, "GetDpiForMonitor"));
      }
    }

    ~MonitorDpiQuery()
    {
      if (this->module_)
      {
        FreeLibrary(this->module_);
        this->module_ = 0;
        this->function_ = 0;
      }
    }

    bool read(HWND hwnd, UINT &out) const
    {
      const HMONITOR monitor = MonitorFromWindow(
          hwnd, MONITOR_DEFAULTTONEAREST);
      UINT dpiX = 0;
      UINT dpiY = 0;
      if (!this->function_ || !monitor
          || FAILED(this->function_(monitor, 0, &dpiX, &dpiY))
          || dpiX == 0)
      {
        return false;
      }
      out = dpiX;
      return true;
    }

  private:
    HMODULE module_;
    GetDpiForMonitorFn function_;

    MonitorDpiQuery(const MonitorDpiQuery &);
    MonitorDpiQuery &operator=(const MonitorDpiQuery &);
  };

  FARPROC ResolveUser32Procedure(const char *name)
  {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    return user32 ? GetProcAddress(user32, name) : 0;
  }

  typedef int(WINAPI *GetSystemMetricsForDpiFn)(int, UINT);

  int ReadSystemMetricForDpi(int index, UINT dpi)
  {
    static GetSystemMetricsForDpiFn getSystemMetricsForDpi =
        reinterpret_cast<GetSystemMetricsForDpiFn>(ResolveUser32Procedure("GetSystemMetricsForDpi"));
    if (getSystemMetricsForDpi)
    {
      return getSystemMetricsForDpi(index, dpi);
    }
    return GetSystemMetrics(index);
  }

  UINT ReadSystemDpiFromDeviceContext()
  {
    HDC dc = GetDC(NULL);
    if (!dc)
    {
      return kDefaultDpi;
    }
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return dpi > 0 ? static_cast<UINT>(dpi) : kDefaultDpi;
  }

  bool ReadWindowMonitorDpi(HWND hwnd, UINT &out)
  {
    static MonitorDpiQuery query;
    return query.read(hwnd, out);
  }
} // namespace

namespace loka
{
  namespace win32
  {
    Win32DisplayScale::Win32DisplayScale(UINT dpi, const loka::app::RailMetrics &metrics)
        : dpi_(dpi > 0 ? dpi : kDefaultDpi), metrics_(metrics)
    {
    }

    bool Win32DisplayScale::queryForWindow(HWND hwnd, Win32DisplayScale &out)
    {
      static GetDpiForWindowFn getDpiForWindow =
          reinterpret_cast<GetDpiForWindowFn>(ResolveUser32Procedure("GetDpiForWindow"));
      if (hwnd && getDpiForWindow)
      {
        const UINT dpi = getDpiForWindow(hwnd);
        if (dpi > 0)
        {
          out = Win32DisplayScale(dpi);
          return true;
        }
      }
      UINT monitorDpi = 0;
      if (hwnd && ReadWindowMonitorDpi(hwnd, monitorDpi))
      {
        out = Win32DisplayScale(monitorDpi);
        return true;
      }
      if (hwnd)
      {
        HDC dc = GetDC(hwnd);
        if (dc)
        {
          const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
          ReleaseDC(hwnd, dc);
          if (dpi > 0)
          {
            out = Win32DisplayScale(static_cast<UINT>(dpi));
            return true;
          }
        }
      }
      return false;
    }

    Win32DisplayScale Win32DisplayScale::forWindow(HWND hwnd)
    {
      Win32DisplayScale result;
      return Win32DisplayScale::queryForWindow(hwnd, result)
                 ? result
                 : Win32DisplayScale::forSystem();
    }

    Win32DisplayScale Win32DisplayScale::forSystem()
    {
      static GetDpiForSystemFn getDpiForSystem =
          reinterpret_cast<GetDpiForSystemFn>(ResolveUser32Procedure("GetDpiForSystem"));
      if (getDpiForSystem)
      {
        const UINT dpi = getDpiForSystem();
        if (dpi > 0)
        {
          return Win32DisplayScale(dpi);
        }
      }
      return Win32DisplayScale(ReadSystemDpiFromDeviceContext());
    }

    int Win32DisplayScale::percent() const
    {
      return MulDiv(static_cast<int>(this->dpi_), 100, static_cast<int>(kDefaultDpi));
    }

    bool Win32DisplayScale::spaceFactors(int &numerator, int &denominator) const
    {
      const loka::app::Ratio &space = this->metrics_.spaceScale;
      if (space.isUnit())
      {
        numerator = static_cast<int>(this->dpi_);
        denominator = static_cast<int>(kDefaultDpi);
        return true;
      }
      const bool fits = space.valid()
          && this->dpi_ <= static_cast<UINT>(INT_MAX / space.num)
          && space.den <= INT_MAX / static_cast<int>(kDefaultDpi);
      assert(fits && "space projection products must fit in int");
      if (!fits)
        return false;
      numerator = static_cast<int>(this->dpi_) * space.num;
      denominator = static_cast<int>(kDefaultDpi) * space.den;
      return true;
    }

    int Win32DisplayScale::projectEdge(int logicalCoordinate) const
    {
      if (!this->metrics_.spaceScale.isUnit())
      {
        int numerator = 0;
        int denominator = 0;
        if (!this->spaceFactors(numerator, denominator))
          return -1;
        return MulDiv(logicalCoordinate, numerator, denominator);
      }
      return MulDiv(logicalCoordinate,
                    static_cast<int>(this->dpi_),
                    static_cast<int>(kDefaultDpi));
    }

    namespace
    {
      // Windows supplies a 64-bit integer even on its C++98 toolchains.
      // Division truncates toward zero; adjust only a nonzero remainder.
      int DirectedRatio(int value, int numerator, int denominator, bool up)
      {
        const LONGLONG product = static_cast<LONGLONG>(value) * numerator;
        LONGLONG result = product / denominator;
        const LONGLONG remainder = product % denominator;
        if (up && remainder > 0)
          ++result;
        if (!up && remainder < 0)
          --result;
        const bool fits = result >= INT_MIN && result <= INT_MAX;
        assert(fits && "projected geometry must fit integer device pixels");
        return fits ? static_cast<int>(result) : -1;
      }
    } // namespace

    int Win32DisplayScale::capacityToLu(int px) const
    {
      int numerator = 0, denominator = 0;
      if (!this->spaceFactors(numerator, denominator))
        return -1;
      return DirectedRatio(px, denominator, numerator, false);
    }

    NativeLength Win32DisplayScale::clientLengthToNative(int lu) const
    {
      int numerator = 0, denominator = 0;
      if (!this->spaceFactors(numerator, denominator))
        return NativeLength(-1);
      return NativeLength(DirectedRatio(lu, numerator, denominator, true));
    }

    int Win32DisplayScale::measurementToLu(int px) const
    {
      int numerator = 0, denominator = 0;
      if (!this->spaceFactors(numerator, denominator))
        return -1;
      return DirectedRatio(px, denominator, numerator, true);
    }

    int Win32DisplayScale::clientCapacityToLu(int px) const
    {
      const int capacity = this->capacityToLu(px);
      assert(capacity >= SHRT_MIN && capacity <= SHRT_MAX && "Win32 client capacity must fit short");
      assert(this->projectEdge(capacity) <= px && "Win32 logical capacity must fit native capacity");
      return capacity;
    }

    NativeRect Win32DisplayScale::damageToNative(const loka::core::Frame &lu) const
    {
      int numerator = 0, denominator = 0;
      if (!this->spaceFactors(numerator, denominator))
      {
        const RECT empty = {0, 0, 0, 0};
        return NativeRect(empty);
      }
      const RECT r = {DirectedRatio(lu.x, numerator, denominator, false),
                      DirectedRatio(lu.y, numerator, denominator, false),
                      DirectedRatio(lu.x + lu.width, numerator, denominator, true),
                      DirectedRatio(lu.y + lu.height, numerator, denominator, true)};
      return NativeRect(r);
    }

    NativeRect Win32DisplayScale::projectDeviceOnly(const loka::core::Frame &pixels) const
    {
      // DPI only, no space scale. Keep the existing sprite rounding.
      return Win32DisplayScale(this->dpi_).projectFrame(pixels);
    }

    // A font size is a logical unit like every other layout number on this
    // rail: one lu is one pixel at 96 dpi (the message font's 9 pt is 12 px
    // = 12 lu). Treating it as a point (dpi/72) made an 18 lu Text 33 % too
    // large next to the 12 lu body text.
    int Win32DisplayScale::fontHeightToNative(int logicalUnits) const
    {
      const loka::app::Ratio &font = this->metrics_.fontScale;
      if (font.isUnit())
        return MulDiv(logicalUnits, static_cast<int>(this->dpi_), static_cast<int>(kDefaultDpi));
      const bool fits = font.valid() && this->dpi_ <= static_cast<UINT>(INT_MAX / font.num)
                        && font.den <= INT_MAX / static_cast<int>(kDefaultDpi);
      assert(fits && "font projection products must fit in int");
      return fits ? MulDiv(logicalUnits, static_cast<int>(this->dpi_) * font.num,
                           static_cast<int>(kDefaultDpi) * font.den)
                  : -1;
    }

    int Win32DisplayScale::intrinsicPixelsToLu(int nativeCoordinate) const
    {
      if (!this->metrics_.spaceScale.isUnit())
      {
        int numerator = 0;
        int denominator = 0;
        if (!this->spaceFactors(numerator, denominator))
          return -1;
        return MulDiv(nativeCoordinate, denominator, numerator);
      }
      return MulDiv(nativeCoordinate,
                    static_cast<int>(kDefaultDpi),
                    static_cast<int>(this->dpi_));
    }

    int Win32DisplayScale::scaleLengthFrom(
        const Win32DisplayScale &sourceScale,
        int sourceLength) const
    {
      return MulDiv(sourceLength,
                    static_cast<int>(this->dpi_),
                    static_cast<int>(sourceScale.dpi()));
    }

    NativeRect Win32DisplayScale::projectFrame(const loka::core::Frame &lu) const
    {
      const NativeEdge left = this->nativeEdge(lu.x);
      const NativeEdge top = this->nativeEdge(lu.y);
      const NativeEdge right = this->nativeEdge(lu.x + lu.width);
      const NativeEdge bottom = this->nativeEdge(lu.y + lu.height);
      const RECT r = {left.px, top.px, right.px, bottom.px};
      // Integer fields make subpixel output unrepresentable on this rail.
      assert(r.left == left.px && r.right == right.px);
      return NativeRect(r);
    }

    loka::core::Frame Win32DisplayScale::windowContentFrameFromNative(
        const RECT &nativeWindowRect,
        int nativeClientWidth,
        int nativeClientHeight) const
    {
      return loka::core::Frame(nativeWindowRect.left,
                               nativeWindowRect.top,
                               this->clientCapacityToLu(nativeClientWidth),
                               this->clientCapacityToLu(nativeClientHeight));
    }

    bool Win32DisplayScale::adjustWindowRect(RECT &nativeClientRect,
                                             DWORD style,
                                             BOOL hasMenu,
                                             DWORD exStyle) const
    {
      static AdjustWindowRectExForDpiFn adjustWindowRectExForDpi =
          reinterpret_cast<AdjustWindowRectExForDpiFn>(
              ResolveUser32Procedure("AdjustWindowRectExForDpi"));
      if (adjustWindowRectExForDpi)
      {
        return adjustWindowRectExForDpi(
                   &nativeClientRect, style, hasMenu, exStyle, this->dpi_)
               != FALSE;
      }
      RECT systemAdjusted = nativeClientRect;
      if (!AdjustWindowRectEx(&systemAdjusted, style, hasMenu, exStyle))
      {
        return false;
      }
      // The legacy API returns non-client edge deltas at system DPI. Preserve
      // the already projected client rect and move each edge by the same
      // metric expressed at the target monitor DPI.
      const Win32DisplayScale systemScale = Win32DisplayScale::forSystem();
      nativeClientRect.left += this->scaleLengthFrom(
          systemScale,
          static_cast<int>(systemAdjusted.left - nativeClientRect.left));
      nativeClientRect.top += this->scaleLengthFrom(
          systemScale,
          static_cast<int>(systemAdjusted.top - nativeClientRect.top));
      nativeClientRect.right += this->scaleLengthFrom(
          systemScale,
          static_cast<int>(systemAdjusted.right - nativeClientRect.right));
      nativeClientRect.bottom += this->scaleLengthFrom(
          systemScale,
          static_cast<int>(systemAdjusted.bottom - nativeClientRect.bottom));
      return true;
    }
    int Win32DisplayScale::systemMetric(int index) const
    {
      return ReadSystemMetricForDpi(index, this->dpi_);
    }

    int Win32DisplayScale::clientHeightBelowHorizontalBar(const loka::core::Frame &seatLu) const
    {
      const NativeRect native = this->projectFrame(seatLu);
      const int nativeHeight = native.r.bottom - native.r.top;
      const int bar = this->systemMetric(SM_CYHSCROLL);
      return nativeHeight > bar ? this->clientCapacityToLu(nativeHeight - bar) : 0;
    }

  } // namespace win32
} // namespace loka
