#include "ToolboxGroundTests.hpp"
#include "app/style/Style.hpp"
#define LOKA_STYLE_VOCAB_DEFINE_NAMES
#include "app/style/StyleVocab.hpp"
#undef LOKA_STYLE_VOCAB_DEFINE_NAMES
#include "../apple/toolbox/src/ToolboxGround.hpp"
#include "support/ContrastRatio.hpp"
#include "support/TestVerify.hpp"
#include <cstdio>
#if defined(__linux__) && !defined(NDEBUG)
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

namespace
{
  using namespace loka::toolbox;
  double PlanarLuminance(ToolboxPlanarColor color, bool monochrome)
  {
    if (monochrome)
      return color == TOOLBOX_PLANAR_WHITE ? 1.0 : 0.0;
    switch (color)
    {
    case TOOLBOX_PLANAR_WHITE:
      return loka_test::RelativeLuminance(1.0, 1.0, 1.0);
    case TOOLBOX_PLANAR_RED:
    case TOOLBOX_PLANAR_GREEN:
    case TOOLBOX_PLANAR_BLUE:
    case TOOLBOX_PLANAR_CYAN:
    case TOOLBOX_PLANAR_MAGENTA:
    case TOOLBOX_PLANAR_YELLOW:
      break;
    case TOOLBOX_PLANAR_BLACK:
      return loka_test::RelativeLuminance(0.0, 0.0, 0.0);
    }
    LOKA_VERIFY(false);
    return 0.0;
  }
}

void testToolboxGroundRoles()
{
  using namespace loka::app;
  ToolboxPlanarColor color = TOOLBOX_PLANAR_BLACK;
  LOKA_VERIFY(!QueryToolboxGroundColor(SURFACE_GROUND_TRANSPARENT, color));
  LOKA_VERIFY(color == TOOLBOX_PLANAR_BLACK);
  LOKA_VERIFY(!QueryToolboxGroundColor(SURFACE_GROUND_NATIVE, color));
  LOKA_VERIFY(color == TOOLBOX_PLANAR_BLACK);
  LOKA_VERIFY(QueryToolboxGroundColor(SURFACE_GROUND_WINDOW, color));
  LOKA_VERIFY(color == TOOLBOX_PLANAR_WHITE);
  LOKA_VERIFY(QueryToolboxGroundColor(SURFACE_GROUND_DOCUMENT, color));
  LOKA_VERIFY(color == TOOLBOX_PLANAR_WHITE);
  LOKA_VERIFY(QueryToolboxGroundColor(SURFACE_GROUND_CONTROL, color));
  LOKA_VERIFY(color == TOOLBOX_PLANAR_WHITE);
  const long expected[] = {33, 33, 33, 30, 205, 341, 409, 273, 137, 69};
  const unsigned count = sizeof(detail::StyleVocabularyColors) / sizeof(detail::StyleVocabularyColors[0]);
  LOKA_VERIFY(count == sizeof(expected) / sizeof(expected[0]));
  for (unsigned i = 0; i < count; ++i)
    LOKA_VERIFY(ToolboxTextInk(detail::StyleVocabularyColors[i].value, SURFACE_GROUND_WINDOW) == expected[i]);
  const SurfaceGround grounds[] = {SURFACE_GROUND_WINDOW, SURFACE_GROUND_DOCUMENT, SURFACE_GROUND_CONTROL};
  for (unsigned i = 0; i < sizeof(grounds) / sizeof(grounds[0]); ++i)
  {
    const long pair = ToolboxTextRoleColor(grounds[i]);
    LOKA_VERIFY(pair == TOOLBOX_PLANAR_BLACK);
    LOKA_VERIFY(ToolboxTextInk(TEXT_COLOR_UNSET, grounds[i]) == pair);
    LOKA_VERIFY(ToolboxTextInk(TEXT_COLOR_PRIMARY, grounds[i]) == pair);
    // Provisional #1196 page 4 pin: a basic GrafPort has no gray.
    LOKA_VERIFY(ToolboxTextInk(TEXT_COLOR_SECONDARY, grounds[i]) == pair);
  }
#ifdef NDEBUG
  LOKA_VERIFY(ToolboxTextRoleColor(SURFACE_GROUND_TRANSPARENT) == -1);
  LOKA_VERIFY(ToolboxTextRoleColor(SURFACE_GROUND_NATIVE) == -1);
#elif defined(__linux__)
  const SurfaceGround declining[] = {SURFACE_GROUND_TRANSPARENT, SURFACE_GROUND_NATIVE};
  for (unsigned i = 0; i < 2; ++i)
  {
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (child == 0)
    {
      ToolboxTextRoleColor(declining[i]);
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  }
#else
  std::puts("[skip] declining-ground assert death pins require Linux; release fallback is pinned with NDEBUG");
#endif
}

void testToolboxGroundLegibility()
{
  using namespace loka::app;
  const SurfaceGround roles[] = {SURFACE_GROUND_TRANSPARENT, SURFACE_GROUND_NATIVE,
      SURFACE_GROUND_WINDOW, SURFACE_GROUND_DOCUMENT, SURFACE_GROUND_CONTROL};
  // The paired text row stays black in both System 7 display columns.
  for (unsigned i = 0; i < sizeof(roles) / sizeof(roles[0]); ++i)
  {
    ToolboxPlanarColor ground;
    if (!QueryToolboxGroundColor(roles[i], ground))
      continue;
    for (int column = 0; column < 2; ++column)
    {
      const double ratio = loka_test::ContrastRatio(
          PlanarLuminance(static_cast<ToolboxPlanarColor>(ToolboxTextRoleColor(roles[i])), column == 0),
          PlanarLuminance(ground, column == 0));
      LOKA_VERIFY(ratio >= 4.5);
    }
  }
}
