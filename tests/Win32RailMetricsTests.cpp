#include "Win32RailMetricsTests.hpp"
#include "support/TestVerify.hpp"
#include "Win32ScenePlatformController.hpp"
#include <climits>
#include <string>
#include <vector>

namespace
{
  void verifyProjectionPolicy();
  void verifyClientIngressAssertion();
} // namespace

void testWin32RailMetricsProjection()
{
  verifyClientIngressAssertion();
  verifyProjectionPolicy();
  using namespace loka::app;
  using loka::win32::Win32DisplayScale;
  const RailMetrics defaults;
  LOKA_VERIFY(defaults.fontScale == Ratio(1, 1));
  LOKA_VERIFY(defaults.spaceScale == Ratio(1, 1));
  const RailMetrics railDefaults = loka::win32::DefaultRailMetrics();
  LOKA_VERIFY(railDefaults.fontScale == Ratio(1, 1));
  LOKA_VERIFY(railDefaults.spaceScale == Ratio(5, 4));
  Win32ScenePlatformController candidate(NULL, Win32DisplayScale(96, railDefaults));
  LOKA_VERIFY(candidate.displayScale().projectEdge(8) == 10);
  LOKA_VERIFY(candidate.displayScale().capacityToLu(301) == 240);
  LOGFONTW explicitFont;
  LOKA_VERIFY(GetObjectW(candidate.textFont(FontSize<24>()).font(), sizeof(explicitFont), &explicitFont));
  LOKA_VERIFY(explicitFont.lfHeight == -MulDiv(24, 96, 96));
  // Space scaling must not change the default row used by fixed controls.
  loka::win32::Win32DisplayFont unitFonts;
  LOKA_VERIFY(unitFonts.create(Win32DisplayScale(96)));
  LOGFONTW defaultFont;
  LOGFONTW unitFont;
  LOKA_VERIFY(GetObjectW(candidate.displayFont(), sizeof(defaultFont), &defaultFont));
  LOKA_VERIFY(GetObjectW(unitFonts.get(), sizeof(unitFont), &unitFont));
  LOKA_VERIFY(defaultFont.lfHeight == unitFont.lfHeight);
  LOKA_VERIFY(!Ratio(3, 2).isUnit());
  Ratio invalid;
  invalid.den = 0;
  LOKA_VERIFY(!invalid.valid());
  invalid.den = -1;
  LOKA_VERIFY(!invalid.valid());

  const Win32DisplayScale unit(144, defaults);
  const int coordinates[] = {-301, -33, -9, -3, -1, 0, 1, 3, 8, 9, 33, 301};
  for (unsigned int i = 0; i < sizeof(coordinates) / sizeof(coordinates[0]); ++i)
  {
    const int value = coordinates[i];
    // Pin today's MulDiv rule directly, including negative edges and ties.
    LOKA_VERIFY(unit.projectEdge(value) == MulDiv(value, 144, 96));
    LOKA_VERIFY(unit.projectLength(value) == MulDiv(value, 144, 96));
    LOKA_VERIFY(unit.unprojectEdge(value) == MulDiv(value, 96, 144));
    LOKA_VERIFY(unit.unprojectLength(value) == MulDiv(value, 96, 144));
    const loka::core::Frame frame(value, -value, 9, 3);
    RECT projected;
    unit.projectFrame(frame, projected);
    LOKA_VERIFY(projected.left == MulDiv(value, 144, 96));
    LOKA_VERIFY(projected.top == MulDiv(-value, 144, 96));
    LOKA_VERIFY(projected.right == MulDiv(value + 9, 144, 96));
    LOKA_VERIFY(projected.bottom == MulDiv(-value + 3, 144, 96));
  }

  const RailMetrics metrics(Ratio(5, 4), Ratio(3, 2));
  const Win32DisplayScale scaled(96, metrics);
  LOKA_VERIFY(scaled.dpi() == 96 && scaled.percent() == 100);
  LOKA_VERIFY(scaled.projectEdge(8) == 12);
  LOKA_VERIFY(scaled.projectEdge(9) == 14);
  LOKA_VERIFY(scaled.projectLength(9) == 14);
  LOKA_VERIFY(scaled.projectEdge(-9) == -14);
  LOKA_VERIFY(scaled.unprojectEdge(12) == 8);
  // Today's nearest inverse; floor-capacity containment belongs to PR a'.
  LOKA_VERIFY(scaled.unprojectLength(301) == 201);
  RECT projected;
  scaled.projectFrame(loka::core::Frame(-9, 3, 9, 5), projected);
  LOKA_VERIFY(projected.left == -14 && projected.top == 5);
  LOKA_VERIFY(projected.right == 0 && projected.bottom == 12);
  LOKA_VERIFY(scaled != Win32DisplayScale(96));
  LOKA_VERIFY(scaled == Win32DisplayScale(96, metrics));
  // Non-client/native metrics remain DPI-only, even with non-unit rail metrics.
  LOKA_VERIFY(scaled.scaleLengthFrom(Win32DisplayScale(144), 12) == 8);
  const Win32DisplayScale equivalentUnit(144, RailMetrics(Ratio(), Ratio(INT_MAX, INT_MAX)));
  LOKA_VERIFY(equivalentUnit.projectEdge(9) == unit.projectEdge(9));
#ifdef NDEBUG
  const Win32DisplayScale overflow(144, RailMetrics(Ratio(), Ratio(INT_MAX, 1)));
  LOKA_VERIFY(overflow.projectEdge(1) == -1);
  LOKA_VERIFY(overflow.unprojectEdge(1) == -1);
  const Win32DisplayScale denominatorOverflow(96, RailMetrics(Ratio(), Ratio(1, INT_MAX)));
  LOKA_VERIFY(denominatorOverflow.projectLength(1) == -1);
  LOKA_VERIFY(!Ratio(1, 0).valid());
  LOKA_VERIFY(!Ratio(1, -1).valid());
#else
  std::printf("[skip] Win32 overflow refusal pins require NDEBUG; Ratio debug death pin runs on Linux.\n");
#endif
}

