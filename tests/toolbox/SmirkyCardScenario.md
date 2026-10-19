# SmirkyCard LaptopTracker scenarios

Status: Null audit expectations; maciix runtime verification and golden approval pending.
Owns: LaptopTracker scenario selection and Toolbox capture handoff.
Does not own: Production card behavior or the standalone full browse sequence.
Code truth: `tests/toolbox/src/SmirkyCardScenarioDriver.cpp` (built by `tests/toolbox/smirkycard/CMakeLists.txt`),
`example/SmirkyCard/LAPTOP.FLOW.JS`, and `tests/toolbox/run-scenario.sh`.
Verification: `checkLaptopScenario`, `ScenarioToolsTest.py`, and
`ToolboxScenarioRunnerTest.sh`; MAME remains the native presentation verdict.

| Cell | Settled presentation |
| --- | --- |
| startup | Default Tall list, all 13 pictures loaded (the one-bit pair on the 1-bit rig) |
| laptop-carousel | Wide carousel, Macintosh Plus, 1 of 13 |
| laptop-detail | PowerBook 180c detail pane |

The vehicle is built only when the Retro68 build is configured with
`-DLOKA_BUILD_SMIRKYCARD=ON` (SmirkyCard is opt-in; CI builds it in its own
SmirkyCard build directory, outside the shipping 68K size inventory), and
`run-scenario.sh` says so when the application is missing.

The vehicle reads `LokaTest.cfg`, prefixes the baked companion with `LAPTOP_CELL`,
and writes the JS runner's audit under the selected cell name. After native
presentation settles it publishes the capture rectangle and completion marker,
then lingers for the configured duration. The production frame and title remain
420×340 at (60, 60), LaptopTracker. The 39 PICTs (13 machines × color, 320×200
one-bit and 160×100 one-bit) come from `disk-pictures.txt` and live in `Pictures/`; the dev disk then
holds three top-level items (application, LokaTest.cfg, Pictures), so the
runner Tabs a full cycle (3) to the application and
gives these cells a 120 s settle window (the carousel settles at about 34 s).

Without `LAPTOP_CELL`, the same companion runs the complete standalone browse
sequence pinned by `example/SmirkyCard/tests/LAPTOP.audit`.
