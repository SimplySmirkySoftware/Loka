#ifndef LOKA_TOOLBOX_UNICODE_TEXT_EDITOR_CONTEXT_HPP
#define LOKA_TOOLBOX_UNICODE_TEXT_EDITOR_CONTEXT_HPP
#include "ToolboxEditorSeat.hpp"
#include "context/ToolboxProjectedNodeContext.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "app/scene/state/RequestSettlement.hpp"
struct ToolboxUnicodeEditorResource;
/** Read-only staging seat. The controller ledger owns its borrowed presentation. */
class ToolboxUnicodeTextEditorContext : public ToolboxProjectedNodeContext, public ToolboxEditorSeat
{
public:
  ToolboxUnicodeTextEditorContext(loka::app::TextEditorNode *,
                                  ToolboxScenePlatformController *,
                                  const loka::app::scene::SeamKey<loka::app::TextEditorNode> &);
  virtual ~ToolboxUnicodeTextEditorContext();
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &);
  virtual void render(loka::app::scene::IPlatformController *);
  virtual void onPropsApplied();
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact);
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &) const;
  virtual Rect chromeRect() const;
  virtual void idleTick();
  virtual void retryProjection();
  virtual void invalidateNativePresentation();
  virtual loka::app::EditorResult scrollKey(ToolboxScrollKey);
  virtual loka::app::EditorResult key(char);
  virtual loka::app::EditorResult paste(const char *, std::size_t);
  virtual void repaintNative();
  virtual loka::app::EditorResult click(const Point &, const EventRecord &);
  virtual bool offerKeyEvent(const EventRecord &);
  virtual bool editVerb(loka::app::EditVerb);
  virtual bool hasNativeHandle() const
  {
    return this->unicode_ != 0;
  }

private:
  class RailOperation;
  class CommandOperation;
  void settle(loka::app::scene::Settlement, RailOperation &);
  loka::app::EditorResult beginInput();
  loka::app::EditorResult refuseInput();
  void updateRect(const Rect &);
  bool queryVisibleLines(unsigned &) const
  {
    return false;
  }
  const loka::app::scene::SeamKey<loka::app::TextEditorNode> key_;
  loka::app::TextEditorNode *node_;
  ToolboxUnicodeEditorResource *unicode_;
  Rect rect_, paintRect_;
};
bool RegisterToolboxUnicodeTextEditorNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &);
#endif
