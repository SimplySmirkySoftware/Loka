#include "ToolboxHitLedger.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "context/ToolboxButtonContext.hpp"
#include "context/ToolboxCellContext.hpp"
#include "context/ToolboxTextContext.hpp"
#include "context/ToolboxEditTextContext.hpp"
#include "context/ToolboxPopupMenuContext.hpp"

void ToolboxScenePlatformController::recordButtonHit(const Rect &rect,
                                                     loka::core::EmitterState *emitter,
                                                     loka::core::State<bool> *enabled,
                                                     loka::app::scene::BoundaryNode *boundary,
                                                     ToolboxButtonContext *context)
{
  Rect clipped;
  if (!this->intersectWithProjectionClip(rect, clipped))
  {
    return;
  }
  ButtonHit hit;
  hit.rect = clipped;
  hit.emitter = emitter;
  hit.enabled = enabled;
  hit.boundary = boundary;
  hit.context = context;
  this->hitLedger_.buttonHits_.record(hit, this->pressLedger_);
  bindEnabledState(enabled);
}

void ToolboxScenePlatformController::recordCellHit(const Rect &rect,
                                                   loka::core::EmitterState *emitter,
                                                   loka::app::scene::BoundaryNode *boundary,
                                                   ToolboxCellContext *context,
                                                   loka::core::State<loka::core::String> *text)
{
  Rect clipped;
  if (!this->intersectWithProjectionClip(rect, clipped))
  {
    return;
  }
  CellHit hit;
  hit.rect = clipped;
  hit.emitter = emitter;
  hit.boundary = boundary;
  hit.context = context;
  hit.text = text;
  this->hitLedger_.cellHits_.record(hit, this->pressLedger_);
  bindTextState(context->liveTextState());
}

void ToolboxScenePlatformController::recordTextHit(const Rect &rect,
                                                   short x,
                                                   short y,
                                                   loka::core::State<loka::core::String> *text,
                                                   loka::app::scene::BoundaryNode *boundary,
                                                   bool needsRelayoutOnChange,
                                                   short visibleWidth,
                                                   ToolboxTextContext *context)
{
  Rect clipped;
  if (!text || !this->intersectWithProjectionClip(rect, clipped))
  {
    return;
  }
  TextHit hit;
  hit.context = context;
  hit.rect = clipped;
  hit.x = x;
  hit.y = y;
  hit.text = text;
  hit.boundary = boundary;
  hit.lastMeasuredWidth = visibleWidth;
  hit.needsRelayoutOnChange = needsRelayoutOnChange;
  hitLedger_.textHits_.push_back(hit);
  bindTextState(context->liveTextState());
}

void ToolboxScenePlatformController::recordPopupHit(const Rect &rect,
                                                    loka::core::State<bool> *enabled,
                                                    ToolboxPopupMenuContext *context)
{
  Rect clipped;
  if (!this->intersectWithProjectionClip(rect, clipped))
  {
    return;
  }
  PopupHit hit;
  hit.rect = clipped;
  hit.enabled = enabled;
  hit.context = context;
  this->hitLedger_.popupHits_.store(hit);
  bindEnabledState(enabled);
}
