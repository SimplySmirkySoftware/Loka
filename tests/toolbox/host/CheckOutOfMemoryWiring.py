"""Structural pin for initial-mount, turn-end and allocation wiring; the host does not run ToolboxApp::run.

The iteration has a host trace fixture; bootstrap runtime acceptance remains
on MAME; this check only detects removal/movement of the fatal calls.
"""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
app = (root / "apple/toolbox/src/ToolboxApp.cpp").read_text()
checks = [
    re.search(r"InitDialogs\(0\);\s*#if LOKA_TOOLBOX_UNICODE\s*loka::toolbox::RequireUnicodeSystem\(\);\s*#endif\s*loka::toolbox::UnicodeText::Session \w+;\s*loka::toolbox::ArmOutOfMemoryReserve\(\);", app),
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
def body_at(source, marker):
    start = source.index("{", source.index(marker))
    depth, end = 1, start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start + 1:end - 1], end

loop, body_end = body_at(app, "while (running_)")
checks.append(re.search(r"\{\s*loka::core::Operation::Regime \w+;\s*while \(running_\)", app))
checks.append(re.match(r"\s*\}\s*(?://[^\n]*\n\s*)*this->retireComponents\(\);", app[body_end:]))
checks.append(re.search(r"WaitNextEvent\([^;]+;\s*this->iterate\(clock, gotEvent, record\);\s*$", loop))
iteration = (root / "apple/toolbox/src/ToolboxIteration.cpp").read_text()
loop, _ = body_at(iteration, "void ToolboxApp::iterate")
checks.append(re.search(r"this->present\([^;]+;[\s\S]*"
                        r"loka::toolbox::QuitIfOutOfMemoryReserveSpent\(\);\s*$", loop))
checks.append(loop.count("QuitIfOutOfMemoryReserveSpent();") == 1)
source = (root / "apple/toolbox/src/ToolboxMemorySource.hpp").read_text()
checks.append(re.search(r"RefusingAllocationScope \w+;\s*return NewPtr\(size\);", source))
checks.append(re.search(r"RefusingAllocationScope \w+;\s*Ptr original = NewPtr\(", source))
oom = (root / "apple/toolbox/src/ToolboxOutOfMemory.cpp").read_text()
checks.append(re.search(r"InstallReserveGrowZone\(NewGrowZoneUPP\(ReleaseReserveForSystem\)\);", oom))
checks.append(re.search(r"void QuitForOutOfMemory\(\)\s*\{[^}]*"
                        r"SuddenTerminate\(\);\s*ExitToShell\(\);", oom))
operators = (root / "apple/toolbox/src/ToolboxOperatorNew.cpp").read_text()
for signature in [r"void \*operator new\(std::size_t size\)",
                  r"void __throw_bad_alloc\(\)",
                  r"void __throw_bad_array_new_length\(\)"]:
    checks.append(re.search(signature + r"\s*\{[^}]*loka::toolbox::QuitForOutOfMemory\(\);", operators))
if not all(checks):
    sys.exit("OOM initial-mount/reserve/operator/turn-end/refusing-allocation wiring missing")
print("OOM structural wiring checks passed (not a ToolboxApp runtime test)")