void testWin32RailMetricsSurviveDpiChange()
{
  using namespace loka::app;
  using loka::win32::Win32DisplayScale;
  const RailMetrics metrics(Ratio(5, 4), Ratio(3, 2));
  // No native subtree is needed to exercise the controller's DPI/font update.
  Win32ScenePlatformController controller(NULL, Win32DisplayScale(96, metrics));
  LOKA_VERIFY(controller.displayScale().railMetrics() == metrics);
  LOGFONTW font;
  LOKA_VERIFY(GetObjectW(controller.textFont(FontSize<24>()).font(), sizeof(font), &font));
  LOKA_VERIFY(font.lfHeight == -MulDiv(24, 96 * 5, 96 * 4));
  // A font-only change must not compare equal in the font-table cache.
  loka::win32::Win32DisplayFont table;
  LOKA_VERIFY(table.create(Win32DisplayScale(96)));
  LOKA_VERIFY(!table.matches(Win32DisplayScale(96, RailMetrics(Ratio(3, 2), Ratio()))));
  LOGFONTW nativeFont;
  LOKA_VERIFY(GetObjectW(table.get(), sizeof(nativeFont), &nativeFont));
  LOKA_VERIFY(GetObjectW(controller.displayFont(), sizeof(font), &font));
  LOKA_VERIFY(font.lfHeight == nativeFont.lfHeight);

  controller.updateDisplayScale(Win32DisplayScale(144));
  LOKA_VERIFY(controller.displayScale().dpi() == 144);
  LOKA_VERIFY(controller.displayScale().percent() == 150);
  LOKA_VERIFY(controller.displayScale().railMetrics() == metrics);
  LOKA_VERIFY(controller.displayScale().projectEdge(8) == 18);
  LOKA_VERIFY(GetObjectW(controller.textFont(FontSize<24>()).font(), sizeof(font), &font));
  LOKA_VERIFY(font.lfHeight == -MulDiv(24, 144 * 5, 96 * 4));
  controller.updateDisplayScale(Win32DisplayScale(96));
  LOKA_VERIFY(controller.displayScale().railMetrics() == metrics);
  LOKA_VERIFY(controller.displayScale().projectEdge(8) == 12);
}

