#!/usr/bin/env python3
"""Mutate each window declaration refusal transition and verify its contract pin."""

from window_mutations import main

WINDOW = "common/app/core/Window.cpp"
DEFINITION = "common/app/core/WindowDefinition.hpp"
VEHICLE = "tests/standalone/StandaloneScenarioSupport.cpp"
BLOCKED = "definition.create(&context) == 0"
VERDICT = """      const bool awaitingVerdict =
          this->borrowedApp_ && !this->completed_
          && (this->completionMode_ != REARM_COMPLETED_SCENE || this->phase_ == AWAITING_WINDOW);
      return this->phase_ == FAILED || awaitingVerdict;"""

MUTATIONS = [
    ("copy-drops-refused", WINDOW,
     "source.refused() || (source.owned() && !this->owned())", "source.owned() && !this->owned()",
     "testWindowRootCaptureRefusal", BLOCKED),
    ("copy-loses-root", WINDOW,
     "source.refused() || (source.owned() && !this->owned())", "source.refused()",
     "testWindowRootCopyRefusal", BLOCKED),
    ("assignment-keeps-empty", WINDOW,
     "if (!this->root_.tryAssign(rhs.root_))\n  {\n    this->refuseContentWrite();",
     "if (!this->root_.tryAssign(rhs.root_))\n  {",
     "testWindowRootAssignmentRefusal", BLOCKED),
    ("assignment-reclones", WINDOW, "this->definition_.reset(candidate.take());",
     "this->definition_.reset(candidate.owned() ? candidate.definition_->clone() : 0);",
     "testWindowRootSuccessfulCopies", "g_limitedCloneCalls == 1"),
    ("capture-keeps-empty", WINDOW, "if (!candidate.isSet())\n  {\n    this->refuseContentWrite();",
     "if (!candidate.isSet())\n  {", "testWindowRootCaptureRefusal", BLOCKED),
    ("accept-null-root-scene", WINDOW, "if (scene->getRootDefinition())", "if (scene)",
     "testWindowSceneHandoffRefusal", "!props.peekInitialScene()"),
    ("release-before-allocation", WINDOW, "      candidate = loka::core::LokaNew<InitialSceneHandoff>(handoffSite(), scene);",
     "    {\n      this->releaseInitialSceneHandoff();\n      candidate = loka::core::LokaNew<InitialSceneHandoff>(handoffSite(), scene);\n    }",
     "testWindowSceneHandoffRefusal", "target.peekInitialScene() == pending"),
    ("clone-ignores-refusal", DEFINITION, "copy.isSet() && !copy->props.rootDefinitionRefused()",
     "copy.isSet()", "testWindowRootCloneRefusal", "!copy.isSet()"),
    ("create-ignores-refusal", DEFINITION, "    if (resolved.rootDefinitionRefused())\n      return 0;\n",
     "", "testWindowRootResolutionRefusal", BLOCKED),
    ("no-verdict-succeeds", VEHICLE, VERDICT, "      return this->phase_ == FAILED;",
     "testScrapbookStandaloneDeclarationRefusalIsWindowless", "config.exitCode() != 0"),
]


if __name__ == "__main__":
    main(MUTATIONS, "window-root-refusal-mutations", __doc__)
