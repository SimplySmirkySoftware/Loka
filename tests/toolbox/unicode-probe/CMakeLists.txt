# Probe only (branch probe/mlte-crlf): observes MLTE line separators, Kotoeri
# commit under bottom-line, and heap peaks on a real Mac OS 9 machine.
# Built locally against the Universal Interfaces header; never merged.
add_application(LokaMlteProbePPC main.cpp Size.r)
loka_toolbox_unicode(LokaMlteProbePPC)
loka_enable_warnings(LokaMlteProbePPC)
target_compile_definitions(LokaMlteProbePPC PRIVATE LOKA_RETRO68=1)
set_target_properties(LokaMlteProbePPC PROPERTIES CXX_STANDARD 98 CXX_STANDARD_REQUIRED YES)
target_compile_options(LokaMlteProbePPC PRIVATE -Os -fno-exceptions -fno-rtti)
