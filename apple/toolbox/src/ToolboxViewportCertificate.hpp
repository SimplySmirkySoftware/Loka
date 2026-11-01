#ifndef LOKA_TOOLBOX_VIEWPORT_CERTIFICATE_HPP
#define LOKA_TOOLBOX_VIEWPORT_CERTIFICATE_HPP
#include "ToolboxPresentObservation.hpp"

/** Layout's copy domain: absent port evidence differs from an empty intersection. */
class ToolboxOptionalRect
{
public:
  ToolboxOptionalRect()
      : rect_(),
        present_(false)
  {
  }
  explicit ToolboxOptionalRect(const Rect &rect)
      : rect_(rect),
        present_(true)
  {
  }
  bool query(Rect &out) const
  {
    if (!this->present_)
      return false;
    out = this->rect_;
    return true;
  }

private:
  Rect rect_;
  bool present_;
};

/** The configuration a CLEAN render showed, or no presented baseline.
    Only the ledger's settlement replaces this value. */
class ToolboxViewportCertificate
{
public:
  ToolboxViewportCertificate()
      : origin_(),
        contentRect_(),
        copyDomain_(),
        present_(false)
  {
  }
  ToolboxViewportCertificate(const loka::core::Frame &origin, const Rect &contentRect, const Rect &copyDomain)
      : origin_(origin),
        contentRect_(contentRect),
        copyDomain_(copyDomain),
        present_(true)
  {
  }
  /** Layout evaluates once per moving viewport, O(1), no rows. */
  ToolboxBaselineStatus
  evaluate(const loka::core::Frame &oldOrigin, const Rect &contentRect, const ToolboxOptionalRect &copyDomain) const
  {
    if (!this->present_)
      return TOOLBOX_BASELINE_NONE;
    Rect domain;
    return this->origin_ == oldOrigin && EqualRect(&this->contentRect_, &contentRect) && copyDomain.query(domain)
                   && EqualRect(&this->copyDomain_, &domain)
               ? TOOLBOX_BASELINE_VALID
               : TOOLBOX_BASELINE_STALE;
  }

private:
  loka::core::Frame origin_;
  Rect contentRect_, copyDomain_;
  bool present_;
};
#endif
