#ifndef LOKA_TOOLBOX_PAINT_REFUSAL_HPP
#define LOKA_TOOLBOX_PAINT_REFUSAL_HPP

/** Screen obligations only: an intentional omission is not a refusal.
    New refusing context kinds must extend the refusal-site inventory and pins.
    This is a chosen twin of LokaAllocationSite: one location, not census tags.
    Unlike PROFILE_FUNC it has no registry or timing. The pointer is unconditional
    so differing core/application diagnostic settings cannot change the layout;
    only the call-site literal is gated. */
enum ToolboxPaintRefusal
{
  /** No refused obligation. */
  TOOLBOX_PAINT_REFUSAL_NONE,
  /** An attempted clip, guard, scratch or native-door allocation failed. */
  TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION,
  /** A native control, TE or handle needed by the screen was not acquired. */
  TOOLBOX_PAINT_REFUSAL_NATIVE_ACQUISITION,
  /** Layout-time measurement refused. */
  TOOLBOX_PAINT_REFUSAL_MEASUREMENT,
  /** Drawing started but did not finish. */
  TOOLBOX_PAINT_REFUSAL_PARTIAL_DRAW,
  /** Blur fallback painted because capture was disabled or ineligible. */
  TOOLBOX_PAINT_REFUSAL_CAPTURE_REFUSED,
  /** Text-to-native conversion failed. */
  TOOLBOX_PAINT_REFUSAL_TEXT_CONVERSION,
  /** A required node, model, context or seat was missing, or failure is unexplained. */
  TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET,
  TOOLBOX_PAINT_REFUSAL_COUNT
};

/** Only diagnostic readers construct text. */
inline const char *ToolboxPaintRefusalName(ToolboxPaintRefusal reason)
{
  switch (reason)
  {
  case TOOLBOX_PAINT_REFUSAL_NONE:
    return "none";
  case TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION:
    return "clip_allocation";
  case TOOLBOX_PAINT_REFUSAL_NATIVE_ACQUISITION:
    return "native_acquisition";
  case TOOLBOX_PAINT_REFUSAL_MEASUREMENT:
    return "measurement";
  case TOOLBOX_PAINT_REFUSAL_PARTIAL_DRAW:
    return "partial_draw";
  case TOOLBOX_PAINT_REFUSAL_CAPTURE_REFUSED:
    return "capture_refused";
  case TOOLBOX_PAINT_REFUSAL_TEXT_CONVERSION:
    return "text_conversion";
  case TOOLBOX_PAINT_REFUSAL_OBLIGATION_UNMET:
    return "obligation_unmet";
  case TOOLBOX_PAINT_REFUSAL_COUNT:
    break;
  }
  return "unknown";
}

/** One atomic reason/location value with an explicit none. */
struct ToolboxPaintRefusalRecord
{
  ToolboxPaintRefusal reason;
  const char *site;
  ToolboxPaintRefusalRecord(ToolboxPaintRefusal value = TOOLBOX_PAINT_REFUSAL_NONE, const char *where = 0)
      : reason(value),
        site(where)
  {
  }
  bool refused() const
  {
    return this->reason != TOOLBOX_PAINT_REFUSAL_NONE;
  }
};

enum ToolboxPaintEligibility
{
  TOOLBOX_PAINT_REFUSED,
  TOOLBOX_PAINT_CLEAN
};

/** Only diagnostic readers construct text. */
inline const char *ToolboxPaintEligibilityName(ToolboxPaintEligibility value)
{
  switch (value)
  {
  case TOOLBOX_PAINT_REFUSED:
    return "refused";
  case TOOLBOX_PAINT_CLEAN:
    return "clean";
  }
  return "unknown";
}

/** Borrowed reporting adapter: direct painting without a controller has no
    observation. One null guard for every leaf; O(1), no rows or retained owner. */
template <class Controller> class ToolboxPaintRefusalTarget
{
public:
  explicit ToolboxPaintRefusalTarget(Controller *controller)
      : controller_(controller)
  {
  }
  void notePaintRefusal(ToolboxPaintRefusal reason, const char *site) const
  {
    if (this->controller_)
      this->controller_->notePaintRefusal(reason, site);
  }

private:
  Controller *const controller_;
};
template <class Controller> ToolboxPaintRefusalTarget<Controller> ToolboxPaintRefusals(Controller *controller)
{
  return ToolboxPaintRefusalTarget<Controller>(controller);
}

/** Editor setters during render are uncertified until the native raster pin;
    successful input outside render is exempt. No retained state. */
template <class Controller> ToolboxPaintRefusalTarget<Controller> ToolboxRenderPaintRefusals(Controller *controller)
{
  return ToolboxPaintRefusalTarget<Controller>(controller && controller->paintInProgress() ? controller : 0);
}

#define LOKA_PAINT_REFUSAL_STRINGIZE_I(value) #value
#define LOKA_PAINT_REFUSAL_STRINGIZE(value) LOKA_PAINT_REFUSAL_STRINGIZE_I(value)
#if defined(LOKA_DIAG) || defined(LOKA_RETRO68_DIAGNOSTICS)
#define LOKA_PAINT_REFUSE(observationExpr, reason)                                                                     \
  (observationExpr).notePaintRefusal((reason), __FILE__ ":" LOKA_PAINT_REFUSAL_STRINGIZE(__LINE__))
#else
#define LOKA_PAINT_REFUSE(observationExpr, reason) (observationExpr).notePaintRefusal((reason), 0)
#endif
#endif
