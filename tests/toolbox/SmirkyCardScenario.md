# SmirkyCard LaptopTracker scenarios

Status: Null audit expectations; maciix runtime verification and golden approval pending.
Owns: LaptopTracker scenario selection and Toolbox capture handoff.
Does not own: Production card behavior or the standalone full browse sequence.
Code truth: `example/SmirkyCard/src/ToolboxScenarioAppConfig.cpp`,
`example/SmirkyCard/LAPTOP.FLOW.JS`, and `tests/toolbox/run-scenario.sh`.
Verification: `checkLaptopScenario`, `ScenarioToolsTest.py`, and
`ToolboxScenarioRunnerTest.sh`; MAME remains the native presentation verdict.

| Cell | Settled presentation |
| --- | --- |
| startup | Default Tall list, all five pictures loaded |
| laptop-carousel | Wide carousel, Macintosh Portable, 1 of 5 |
| laptop-detail | PowerBook 100 detail pane |

The vehicle reads `LokaTest.cfg`, prefixes the baked companion with `LAPTOP_CELL`,
and writes the JS runner's audit under the selected cell name. After native
presentation settles it publishes the capture rectangle and completion marker,
then lingers for the configured duration. The production frame and title remain
420×340 at (60, 60), LaptopTracker. Five PICTs come from `disk-pictures.txt`.

Without `LAPTOP_CELL`, the same companion runs the complete standalone browse
sequence pinned by `example/SmirkyCard/tests/LAPTOP.audit`.
