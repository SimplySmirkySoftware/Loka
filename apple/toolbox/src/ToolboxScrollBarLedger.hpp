#ifndef LOKA_TOOLBOX_SCROLL_BAR_LEDGER_HPP
#define LOKA_TOOLBOX_SCROLL_BAR_LEDGER_HPP

#include "app/scene/projection/NativeHandlePool.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "core/State.hpp"
#include "core/Managed.hpp"
#include "app/layout/StackSpans.hpp"
#include "app/scene/state/WriteSeat.hpp"
#include <Quickdraw.h>
#include <Controls.h>
#include <cstddef>
#include <vector>

class ToolboxScenePlatformController;

namespace loka
{
  namespace app
  {
    class ScrollViewNode;
  }
}

/** Owns the Toolbox scrollBarProc bindings and their exact-match handle pool. */
class ToolboxScrollBarLedger
{
public:
  /** One live scrollBarProc control. The props are copied out by value
      rather than kept as a node pointer: the binding outlives nothing, but
      a value copy removes the question entirely, the way PopupHit does. */
  struct ScrollBarControlBinding
  {
    short resourceId;
    ControlRef control;
    loka::core::State<int> *value;
    loka::app::scene::WriteSeat<int> valueSeat;
    loka::core::EmitterState *onChange;
    loka::core::State<bool> *enabled;
    int minimum;
    int maximum;
    int lineStep;
    int pageStep;
    /** The value this side last pushed into the CDEF. A tracking loop that
        ends back here changed nothing, so no settle is published. */
    int appliedValue;
    bool active;
    bool usedThisFrame;
    Rect rect;
    loka::app::scene::NativeLifetimeHint lifetimeHint;
  };

  /** One viewport binding with two bar ids, indexed by ScrollBarOrientation.
      Its ScrollView pointer is the fact-write door;
      lifecycle teardown removes the binding before the node can be reclaimed. */
  struct ViewportScrollBarBinding
  {
    short barId[2];
    loka::app::ScrollViewNode *scrollView;
    loka::core::Managed<loka::app::layout::StackSpans> spans;
    bool usedThisFrame;
    Rect rect;
    Rect contentRect;
  };

  explicit ToolboxScrollBarLedger(std::size_t bucketDepth)
      : scrollBarControls_(),
        viewportScrollBars_(),
        scrollBarBucket_(bucketDepth)
  {
  }

private:
  friend class ToolboxScenePlatformController;

  std::vector<ScrollBarControlBinding> scrollBarControls_;
  std::vector<ViewportScrollBarBinding> viewportScrollBars_;
  loka::app::scene::ExactMatchHandleBucket<ControlRef> scrollBarBucket_;
};

#endif // LOKA_TOOLBOX_SCROLL_BAR_LEDGER_HPP
