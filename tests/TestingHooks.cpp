// Implementations of TEST_BUILD-only failure-injection hooks declared in
// library and scenario-test headers. They live in a test translation unit
// on purpose: platform core libraries (LokaWin32Core etc.) are built without
// TEST_BUILD and must stay hook-free, while every test executable compiles
// this file via LOKA_SHARED_TEST_SOURCES.
#include "app/Menu.hpp"
#include "core/LokaAlloc.hpp"
#include "support/LokaAllocFailure.hpp"
#include <cstring>
#include <cassert>
#include "scenarios/ObservedMainDefinition.hpp"
#include "scenarios/ScenarioReel.hpp"

#ifdef TEST_BUILD

namespace loka
{
  namespace testing
  {
    namespace
    {
      unsigned macStackUnitViewFailures = 0;
      unsigned macCardBitmapFailures = 0;
      unsigned macCardPathFailures = 0;
    }
    void failMacStackUnitViews(unsigned count)
    {
      macStackUnitViewFailures = count;
    }
    bool declineMacStackUnitView()
    {
      if (!macStackUnitViewFailures)
        return false;
      --macStackUnitViewFailures;
      return true;
    }
    void failMacCardBitmaps(unsigned count)
    {
      macCardBitmapFailures = count;
    }
    bool declineMacCardBitmap()
    {
      if (!macCardBitmapFailures)
        return false;
      --macCardBitmapFailures;
      return true;
    }
    void failMacCardPaths(unsigned count)
    {
      macCardPathFailures = count;
    }
    bool declineMacCardPath()
    {
      if (!macCardPathFailures)
        return false;
      --macCardPathFailures;
      return true;
    }
  } // namespace testing
} // namespace loka

#ifdef LOKA_HOST_MENU_ATTACHMENT
#include "toolbox/host/Menus.h"
#include "support/TestVerify.hpp"
namespace toolbox_host
{
  unsigned menuInitializations = 0;
  std::vector<MenuAllocation> menuAllocations;
  std::vector<MenuInsertion> menuInsertions;
  std::vector<ItemCmdCall> itemMarkCalls;
  namespace
  {
    unsigned creationFailures = 0, creationCall = 0;
    unsigned separatorFailures = 0, placeholderFailures = 0, insertFailures = 0;
    bool consume(unsigned &count)
    {
      if (!count)
        return false;
      --count;
      return true;
    }
  } // namespace
  namespace testing
  {
    void failMenuCreations(unsigned count)
    {
      creationFailures = count;
    }
    void failMenuCreationCall(unsigned call)
    {
      creationCall = call;
    }
    void failSeparatorAppends(unsigned count)
    {
      separatorFailures = count;
    }
    void failPlaceholderAppends(unsigned count)
    {
      placeholderFailures = count;
    }
    void failItemInserts(unsigned count)
    {
      insertFailures = count;
    }
  } // namespace testing
  bool refuseMenuCreation()
  {
    const bool nth = creationCall && --creationCall == 0;
    return consume(creationFailures) || nth;
  }
  bool refuseMenuAppend(const unsigned char *text)
  {
    return consume(text[0] == 1 && text[1] == '-' ? separatorFailures : placeholderFailures);
  }
  bool refuseMenuInsert()
  {
    return consume(insertFailures);
  }
  void allocatedMenu(MenuHandle menu)
  {
    const MenuAllocation entry = {menu, 0};
    menuAllocations.push_back(entry);
  }
  void disposedMenu(MenuHandle menu)
  {
    for (size_t i = 0; i < installedMenus.size(); ++i)
      LOKA_VERIFY(installedMenus[i] != menu);
    // Search newest first: allocator addresses may be reused after disposal.
    for (size_t i = menuAllocations.size(); i > 0; --i)
      if (menuAllocations[i - 1].handle == menu)
      {
        LOKA_VERIFY(menuAllocations[i - 1].disposals == 0);
        ++menuAllocations[i - 1].disposals;
        ++menuDisposes;
        return;
      }
    LOKA_VERIFY(false);
  }
  void insertedMenu(MenuHandle menu, short mode)
  {
    const MenuInsertion entry = {menu, mode};
    menuInsertions.push_back(entry);
    installedMenus.push_back(menu);
  }
} // namespace toolbox_host
void InitMenus() { ++toolbox_host::menuInitializations; }
#endif

#ifdef _WIN32
#include "../win32/src/context/Win32EditTextBridge.hpp"
#include "../win32/src/Win32ScenePlatformController.hpp"
namespace loka
{
  namespace win32
  {
    namespace testing
    {
      namespace
      {
        TextEditorSetFailure g_textEditorSetFailure = TEXT_EDITOR_SET_REFUSED;
        unsigned g_textEditorSetFailures = 0;
        unsigned g_nativeChildWindowFailures = 0;
      } // namespace

