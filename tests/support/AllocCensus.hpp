#ifndef LOKA_TESTS_SUPPORT_ALLOC_CENSUS_HPP
#define LOKA_TESTS_SUPPORT_ALLOC_CENSUS_HPP

#include <cstddef>

// Allocation census used by the zero-allocation pin (LokaAllocPinTests).
//
// AllocPinMain.cpp replaces the global operator new/new[]/delete/delete[]
// for this binary only and records, while a capture window is open, every
// shared-heap allocation together with its backtrace and interaction phase.
// AllocPinTests.cpp drives a headless HelloWorld scene through warmup and
// two captured steady-state interactions, then asserts the pin.
//
// Linux-only test scaffolding: never built for Retro68 targets.

namespace allocpin
{
  // Phases of one UI interaction. PHASE_OUTSIDE covers everything outside
  // an explicitly phased region (should stay empty while a capture is open).
  enum Phase
  {
    PHASE_OUTSIDE = 0,
    PHASE_HANDLER = 1, // event handler body: concats + state sets
    PHASE_COMMIT = 2,  // tracker end(): propagation / notify / invalidate
    PHASE_FLUSH = 3,   // scene.flushInvalidation(): compose + apply
    PHASE_COUNT = 4
  };

  void SetPhase(int phase);

  // Two capture windows: index 0 (first captured interaction, may contain
  // warmup stragglers) and index 1 (second captured interaction = the
  // steady-state census and the pinned quantity).
  void BeginCapture(int captureIndex);
  void EndCapture();

  unsigned long CaptureAllocCount(int captureIndex);
  unsigned long CaptureAllocBytes(int captureIndex);

  // Prints the per-stack census for both captures plus the cross-capture
  // steady-state/warmup-residual classification.
  void PrintCensusReport();

  // Scenario entry point (AllocPinTests.cpp).
  void RunStateLifetimeTokenAllocPin();
  void RunLazyViewPageFlipAllocPin();
  void RunAttributedStringEqualsAllocPin();
  void RunZeroAllocPin();
  void RunDocumentMarkdownAllocPin();
  void RunFocusCompletionAllocPin();
  void RunFloppyBirdSurfaceAllocPin();
  void RunFloppyBirdScoreAllocPin();
} // namespace allocpin

#endif // LOKA_TESTS_SUPPORT_ALLOC_CENSUS_HPP
