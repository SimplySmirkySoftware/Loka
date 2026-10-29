#ifndef LOKA_TOOLBOX_PRESENT_OBSERVATION_HPP
#define LOKA_TOOLBOX_PRESENT_OBSERVATION_HPP

#include "ToolboxFullDrawReason.hpp"
#include <Quickdraw.h>
#include <string>
#include <cassert>

/** Observation never authorizes copying pixels. */
enum ToolboxPresentVerdict
{
  TOOLBOX_PRESENT_ERASE_ALL,
  TOOLBOX_PRESENT_VERDICT_COUNT
};

/** First refusal, in admission order. Suppression capability is deliberately absent. */
enum ToolboxPresentRefusal
{
  TOOLBOX_PRESENT_NO_VIEWPORT_MOVED,
  TOOLBOX_PRESENT_SEVERAL_VIEWPORTS_MOVED,
  TOOLBOX_PRESENT_HORIZONTAL_MOVE,
  TOOLBOX_PRESENT_REASONS_BEYOND_LAYOUT,
  TOOLBOX_PRESENT_CONTENT_INPUTS,
  TOOLBOX_PRESENT_REQUESTS_DURING_WALK,
  TOOLBOX_PRESENT_UNSUPPORTED_KIND,
  TOOLBOX_PRESENT_DITHER_PHASE,
  TOOLBOX_PRESENT_DAMAGE_OUTSIDE_VIEWPORT,
  TOOLBOX_PRESENT_FOREGROUND_UNITS,
  TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY,
  TOOLBOX_PRESENT_REFUSAL_COUNT
};

inline const char *ToolboxPresentVerdictName(ToolboxPresentVerdict verdict)
{
  switch (verdict)
  {
  case TOOLBOX_PRESENT_ERASE_ALL:
    return "erase";
  case TOOLBOX_PRESENT_VERDICT_COUNT:
    break;
  }
  return "unknown";
}

inline const char *ToolboxPresentRefusalName(ToolboxPresentRefusal refusal)
{
  switch (refusal)
  {
  case TOOLBOX_PRESENT_NO_VIEWPORT_MOVED:
    return "no_viewport_moved";
  case TOOLBOX_PRESENT_SEVERAL_VIEWPORTS_MOVED:
    return "several_viewports_moved";
  case TOOLBOX_PRESENT_HORIZONTAL_MOVE:
    return "horizontal_move";
  case TOOLBOX_PRESENT_REASONS_BEYOND_LAYOUT:
    return "reasons_beyond_layout";
  case TOOLBOX_PRESENT_CONTENT_INPUTS:
    return "content_inputs";
  case TOOLBOX_PRESENT_REQUESTS_DURING_WALK:
    return "requests_during_walk";
  case TOOLBOX_PRESENT_UNSUPPORTED_KIND:
    return "unsupported_kind";
  case TOOLBOX_PRESENT_DITHER_PHASE:
    return "dither_phase";
  case TOOLBOX_PRESENT_DAMAGE_OUTSIDE_VIEWPORT:
    return "damage_outside_viewport";
  case TOOLBOX_PRESENT_FOREGROUND_UNITS:
    return "foreground_units";
  case TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY:
    return "no_suppression_capability";
  case TOOLBOX_PRESENT_REFUSAL_COUNT:
    break;
  }
  return "unknown";
}

/** Counts visited nodes only; band windows may skip rows. Pre-layout inputs
    contribute only after successful projected layout. Diagnostics, verdict erase:
    zero counts are not a content-invariance proof (window-anchored raster phase,
    late publication, clipping are separate gates). */
class ToolboxViewportContentObservation
{
public:
  ToolboxViewportContentObservation()
      : contentInputs_(0),
        unsupportedKinds_(0),
        phaseBound_(0)
  {
  }
  /** Dispatch calls once per successful dirty leaf visit; O(1), walks nothing. */
  void noteContentInputs()
  {
    ++this->contentInputs_;
  }
  /** Dispatch calls once per refused-kind visit; O(1), walks nothing. */
  void noteUnsupportedKind()
  {
    ++this->unsupportedKinds_;
  }
  /** Dispatch calls once per successful CardBand visit; O(1), walks nothing.
      Its 8x8 shadow phase is window-anchored: context/ToolboxCardBandContext.cpp
      and common/app/style/CardShadow.cpp (CardShadowStencilInk). */
  void notePhaseBound()
  {
    ++this->phaseBound_;
  }
  /** Completed window-anchored raster visits; O(1), walks nothing. */
  unsigned phaseBound() const
  {
    return this->phaseBound_;
  }
  /** Completed visit count; O(1), walks nothing. */
  unsigned contentInputs() const
  {
    return this->contentInputs_;
  }
  /** Completed visit count; O(1), walks nothing. */
  unsigned unsupportedKinds() const
  {
    return this->unsupportedKinds_;
  }