      void failNativeChildWindowCreations(unsigned count)
      {
        g_nativeChildWindowFailures = count;
      }

      bool refuseNativeChildWindowCreation()
      {
        if (!g_nativeChildWindowFailures)
          return false;
        --g_nativeChildWindowFailures;
        return true;
      }

      void failTextEditorSets(TextEditorSetFailure failure, unsigned count)
      {
        g_textEditorSetFailure = failure;
        g_textEditorSetFailures = count;
      }

      bool setTextEditorWide(HWND hwnd, const wchar_t *text)
      {
        if (g_textEditorSetFailures)
        {
          --g_textEditorSetFailures;
          switch (g_textEditorSetFailure)
          {
          case TEXT_EDITOR_SET_REFUSED:
            return false;
          case TEXT_EDITOR_SET_TRUNCATED:
            return SetWindowTextW(hwnd, L"") != FALSE;
          case TEXT_EDITOR_SET_FALSE_AFTER_DELIVERY:
            SetWindowTextW(hwnd, text);
            return false;
          }
        }
        return SetWindowTextW(hwnd, text) != FALSE;
      }
    } // namespace testing
  } // namespace win32
} // namespace loka
#endif

namespace loka
{
  namespace core
  {
    namespace testing
    {
      namespace
      {
        const char *g_failureOwner = 0;
        const char *g_failureType = 0;
        int g_allocationsUntilFailure = 0;
        int g_allocationLive = 0;
        int g_allocationAttempts = 0;

        void *allocateWithFailures(std::size_t size, const LokaAllocationSite &site)
        {
          ++g_allocationAttempts;
          if (g_allocationsUntilFailure > 0 && site.ownerTag && site.typeTag
              && std::strcmp(site.ownerTag, g_failureOwner) == 0 && std::strcmp(site.typeTag, g_failureType) == 0)
          {
            if (--g_allocationsUntilFailure == 0)
              return 0;
          }
          void *memory = new (std::nothrow) char[size];
          if (memory)
            ++g_allocationLive;
          return memory;
        }

        void freeWithFailures(void *memory, const LokaAllocationSite &)
        {
          --g_allocationLive;
          delete[] static_cast<char *>(memory);
        }
      } // namespace

      void failLokaAllocRaw(const char *owner, const char *type, int count)
      {
        g_failureOwner = owner;
        g_failureType = type;
        g_allocationsUntilFailure = count;
        g_allocationAttempts = 0;
        LokaAllocSetBackend(&allocateWithFailures, &freeWithFailures);
      }

      void allowLokaAllocRaw()
      {
        assert(g_allocationLive == 0);
        LokaAllocSetBackend(0, 0);
        g_allocationsUntilFailure = 0;
      }

      int lokaAllocRawLive()
      {
        return g_allocationLive;
      }
      int lokaAllocRawAttempts()
      {
        return g_allocationAttempts;
      }
    } // namespace testing
  } // namespace core

  namespace app
  {
    namespace testing
    {
      namespace
      {
        int g_menuBarDefinitionCloneFailures = 0;
        int g_menuItemDefinitionCloneFailures = 0;
        int g_menuItemDefinitionCloneSkips = 0;
        int g_menuDefinitionCloneFailures = 0;
      }

      void failMenuItemDefinitionClones(int count, int successfulClonesBeforeFailure)
      {
        g_menuItemDefinitionCloneFailures = count;
        g_menuItemDefinitionCloneSkips = successfulClonesBeforeFailure;
      }

      bool shouldCloneMenuItemDefinition()
      {
        if (g_menuItemDefinitionCloneSkips > 0)
        {
          --g_menuItemDefinitionCloneSkips;
          return true;
        }
        if (g_menuItemDefinitionCloneFailures > 0)
        {
          --g_menuItemDefinitionCloneFailures;
          return false;
        }
        return true;
      }

      void failMenuDefinitionClones(int count)
      {
        g_menuDefinitionCloneFailures = count;
      }

      bool shouldCloneMenuDefinition()
      {
        if (g_menuDefinitionCloneFailures > 0)
        {
          --g_menuDefinitionCloneFailures;
          return false;
        }
        return true;
      }

      void failNextMenuBarDefinitionClone()
      {
        g_menuBarDefinitionCloneFailures = 1;
      }

      void failMenuBarDefinitionClones(int count)
      {
        g_menuBarDefinitionCloneFailures = count;
      }

      void allowMenuBarDefinitionClones()
      {
        g_menuBarDefinitionCloneFailures = 0;
      }

