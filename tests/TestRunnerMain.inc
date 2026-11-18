#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#ifdef _MSC_VER
#include <stdlib.h>
#endif
#include "core/diag/LifecycleAudit.hpp"
#include "core/LokaAlloc.hpp"

namespace
{
#ifdef _MSC_VER
  void lokaConfigureNonInteractiveWin32TestProcess()
  {
    // The console runner already sends assert diagnostics to stderr. Disable
    // only abort's follow-up message and Windows Error Reporting UI.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  }
#endif

  typedef void (*LokaTestFunction)();

  struct LokaRegisteredTest
  {
    const char *name;
    LokaTestFunction function;
  };

#if defined(LOKA_TEST_RUNNER_CONTRACT) || defined(LOKA_TEST_RUNNER_COMBINED)
#define LOKA_TEST(testFunction) {#testFunction, &testFunction},
#define LOKA_SCENARIO_TESTS_HERE() {0, 0},
  const LokaRegisteredTest lokaContractTests[] = {
#include "ContractTestRegistry.inc"
  };
#undef LOKA_SCENARIO_TESTS_HERE
#undef LOKA_TEST
#endif

#if defined(LOKA_TEST_RUNNER_SCENARIO) || defined(LOKA_TEST_RUNNER_COMBINED)
#define LOKA_TEST(testFunction) {#testFunction, &testFunction},
  const LokaRegisteredTest lokaScenarioTests[] = {
#include "ScenarioTestRegistry.inc"
  };
#undef LOKA_TEST
#endif

  bool lokaSkipExpectedRed(const LokaRegisteredTest &test)
  {
    const char *enabled = std::getenv("LOKA_EXPECTED_RED");
    return std::strncmp(test.name, "testExpectedRed", 15) == 0 &&
        (!enabled || std::strcmp(enabled, "1") != 0);
  }

  void lokaRunTest(const LokaRegisteredTest &test)
  {
    if (lokaSkipExpectedRed(test))
    {
      std::printf("SKIP expected-red: %s (set LOKA_EXPECTED_RED=1)\n", test.name);
      return;
    }
    test.function();
    LOKA_AUDIT_CHECKPOINT(test.name);
  }

#if defined(LOKA_TEST_RUNNER_SCENARIO) || defined(LOKA_TEST_RUNNER_COMBINED)
  void lokaRunAllScenarioTests()
  {
    const std::size_t count = sizeof(lokaScenarioTests) / sizeof(lokaScenarioTests[0]);
    for (std::size_t i = 0; i < count; ++i)
    {
      lokaRunTest(lokaScenarioTests[i]);
    }
  }

  void lokaListAllScenarioTests()
  {
    const std::size_t count = sizeof(lokaScenarioTests) / sizeof(lokaScenarioTests[0]);
    for (std::size_t i = 0; i < count; ++i)
    {
      std::printf("%s\n", lokaScenarioTests[i].name);
    }
  }

  const LokaRegisteredTest *lokaFindScenarioTest(const char *name)
  {
    const std::size_t count = sizeof(lokaScenarioTests) / sizeof(lokaScenarioTests[0]);
    for (std::size_t i = 0; i < count; ++i)
    {
      if (std::strcmp(name, lokaScenarioTests[i].name) == 0)
      {
        return &lokaScenarioTests[i];
      }
    }
    return 0;
  }
#endif

  void lokaRunAllTests()
  {
#if defined(LOKA_TEST_RUNNER_CONTRACT) || defined(LOKA_TEST_RUNNER_COMBINED)
    const std::size_t contractCount = sizeof(lokaContractTests) / sizeof(lokaContractTests[0]);
    for (std::size_t i = 0; i < contractCount; ++i)
    {
      if (lokaContractTests[i].function)
      {
        lokaRunTest(lokaContractTests[i]);
      }
#if defined(LOKA_TEST_RUNNER_COMBINED)
      else
      {
        lokaRunAllScenarioTests();
      }
#endif
    }
#elif defined(LOKA_TEST_RUNNER_SCENARIO)
    lokaRunAllScenarioTests();
#endif
    LOKA_AUDIT_CHECKPOINT(LOKA_TEST_RUNNER_FINAL_CHECKPOINT);
  }

  void lokaListAllTests()
  {
#if defined(LOKA_TEST_RUNNER_CONTRACT) || defined(LOKA_TEST_RUNNER_COMBINED)
    const std::size_t contractCount = sizeof(lokaContractTests) / sizeof(lokaContractTests[0]);
    for (std::size_t i = 0; i < contractCount; ++i)
    {
      if (lokaContractTests[i].function)
      {
        std::printf("%s\n", lokaContractTests[i].name);
      }
#if defined(LOKA_TEST_RUNNER_COMBINED)
      else
      {
        lokaListAllScenarioTests();
      }
#endif
    }
#elif defined(LOKA_TEST_RUNNER_SCENARIO)
    lokaListAllScenarioTests();
#endif
  }

  const LokaRegisteredTest *lokaFindTest(const char *name)
  {
#if defined(LOKA_TEST_RUNNER_CONTRACT) || defined(LOKA_TEST_RUNNER_COMBINED)
    const std::size_t contractCount = sizeof(lokaContractTests) / sizeof(lokaContractTests[0]);
    for (std::size_t i = 0; i < contractCount; ++i)
    {
      if (lokaContractTests[i].function && std::strcmp(name, lokaContractTests[i].name) == 0)
      {
        return &lokaContractTests[i];
      }
    }
#endif
#if defined(LOKA_TEST_RUNNER_SCENARIO) || defined(LOKA_TEST_RUNNER_COMBINED)
    return lokaFindScenarioTest(name);
#else
    return 0;
#endif
  }
} // namespace

int main(int argc, char **argv)
{
#ifdef _MSC_VER
  lokaConfigureNonInteractiveWin32TestProcess();
#endif

#if defined(_MSC_VER) && !defined(NDEBUG)
  if (argc == 2 && std::strcmp(argv[1], "--win32-assert-probe") == 0)
  {
    assert(false && "intentional Win32 assert probe");
    return 0;
  }
#endif

#ifdef LOKA_LIFECYCLE_AUDIT
  // Absolute zero is a startup invariant; tests use their own allocation deltas.
  loka::core::LokaAllocAuditCheckpoint("test runner startup");
#endif

#ifdef LOKA_TEST_RUNNER_CONTRACT
  if (argc == 4 && std::strcmp(argv[1], "--document-markdown-cases") == 0)
    return documentMarkdownCases(static_cast<unsigned>(std::strtoul(argv[2], 0, 10)),
                                 static_cast<unsigned>(std::strtoul(argv[3], 0, 10)));
#endif
  if (argc == 1)
  {
    lokaRunAllTests();
    return 0;
  }

  if (argc == 2 && std::strcmp(argv[1], "--list") == 0)
  {
    lokaListAllTests();
    return 0;
  }

  if (argc == 2)
  {
    const LokaRegisteredTest *test = lokaFindTest(argv[1]);
    if (test)
    {
      const bool skipped = lokaSkipExpectedRed(*test);
      lokaRunTest(*test);
      return skipped ? 77 : 0;
    }
    std::fprintf(stderr, "Unknown test: %s\n", argv[1]);
    return 1;
  }

  std::fprintf(stderr, "Usage: %s [--list|test-name]\n", argv[0]);
  return 1;
}
