#ifndef LOKA_TOOLBOX_HIT_LEDGER_HPP
#define LOKA_TOOLBOX_HIT_LEDGER_HPP

#include "core/State.hpp"
#include "ToolboxPressRows.hpp"
#include "core/Vector.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include <Quickdraw.h>
#include <vector>

class ToolboxScenePlatformController;
class ToolboxButtonContext;
class ToolboxPopupMenuContext;
class ToolboxCellContext;
class ToolboxTextContext;
class ToolboxEditTextContext;

/** Owns the five hit registries projected by the Toolbox controller. */
class ToolboxHitLedger
{
public:
  ToolboxHitLedger();
  ~ToolboxHitLedger();
  void retirePressRows(const loka::app::scene::NodeContext *, ToolboxPressLedger &, ToolboxReleasedStates &);
  void clearPressRows();
  struct ButtonHit
  {
    Rect rect;
    loka::core::EmitterState *emitter;
    loka::core::State<bool> *enabled;
    loka::app::scene::BoundaryNode *boundary;
    ToolboxButtonContext *context;
    static void release(const ButtonHit &hit, ToolboxReleasedStates &released)
    {
      released.enableds.push_back(hit.enabled);
    }
  };

  struct CellHit
  {
    Rect rect;
    loka::core::EmitterState *emitter;
    loka::app::scene::BoundaryNode *boundary;
    ToolboxCellContext *context;
    loka::core::State<loka::core::String> *text;
    static void release(const CellHit &hit, ToolboxReleasedStates &released)
    {
      released.texts.push_back(hit.text);
    }
  };

  struct EditHit
  {
    ToolboxEditTextContext *context;
    Rect rect;
    loka::core::State<loka::core::String> *text;
    loka::app::scene::BoundaryNode *boundary;
    static void release(const EditHit &hit, ToolboxReleasedStates &released)
    {
      released.texts.push_back(hit.text);
    }
  };

  struct TextHit
  {
    ToolboxTextContext *context;
    Rect rect;
    short x;
    short y;
    loka::core::State<loka::core::String> *text;
    loka::app::scene::BoundaryNode *boundary;
    short lastMeasuredWidth;
    bool needsRelayoutOnChange;
  };

  struct PopupHit
  {
    Rect rect;
    loka::core::State<bool> *enabled;
    ToolboxPopupMenuContext *context;
    static void release(const PopupHit &hit, ToolboxReleasedStates &released)
    {
      released.enableds.push_back(hit.enabled);
    }
  };

private:
  friend class ToolboxScenePlatformController;

  ToolboxHitLedger(const ToolboxHitLedger &);
  ToolboxHitLedger &operator=(const ToolboxHitLedger &);
  ToolboxPressStoreChain chain_;
  ToolboxPressRows<ButtonHit, PressRow::BUTTON_HIT> buttonHits_;
  ToolboxPressRows<CellHit, PressRow::CELL_HIT> cellHits_;
  ToolboxPressRows<PopupHit, PressRow::POPUP_HIT> popupHits_;
  ToolboxPressRows<EditHit, PressRow::EDIT_HIT> editHits_;
  std::vector<TextHit> textHits_;
};

#endif // LOKA_TOOLBOX_HIT_LEDGER_HPP