void testWin32SmallLabelUsesTheMessageFaceAtTenUnits()
{
  using namespace loka::app;
  using loka::win32::Win32DisplayScale;
  loka::win32::Win32DisplayFont table;
  LOKA_VERIFY(table.create(Win32DisplayScale(96)));
  LOGFONTW body;
  LOKA_VERIFY(GetObjectW(table.get(), sizeof(body), &body));
  // The role has its own row: the message face at -10 px, not the default row.
  const HFONT caption = table.find(SmallLabel).font();
  LOGFONTW font;
  LOKA_VERIFY(caption && caption != table.get());
  LOKA_VERIFY(GetObjectW(caption, sizeof(font), &font));
  LOKA_VERIFY(font.lfHeight == -10);
  LOKA_VERIFY(lstrcmpW(font.lfFaceName, body.lfFaceName) == 0);
  LOKA_VERIFY(font.lfWeight == body.lfWeight);
  // Weight composes with the role through the row's bold column.
  const HFONT captionBold = table.find(SmallLabel + Bold).font();
  LOKA_VERIFY(captionBold && captionBold != caption);
  LOKA_VERIFY(GetObjectW(captionBold, sizeof(font), &font));
  LOKA_VERIFY(font.lfHeight == -10 && font.lfWeight == FW_BOLD);
  // The row follows dpi and the rail's font scale like the numeric rows, and a
  // table swap carries a borrowed SmallLabel handle to the new table's row.
  loka::win32::Win32DisplayFont scaled;
  LOKA_VERIFY(scaled.create(Win32DisplayScale(144, RailMetrics(Ratio(5, 4), Ratio()))));
  LOKA_VERIFY(GetObjectW(scaled.find(SmallLabel).font(), sizeof(font), &font));
  LOKA_VERIFY(font.lfHeight == -MulDiv(10, 144 * 5, 96 * 4));
  LOKA_VERIFY(table.replacementFor(caption, scaled) == scaled.find(SmallLabel).font());
}