      bool shouldCloneMenuBarDefinition()
      {
        if (g_menuBarDefinitionCloneFailures > 0)
        {
          --g_menuBarDefinitionCloneFailures;
          return false;
        }
        return true;
      }
    } // namespace testing
  } // namespace app

  namespace scenario_tests
  {
    namespace testing
    {
      namespace
      {
        int g_observedMainDefinitionCloneFailures = 0;
        int g_scenarioReelDriverAllocationFailures = 0;
      } // namespace

      void failScenarioReelDriverAllocations(int count)
      {
        g_scenarioReelDriverAllocationFailures = count;
      }

      void allowScenarioReelDriverAllocations()
      {
        g_scenarioReelDriverAllocationFailures = 0;
      }

      bool shouldAllocateScenarioReelDriver()
      {
        if (g_scenarioReelDriverAllocationFailures > 0)
        {
          --g_scenarioReelDriverAllocationFailures;
          return false;
        }
        return true;
      }

      void failObservedMainDefinitionClones(int count)
      {
        g_observedMainDefinitionCloneFailures = count;
      }

      void allowObservedMainDefinitionClones()
      {
        g_observedMainDefinitionCloneFailures = 0;
      }

      bool shouldCloneObservedMainDefinition()
      {
        if (g_observedMainDefinitionCloneFailures > 0)
        {
          --g_observedMainDefinitionCloneFailures;
          return false;
        }
        return true;
      }
    } // namespace testing
  } // namespace scenario_tests
} // namespace loka

#endif // TEST_BUILD

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    namespace
    {
      unsigned g_nullVisibleLineFailures = 0;
    }
    void failNullTextEditorVisibleLines(unsigned count)
    {
      g_nullVisibleLineFailures = count;
    }
    bool declineNullTextEditorVisibleLines()
    {
      if (!g_nullVisibleLineFailures)
        return false;
      --g_nullVisibleLineFailures;
      return true;
    }
  } // namespace testing
} // namespace loka
#endif

// Declared in tests/support/LocalRebuildRefusal.hpp.
#ifdef TEST_BUILD
namespace loka { namespace app { namespace testing {
  namespace { unsigned localRebuildProbePropsFailures = 0; }
  void failLocalRebuildProbeProps(unsigned count)
  {
    localRebuildProbePropsFailures = count;
  }
  bool consumeLocalRebuildProbePropsFailure()
  {
    if (!localRebuildProbePropsFailures) return false;
    --localRebuildProbePropsFailures;
    return true;
  }
} } }
#endif

// Declared in tests/support/NullLayoutRefusal.hpp.
#ifdef TEST_BUILD
namespace loka { namespace testing {
  namespace { unsigned nullProjectedLayoutRestoreFailures = 0; }
  void failNullProjectedLayoutRestores(unsigned count)
  { nullProjectedLayoutRestoreFailures = count; }
  bool declineNullProjectedLayoutRestore()
  {
    if (!nullProjectedLayoutRestoreFailures) return false;
    --nullProjectedLayoutRestoreFailures;
    return true;
  }
} }
#endif

#ifdef TEST_BUILD
#include "app/scene/Node.hpp"
namespace loka { namespace testing {
  namespace {
    unsigned nullTextMeasurementFailures = 0;
    app::scene::Node *nullTextMarkDuringRefusal = 0;
  }
  void failNullTextMeasurements(unsigned count, app::scene::Node *markDuringRefusal)
  {
    nullTextMeasurementFailures = count;
    nullTextMarkDuringRefusal = markDuringRefusal;
  }
  bool declineNullTextMeasurement()
  {
    if (!nullTextMeasurementFailures) return false;
    --nullTextMeasurementFailures;
    if (nullTextMarkDuringRefusal)
      nullTextMarkDuringRefusal->requeueLayoutInputs(app::scene::NODE_DIRTY_INITIAL);
    return true;
  }
} }
#endif

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    namespace
    {
      unsigned win32CardStencilFailures = 0;
    }
    void failWin32CardStencils(unsigned count)
    {
      win32CardStencilFailures = count;
    }
    bool declineWin32CardStencil()
    {
      if (!win32CardStencilFailures)
        return false;
      --win32CardStencilFailures;
      return true;
    }
  } // namespace testing
} // namespace loka
#endif

#ifdef TEST_BUILD
namespace loka
{
  namespace testing
  {
    namespace
    {
      unsigned win32CardPaintOrder = 0;
    }
    void resetWin32CardPaintOrder()
    {
      win32CardPaintOrder = 0;
    }
    void noteWin32CardPaintStage(unsigned stage)
    {
      win32CardPaintOrder = win32CardPaintOrder * 10 + stage;
    }
    unsigned readWin32CardPaintOrder()
    {
      return win32CardPaintOrder;
    }
  } // namespace testing
} // namespace loka
#endif
