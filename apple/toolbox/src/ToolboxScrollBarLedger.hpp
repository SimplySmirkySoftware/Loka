#ifndef LOKA_TOOLBOX_SCROLL_BAR_LEDGER_HPP
#define LOKA_TOOLBOX_SCROLL_BAR_LEDGER_HPP

#include "app/scene/projection/NativeHandlePool.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "core/State.hpp"
#include "core/Frame.hpp"
#include "core/Managed.hpp"
#include "app/layout/StackSpans.hpp"
#include "app/scene/state/WriteSeat.hpp"
#include "ToolboxEditorScrollBar.hpp"
#include "ToolboxNativePlacement.hpp"
#include "ToolboxViewportCertificate.hpp"
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
  /** One live scrollBarProc control. Props are copied for dispatch; the DSL
      owner is borrowed until synchronous row erasure on context retirement. */
  struct ScrollBarControlBinding
  {
    ToolboxEditorScrollBarAssociation editor;
    /** DSL bars only; editor and viewport bars use their existing associations. */
    loka::app::scene::NodeContext *ownerContext;
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
    ToolboxNativePlacement placement;
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
    /** Content origin placed by the last successful layout; layout alone compares and replaces it. */
    loka::core::Frame placedOrigin;
    ToolboxOptionalRect copyDomain;
    ToolboxViewportCertificate presented;
  };

  explicit ToolboxScrollBarLedger(std::size_t bucketDepth)
      : scrollBarControls_(),
        viewportScrollBars_(),
        scrollBarBucket_(bucketDepth)
  {
  }

  /** Interval calls once per render attempt, O(V) over this ledger's own rows.
      This is the only writer of presented certificates. */
  void settlePresented(ToolboxPaintEligibility eligibility)
  {
    for (std::size_t i = 0; i < this->viewportScrollBars_.size(); ++i)
    {
      ViewportScrollBarBinding &row = this->viewportScrollBars_[i];
      Rect domain;
      row.presented = eligibility == TOOLBOX_PAINT_CLEAN && row.usedThisFrame && row.copyDomain.query(domain)
                          ? ToolboxViewportCertificate(row.placedOrigin, row.contentRect, domain)
                          : ToolboxViewportCertificate();
    }
  }

private:
  friend class ToolboxScenePlatformController;

  std::vector<ScrollBarControlBinding> scrollBarControls_;
  std::vector<ViewportScrollBarBinding> viewportScrollBars_;
  loka::app::scene::ExactMatchHandleBucket<ControlRef> scrollBarBucket_;
};

#endif // LOKA_TOOLBOX_SCROLL_BAR_LEDGER_HPP