namespace
{
  void verifyProjectionPolicy()
  {
    using loka::core::Frame;
    using loka::win32::Win32DisplayScale;
    const Win32DisplayScale identity(96);
    const int values[] = {-301, -33, -2, -1, 0, 1, 2, 33, 301};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
      const int value = values[i];
      LOKA_VERIFY(identity.projectEdge(value) == value);
      LOKA_VERIFY(identity.nativeEdge(value).px == value);
      LOKA_VERIFY(identity.nativeLength(0, value).px == value);
      LOKA_VERIFY(identity.capacityToLu(value) == value);
      LOKA_VERIFY(identity.measurementToLu(value) == value);
      LOKA_VERIFY(identity.scrollOffsetToNative(value) == value);
      LOKA_VERIFY(identity.scrollPositionToLu(value) == value);
      const loka::win32::NativeRect frame = identity.projectFrame(Frame(value, value, 1, 2));
      const loka::win32::NativeRect damage = identity.damageToNative(Frame(value, value, 1, 2));
      LOKA_VERIFY(frame.r.left == value && frame.r.top == value);
      LOKA_VERIFY(frame.r.right == value + 1 && frame.r.bottom == value + 2);
      LOKA_VERIFY(EqualRect(&frame.r, &damage.r));
    }
    const Win32DisplayScale equivalentUnit(
        96, loka::app::RailMetrics(loka::app::Ratio(), loka::app::Ratio(INT_MAX, INT_MAX)));
    LOKA_VERIFY(equivalentUnit.capacityToLu(301) == 301);
    LOKA_VERIFY(equivalentUnit.measurementToLu(-301) == -301);
    const Win32DisplayScale scales[] = {
        Win32DisplayScale(144),
        Win32DisplayScale(96, loka::app::RailMetrics(loka::app::Ratio(), loka::app::Ratio(3, 2)))};
    for (unsigned i = 0; i < sizeof(scales) / sizeof(scales[0]); ++i)
    {
      const Win32DisplayScale &scale = scales[i];
      LOKA_VERIFY(scale.capacityToLu(301) == 200);
      LOKA_VERIFY(scale.clientCapacityToLu(301) == 200);
      LOKA_VERIFY(scale.projectEdge(200) == 300);
      LOKA_VERIFY(scale.projectEdge(scale.capacityToLu(301)) <= 301);
      LOKA_VERIFY(scale.measurementToLu(301) == 201);
      LOKA_VERIFY(scale.capacityToLu(-301) == -201);
      LOKA_VERIFY(scale.measurementToLu(-301) == -200);
      LOKA_VERIFY(scale.projectEdge(1) == 2 && scale.projectEdge(-1) == -2);
      const loka::win32::NativeRect frame = scale.projectFrame(Frame(1, 1, 1, 1));
      LOKA_VERIFY(frame.r.right == scale.projectEdge(2));
      LOKA_VERIFY(frame.r.right == 3 && frame.r.right - frame.r.left == 1);
      const loka::win32::NativeRect damage = scale.damageToNative(Frame(1, 1, 1, 1));
      LOKA_VERIFY(damage.r.left == 1 && damage.r.top == 1);
      LOKA_VERIFY(damage.r.right == 3 && damage.r.bottom == 3);
      const loka::win32::NativeRect negative = scale.damageToNative(Frame(-1, -1, 2, 2));
      LOKA_VERIFY(negative.r.left == -2 && negative.r.top == -2);
      LOKA_VERIFY(negative.r.right == 2 && negative.r.bottom == 2);
      LOKA_VERIFY(scale.scrollPositionToLu(scale.scrollOffsetToNative(301)) == 301);
    }
    // Decoded sprite geometry is DPI only, no space scale.
    const loka::win32::NativeRect sprite = scales[1].projectDeviceOnly(Frame(1, 1, 1, 1));
    LOKA_VERIFY(sprite.r.left == 1 && sprite.r.right == 2);
  }

  void verifyClientIngressAssertion()
  {
#ifndef NDEBUG
    const char *mode = std::getenv("LOKA_WIN32_CAPACITY_PROBE");
    if (mode)
    {
      // Exercise the actual controller ingress, even without a root node.
      Win32ScenePlatformController controller(static_cast<HWND>(0), loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
      controller.relayoutNativeClientPixels(mode[0] == 'o' ? SHRT_MAX + 1 : SHRT_MAX, 1);
      ExitProcess(0);
    }
    wchar_t executable[32768];
    const DWORD length = GetModuleFileNameW(NULL, executable, 32768);
    LOKA_VERIFY(length > 0 && length < 32768);
    for (int overflow = 0; overflow != 2; ++overflow)
    {
      SECURITY_ATTRIBUTES security = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};
      HANDLE readPipe = NULL, writePipe = NULL;
      LOKA_VERIFY(CreatePipe(&readPipe, &writePipe, &security, 0));
      LOKA_VERIFY(SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0));
      STARTUPINFOW startup = {};
      startup.cb = sizeof(startup);
      startup.dwFlags = STARTF_USESTDHANDLES;
      startup.hStdError = writePipe;
      startup.hStdOutput = writePipe;
      startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
      PROCESS_INFORMATION process = {};
      std::wstring command = L"\"";
      command += executable;
      command += L"\" testWin32RailMetricsProjection";
      std::vector<wchar_t> mutableCommand(command.begin(), command.end());
      mutableCommand.push_back(0);
      LOKA_VERIFY(SetEnvironmentVariableW(L"LOKA_WIN32_CAPACITY_PROBE", overflow ? L"overflow" : L"valid"));
      const BOOL created =
          CreateProcessW(NULL, &mutableCommand[0], NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process);
      LOKA_VERIFY(SetEnvironmentVariableW(L"LOKA_WIN32_CAPACITY_PROBE", NULL));
      LOKA_VERIFY(created);
      CloseHandle(writePipe);
      const DWORD waited = WaitForSingleObject(process.hProcess, 5000);
      if (waited != WAIT_OBJECT_0)
        TerminateProcess(process.hProcess, 1);
      LOKA_VERIFY(waited == WAIT_OBJECT_0);
      DWORD code = 0;
      LOKA_VERIFY(GetExitCodeProcess(process.hProcess, &code));
      std::string diagnostic;
      char buffer[1024];
      DWORD read = 0;
      while (ReadFile(readPipe, buffer, sizeof(buffer), &read, NULL) && read)
        diagnostic.append(buffer, read);
      CloseHandle(readPipe);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
      if (overflow)
      {
        LOKA_VERIFY(code != 0);
        LOKA_VERIFY(diagnostic.find("Win32 client capacity must fit short") != std::string::npos);
      }
      else
      {
        LOKA_VERIFY(code == 0);
      }
    }
#else
    std::printf("[skip] Win32 client-capacity assertion pin requires a debug build.\n");
#endif
  }
} // namespace

namespace
{
  void verifyMonospaceFont(HFONT font)
  {
    LOKA_VERIFY(font);
    HDC dc = CreateCompatibleDC(NULL);
    LOKA_VERIFY(dc);
    HGDIOBJ previous = SelectObject(dc, font);
    LOKA_VERIFY(previous && previous != HGDI_ERROR);
    wchar_t face[LF_FACESIZE];
    TEXTMETRICW metrics;
    int narrow = 0, wide = 0;
    LOKA_VERIFY(GetTextFaceW(dc, LF_FACESIZE, face));
    LOKA_VERIFY(lstrcmpiW(face, L"Consolas") == 0 || lstrcmpiW(face, L"Lucida Console") == 0
                || lstrcmpiW(face, L"Courier New") == 0);
    LOKA_VERIFY(GetTextMetricsW(dc, &metrics));
    LOKA_VERIFY(!(metrics.tmPitchAndFamily & TMPF_FIXED_PITCH));
    LOKA_VERIFY(GetCharWidth32W(dc, L'i', L'i', &narrow));
    LOKA_VERIFY(GetCharWidth32W(dc, L'W', L'W', &wide));
    LOKA_VERIFY(narrow > 0 && narrow == wide);
    SelectObject(dc, previous);
    DeleteDC(dc);
  }
} // namespace

