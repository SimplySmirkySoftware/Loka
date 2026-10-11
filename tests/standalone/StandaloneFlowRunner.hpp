#ifndef LOKA_TESTS_STANDALONE_FLOW_RUNNER_HPP
#define LOKA_TESTS_STANDALONE_FLOW_RUNNER_HPP

#include <cassert>

#include "StandalonePerformanceConfig.hpp"
#include "StandaloneScenarioSupport.hpp"
#include "app/PlatformContext.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/core/App.hpp"
#include "core/Profiler.hpp"
#include "core/util/ScopedPtr.hpp"

#if LOKA_STANDALONE_PERFORMANCE_RUNS > 0
#include "StandalonePerformance.hpp"
#endif

namespace loka
{
  namespace standalone_tests
  {
    namespace standalone_flow_runner_detail
    {
      template <class Config> int RunPass(PlatformContext *platformContext, NativeModuleHandle hInstance, int nCmdShow)
      {
#if defined(LOKA_RETRO68)
        // Classic keeps its existing refusal path until the generic alert component (#1237).
        Config config(platformContext);
#else
        const platform::file::FileHandle auditFile = ResolveStandaloneAuditFile();
        const char *refusal = StandaloneAuditRefusal(auditFile);
        if (refusal)
        {
          ShowStandaloneAuditRefusal(refusal);
          return 1;
        }
        Config config(platformContext, &auditFile);
#endif
        if (config.exitCode() != 0)
        {
          return config.exitCode();
        }
        core::ScopedPtr<App> app(platformContext->createApp(&config, hInstance, nCmdShow));
        assert(app.get() && "App is required");
        if (!app.get())
        {
          return 1;
        }
        config.setApp(app.get());
        app->run();
        return config.exitCode();
      }
    } // namespace standalone_flow_runner_detail

    /** Runs one ordinary Standalone Flow pass, or a bounded measured set when
        LOKA_STANDALONE_PERFORMANCE_RUNS is enabled for the target. */
    template <class Config> int RunStandaloneFlowWithConfig(NativeModuleHandle hInstance, int nCmdShow)
    {
      platform::InitPlatformRuntime();
      core::ScopedPtr<PlatformContext> platformContext(platform::CreatePlatformContext());
      assert(platformContext.get() && "PlatformContext is required");
      if (!platformContext.get())
      {
        return 1;
      }

#if LOKA_STANDALONE_PERFORMANCE_RUNS > 0
      StandalonePerformanceSession performance(LOKA_STANDALONE_PERFORMANCE_RUNS);
      platform::file::FileHandle reportFile;
      if (!performance.isValid() || !core::gProfilerBackend || !ResolveStandalonePerformanceReport(reportFile)
          || !PrepareStandalonePerformanceReport(reportFile))
      {
        return 1;
      }
      while (!performance.isComplete())
      {
        const long startTicks = core::ProfileTicks();
        const int runResult =
            standalone_flow_runner_detail::RunPass<Config>(platformContext.get(), hInstance, nCmdShow);
        const long endTicks = core::ProfileTicks();
        if (runResult != 0)
        {
          return runResult;
        }
        if (!performance.recordRun(startTicks, endTicks))
        {
          return 1;
        }
      }
      return WriteStandalonePerformanceReport(reportFile, performance) ? 0 : 1;
#endif
#if LOKA_STANDALONE_PERFORMANCE_RUNS == 0
      return standalone_flow_runner_detail::RunPass<Config>(platformContext.get(), hInstance, nCmdShow);
#endif
    }
  } // namespace standalone_tests
} // namespace loka

#endif // LOKA_TESTS_STANDALONE_FLOW_RUNNER_HPP
