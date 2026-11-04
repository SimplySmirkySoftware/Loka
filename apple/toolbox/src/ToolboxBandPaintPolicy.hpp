#ifndef LOKA_TOOLBOX_BAND_PAINT_POLICY_HPP
#define LOKA_TOOLBOX_BAND_PAINT_POLICY_HPP
#include "ToolboxBlurSnapshot.hpp"

/** Selected LIVE band's completed ground decision for one synchronous render. */
enum ToolboxBandGround
{
  BAND_BLUR,
  BAND_WINDOW_GROUND,
  BAND_WINDOW_GROUND_OVER_PICTURE
};

/** Owned by render after extent publication; window/device stay stable through paint. */
class ToolboxBandPaintPolicy
{
public:
  /** Render calls once; O(1), queries only the selected row, no ledger walk. */
  static ToolboxBandPaintPolicy resolve(GrafPtr window,
                                        const ToolboxUnitLedger::FirstBlurUnits &selection,
                                        const ToolboxBlurSnapshot<TOOLBOX_BLUR_LIVE> &band)
  {
    if (!selection.band)
      return ToolboxBandPaintPolicy(false, BAND_BLUR);
    const bool eligible = ToolboxBlurPixels::eligible(window, selection.band->rect);
    const bool holds = band.holdsPicture(*selection.band);
    return ToolboxBandPaintPolicy(true,
                                  eligible ? BAND_BLUR
                                  : holds  ? BAND_WINDOW_GROUND_OVER_PICTURE
                                           : BAND_WINDOW_GROUND);
  }
  /** Paint/classification query per row; O(1), no walk; absent leaves out untouched. */
  bool querySelectedGround(ToolboxBandGround &out) const
  {
    if (!this->selected_)
      return false;
    out = this->ground_;
    return true;
  }

private:
  ToolboxBandPaintPolicy(bool selected, ToolboxBandGround ground)
      : selected_(selected),
        ground_(ground)
  {
  }
  bool selected_;
  ToolboxBandGround ground_;
};
#endif