  /** Borrows the layoutScrollView local only during child layout. O(1), no walk. */
  class Scope
  {
  public:
    Scope(ToolboxViewportContentObservation *&active, ToolboxViewportContentObservation &value)
        : active_(active)
    {
      assert(!this->active_ && "nested viewports refuse before child layout");
      this->active_ = &value;
    }
    ~Scope()
    {
      this->active_ = 0;
    }

  private:
    Scope(const Scope &);
    Scope &operator=(const Scope &);
    ToolboxViewportContentObservation *&active_;
  };

private:
  unsigned contentInputs_, unsupportedKinds_, phaseBound_;
};

/** Controller-owned interval facts; completed copies contain no borrowed identities. */
class ToolboxPresentObservation
{
public:
  ToolboxPresentObservation()
  {
    this->reset();
  }

  /** Reasons noted between passes (before render) belong to the interval that render
      consumes; a request arriving during paint belongs to the next one (after the reset).
      Every render exit resets through this scope, including refused layout entries. */
  class Interval
  {
  public:
    explicit Interval(ToolboxPresentObservation &observation)
        : observation_(observation)
    {
      this->observation_.phase_ = WALKING;
    }
    ~Interval()
    {
      const unsigned next = this->observation_.nextReasons_;
      this->observation_.reset();
      this->observation_.reasons_ = next;
    }

  private:
    Interval(const Interval &);
    Interval &operator=(const Interval &);
    ToolboxPresentObservation &observation_;
  };