void testWin32TextFamilyMonospaceSlot()
{
  using namespace loka::app;
  loka::win32::Win32DisplayFont table(loka::win32::Win32DisplayScale(96));
  LOKA_VERIFY(table.get());
  LOKA_VERIFY(table.find(TextStyle()).font() == table.get());
  LOGFONTW messageDescriptor, bodyDescriptor;
  LOKA_VERIFY(GetObjectW(table.get(), sizeof(messageDescriptor), &messageDescriptor));
  LOKA_VERIFY(GetObjectW(table.find(Body).font(), sizeof(bodyDescriptor), &bodyDescriptor));
  LOKA_VERIFY(lstrcmpiW(messageDescriptor.lfFaceName, bodyDescriptor.lfFaceName) == 0);
  LOKA_VERIFY(table.find(Body).family() == TEXT_FAMILY_SYSTEM);
  LOKA_VERIFY(table.find(TextStyle().family(TEXT_FAMILY_SYSTEM)).font() == table.get());
  HDC dc = CreateCompatibleDC(NULL);
  LOKA_VERIFY(dc);
  HGDIOBJ previous = SelectObject(dc, table.get());
  LOKA_VERIFY(previous && previous != HGDI_ERROR);
  TEXTMETRICW metrics;
  wchar_t face[LF_FACESIZE];
  LOKA_VERIFY(GetTextFaceW(dc, LF_FACESIZE, face));
  LOKA_VERIFY(GetTextMetricsW(dc, &metrics));
  if (!(metrics.tmPitchAndFamily & TMPF_FIXED_PITCH))
    std::printf("[skip] message face is fixed pitch: %ls\n", face);
  SelectObject(dc, previous);
  DeleteDC(dc);
  if (!table.find(Monospace).font())
  {
    std::printf("[skip] monospace candidates: none of Consolas / Lucida Console / Courier New installed\n");
    return;
  }
  LOKA_VERIFY(table.find(Monospace).family() == TEXT_FAMILY_MONOSPACE);
  verifyMonospaceFont(table.find(Monospace).font());
  verifyMonospaceFont(table.find(Monospace + Bold).font());
  LOGFONTW descriptor;
  LOKA_VERIFY(GetObjectW(table.find(Monospace + Bold).font(), sizeof(descriptor), &descriptor));
  LOKA_VERIFY(descriptor.lfWeight == FW_BOLD);
  verifyMonospaceFont(table.find(Monospace + FontSize<12>()).font());
  LOKA_VERIFY(GetObjectW(table.find(Monospace + FontSize<12>()).font(), sizeof(descriptor), &descriptor));
  LOKA_VERIFY(descriptor.lfHeight == -12);
}

void testWin32TextFamilyNoCandidate()
{
  using namespace loka::app;
  static const wchar_t *const absent[] = {
      L"Loka missing mono face 1", L"Loka missing mono face 2", L"Loka missing mono face 3", 0};
  loka::win32::testing::useMonospaceCandidatesForNextBuild(absent);
  loka::win32::Win32DisplayFont table(loka::win32::Win32DisplayScale(96));
  LOKA_VERIFY(table.get());
  const loka::win32::Win32FontResolution &resolution = table.find(Monospace);
  LOKA_VERIFY(!resolution.font() && resolution.family() == TEXT_FAMILY_MONOSPACE);
  LOKA_VERIFY(Win32ScenePlatformController::fallbackFont(table, Monospace) == table.get());
  LOKA_VERIFY(table.find(TextStyle()).family() == TEXT_FAMILY_SYSTEM);
  loka::win32::Win32DisplayFont natural(loka::win32::Win32DisplayScale(96));
  if (!natural.find(Monospace).font())
    std::printf("[skip] monospace candidates: none of Consolas / Lucida Console / Courier New installed\n");
  else
    verifyMonospaceFont(natural.find(Monospace).font());
}
