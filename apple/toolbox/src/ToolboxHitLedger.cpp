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
  if (!emitter || !this->intersectWithProjectionClip(rect, clipped))
  {
    return;
  }
  ButtonHit hit;
  hit.rect = clipped;
  hit.emitter = emitter;
  hit.enabled = enabled;
  hit.boundary = boundary;
  hit.context = context;
  hitLedger_.buttonHits_.push_back(hit);
  this->pressLedger_.append(PressRow::BUTTON_HIT, this->hitLedger_.buttonHits_.size() - 1);
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
  hitLedger_.cellHits_.push_back(hit);
  this->pressLedger_.append(PressRow::CELL_HIT, this->hitLedger_.cellHits_.size() - 1);
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
  hitLedger_.popupHits_.push_back(hit);
  bindEnabledState(enabled);
}
