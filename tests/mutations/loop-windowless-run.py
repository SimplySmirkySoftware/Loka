#!/usr/bin/env python3
"""Verify the standalone loop window factory and launch verdict transitions."""

from window_mutations import main

CONTROL = "tests/standalone/StandaloneScenarioSupport.cpp"
CLOSE = "testScrapbookStandaloneLoopCloseBeforeFirstIdleSucceeds"
REFUSED = "testStandaloneLoopDeclarationRefusalIsWindowless"
LATE = "testLateWindowCreationPreservesDeadlineFailure"

MUTATIONS = [
    ("slice-clone", "tests/standalone/StandaloneScenarioSupport.hpp",
     "return cloneWindowDefinition(*this);", "return WindowDefinition<WindowProps>::clone();",
     CLOSE, "exitAfterClose == 0"),
    ("skip-report", CONTROL, "      if (window)\n        this->runControl_.windowCreated();", "",
     CLOSE, "exitAfterClose == 0"),
    ("report-null", CONTROL, "      if (window)\n        this->runControl_.windowCreated();", "      this->runControl_.windowCreated();",
     "testStandaloneLoopFactoryRefusalIsWindowless", "config.exitCode() != 0"),
    ("ignore-windowless", CONTROL, " || this->phase_ == AWAITING_WINDOW", "",
     REFUSED, "config.exitCode() != 0"),
    ("require-idle", CONTROL, " || this->phase_ == AWAITING_WINDOW", " || this->tick_ == 0",
     CLOSE, "exitAfterClose == 0"),
    ("revive-failed", CONTROL, "      if (this->phase_ == AWAITING_WINDOW)\n", "",
     LATE, "control.failed()"),
    ("reset-on-bind", CONTROL, "this->borrowedApp_ = app;", "this->borrowedApp_ = app;\n      this->phase_ = AWAITING_WINDOW;",
     "testStandaloneLoopCreatedBeforeMountSucceeds", "!control.failed()"),
    ("reset-on-rearm", CONTROL, "this->tick_ = 0;", "this->tick_ = 0;\n        this->phase_ = AWAITING_WINDOW;",
     "testStandaloneRunControlRearmsCompletedSceneWithoutQuittingApp", "!completed.failed()"),
]

for vehicle in ("HelloWorld", "MineSweeper", "Scrapbook", "Tutorial", "FloppyBird"):
    MUTATIONS.append((
        f"plain-window-{vehicle}", f"tests/standalone/{vehicle}StandaloneFlowAppConfig.cpp",
        "composition << StandaloneWindowDefinition(", "composition << WindowDef(",
        f"test{vehicle}StandaloneLoopCloseBeforeFirstIdleSucceeds", "exitAfterClose == 0",
        (",\n          this->runControl_);", ");"),
    ))


if __name__ == "__main__":
    main(MUTATIONS, "loop-windowless-mutations", __doc__)
