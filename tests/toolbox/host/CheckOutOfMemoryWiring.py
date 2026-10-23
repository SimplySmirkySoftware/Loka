"""Structural pin for initial-mount, turn-end and allocation wiring; the host does not run ToolboxApp::run.

The real event-loop/bootstrap fixture is absent. Runtime path acceptance remains
on MAME; this check only detects removal/movement of the fatal calls.
"""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
app = (root / "apple/toolbox/src/ToolboxApp.cpp").read_text()
checks = [
    re.search(r"InitDialogs\(0\);\s*#if LOKA_TOOLBOX_UNICODE\s*loka::toolbox::RequireUnicodeSystem\(\);\s*#endif\s*loka::toolbox::ArmOutOfMemoryReserve\(\);", app),
    re.search(r"bool ToolboxApp::windowAdopted\(Window \*window\)\s*\{[\s\S]*?"
              r"toolboxWindow->setApp\(this\);\s*toolboxWindow->open\(\);\s*"
              r"toolboxWindow->ensureSceneMounted\(\);\s*"
              r"if \(toolboxWindow->scene\(\) && toolboxWindow->scene\(\)->composeRefusedForMemory\(\)\)\s*"
              r"return false;\s*(?://[^\n]*\n\s*)*"
              r"loka::toolbox::QuitIfOutOfMemoryReserveSpent\(\);\s*return true;\s*\}", app),
    re.search(r"void ToolboxApp::bootstrapWindowRefused\(Window \*\)\s*"
              r"\{\s*loka::toolbox::QuitForOutOfMemory\(\);\s*\}", app),
    re.search(r"void ToolboxApp::bootstrapWindowless\(\)\s*"
              r"\{\s*loka::toolbox::QuitForOutOfMemory\(\);\s*\}", app),
]
# Refusal must reach the fatal rail policy inline before initial projection.
core = (root / "common/app/core/App.cpp").read_text()
checks.append(re.search(r"if \(window && !this->windowAdopted\(window\)\)\s*"
                        r"this->bootstrapWindowRefused\(window\);\s*\}\s*"
                        r"(?://[^\n]*\n\s*)*this->admitDocumentWindows\(ADOPT_AT_LAUNCH\);\s*"
                        r"if \(this->windowlessAfterLaunch\(\)\)\s*"
                        r"this->bootstrapWindowless\(\);\s*\}\s*"
                        r"projectInitialVisibilityChunks\(\);\s*this->projectMenuSources\(\);", core))
# Match the complete loop by braces, then require the check at its tail.
loop_start = app.index("while (running_)")
body_start = app.index("{", loop_start)
depth = 1
body_end = body_start + 1
while depth:
    depth += (app[body_end] == "{") - (app[body_end] == "}")
    body_end += 1
loop = app[body_start + 1:body_end - 1]
checks.append(re.search(r"this->present\([^;]+;[\s\S]*"
                        r"loka::toolbox::QuitIfOutOfMemoryReserveSpent\(\);\s*$", loop))
checks.append(loop.count("QuitIfOutOfMemoryReserveSpent();") == 1)
source = (root / "apple/toolbox/src/ToolboxMemorySource.hpp").read_text()
checks.append(re.search(r"RefusingAllocationScope \w+;\s*return NewPtr\(size\);", source))
checks.append(re.search(r"RefusingAllocationScope \w+;\s*Ptr original = NewPtr\(", source))
oom = (root / "apple/toolbox/src/ToolboxOutOfMemory.cpp").read_text()
checks.append(re.search(r"InstallReserveGrowZone\(NewGrowZoneUPP\(ReleaseReserveForSystem\)\);", oom))
operators = (root / "apple/toolbox/src/ToolboxOperatorNew.cpp").read_text()
for signature in [r"void \*operator new\(std::size_t size\)",
                  r"void __throw_bad_alloc\(\)",
                  r"void __throw_bad_array_new_length\(\)"]:
    checks.append(re.search(signature + r"\s*\{[^}]*loka::toolbox::QuitForOutOfMemory\(\);", operators))
if not all(checks):
    sys.exit("OOM initial-mount/reserve/operator/turn-end/refusing-allocation wiring missing")
print("OOM structural wiring checks passed (not a ToolboxApp runtime test)")