  /** O(1) per full request, walks nothing; completed intervals defer new causes.
      Viewport publication can change an already-laid-out leaf through a binding;
      requests during WALKING are this pass's trace of that late paint input. */
  void noteReason(ToolboxFullDrawReason reason)
  {
    if (this->phase_ == COMPLETED)
      this->nextReasons_ |= 1U << reason;
    else if (this->phase_ == WALKING)
      this->walkReasons_ |= 1U << reason;
    else
      this->reasons_ |= 1U << reason;
  }
  /** O(1) at either window draw entry, before BeginUpdate or validation. */
  void noteDamage(const Rect &damage)
  {
    this->damage_ = damage;
  }
  /** layoutScrollView calls once per successful move; O(1) merge, walks nothing. */
  void noteViewportMove(const Rect &contentRect, int dx, int dy, const ToolboxViewportContentObservation &content)
  {
    if (dx == 0 && dy == 0)
      return;
    if (this->viewports_ == 0)
    {
      this->viewport_ = contentRect;
      this->dy_ = dy;
    }
    this->contentInputs_ += content.contentInputs();
    this->unsupportedKinds_ += content.unsupportedKinds();
    this->phaseBound_ += content.phaseBound();
    ++this->viewports_;
    this->horizontal_ = this->horizontal_ || dx != 0;
  }
  /** O(1) after full layout; the controller supplies its ledger overlap count. */
  void complete(unsigned foregroundUnits)
  {
    this->foregroundUnits_ = foregroundUnits;
    this->phase_ = COMPLETED;
    this->verdict_ = TOOLBOX_PRESENT_ERASE_ALL;
    if (!this->viewports_)
      this->refusal_ = TOOLBOX_PRESENT_NO_VIEWPORT_MOVED;
    else if (this->viewports_ > 1)
      this->refusal_ = TOOLBOX_PRESENT_SEVERAL_VIEWPORTS_MOVED;
    else if (this->horizontal_)
      this->refusal_ = TOOLBOX_PRESENT_HORIZONTAL_MOVE;
    else if (this->reasons_ != (1U << TOOLBOX_FULL_DRAW_LAYOUT_DIRTY))
      this->refusal_ = TOOLBOX_PRESENT_REASONS_BEYOND_LAYOUT;
    else if (this->contentInputs_)
      this->refusal_ = TOOLBOX_PRESENT_CONTENT_INPUTS;
    else if (this->walkReasons_)
      this->refusal_ = TOOLBOX_PRESENT_REQUESTS_DURING_WALK;
    else if (this->unsupportedKinds_)
      this->refusal_ = TOOLBOX_PRESENT_UNSUPPORTED_KIND;
    else if (this->phaseBound_ > 0 && (this->dy_ & 7) != 0)
      this->refusal_ = TOOLBOX_PRESENT_DITHER_PHASE;
    else if (this->damage_.left < this->damage_.right && this->damage_.top < this->damage_.bottom
             && (this->damage_.left < this->viewport_.left || this->damage_.top < this->viewport_.top
                 || this->damage_.right > this->viewport_.right || this->damage_.bottom > this->viewport_.bottom))
      this->refusal_ = TOOLBOX_PRESENT_DAMAGE_OUTSIDE_VIEWPORT;
    else if (this->foregroundUnits_)
      this->refusal_ = TOOLBOX_PRESENT_FOREGROUND_UNITS;
    else
      this->refusal_ = TOOLBOX_PRESENT_NO_SUPPRESSION_CAPABILITY;
  }
  /** O(1) at interval exit or explicit diagnostic reset; no owned resources. */
  void reset()
  {
    this->reasons_ = this->nextReasons_ = this->walkReasons_ = 0;
    this->contentInputs_ = this->unsupportedKinds_ = this->phaseBound_ = 0;
    this->damage_ = this->viewport_ = Rect();
    this->dy_ = 0;
    this->viewports_ = this->foregroundUnits_ = 0;
    this->horizontal_ = false;
    this->phase_ = COLLECTING;
    this->verdict_ = TOOLBOX_PRESENT_ERASE_ALL;
    this->refusal_ = TOOLBOX_PRESENT_NO_VIEWPORT_MOVED;
  }
  /** Merged successful moved-viewport visits; O(1), no walk. */
  unsigned contentInputs() const
  {
    return this->contentInputs_;
  }
  /** Merged refused-kind visits; O(1), no walk. */
  unsigned unsupportedKinds() const
  {
    return this->unsupportedKinds_;
  }
  /** Merged window-anchored raster visits; O(1), no walk. */
  unsigned phaseBound() const
  {
    return this->phaseBound_;
  }
  /** Requests received during layout; O(1), no walk. */
  unsigned walkReasons() const
  {
    return this->walkReasons_;
  }
  unsigned reasons() const
  {
    return this->reasons_;
  }
  const Rect &damage() const
  {
    return this->damage_;
  }
  const Rect &viewport() const
  {
    return this->viewport_;
  }
  int dy() const
  {
    return this->dy_;
  }
  unsigned viewports() const
  {
    return this->viewports_;
  }
  unsigned foregroundUnits() const
  {
    return this->foregroundUnits_;
  }
  ToolboxPresentVerdict verdict() const
  {
    return this->verdict_;
  }
  ToolboxPresentRefusal refusal() const
  {
    return this->refusal_;
  }
  /** Only diagnostic readers construct text; collecting and completing allocate nothing. */
  std::string reasonNames() const
  {
    return reasonNames(this->reasons_);
  }
  /** Diagnostic formatting only; scans the fixed reason enum, no owner rows. */
  std::string walkReasonNames() const
  {
    return reasonNames(this->walkReasons_);
  }

private:
  static std::string reasonNames(unsigned reasons)
  {
    std::string result;
    for (int i = 0; i < TOOLBOX_FULL_DRAW_REASON_COUNT; ++i)
      if (reasons & (1U << i))
      {
        if (!result.empty())
          result += '+';
        result += ToolboxFullDrawReasonName(static_cast<ToolboxFullDrawReason>(i));
      }
    return result.empty() ? "none" : result;
  }

private:
  enum Phase
  {
    COLLECTING,
    WALKING,
    COMPLETED
  };
  unsigned reasons_, nextReasons_, walkReasons_;
  unsigned contentInputs_, unsupportedKinds_, phaseBound_;
  Rect damage_, viewport_;
  int dy_;
  unsigned viewports_, foregroundUnits_;
  bool horizontal_;
  Phase phase_;
  ToolboxPresentVerdict verdict_;
  ToolboxPresentRefusal refusal_;
};
#endif
