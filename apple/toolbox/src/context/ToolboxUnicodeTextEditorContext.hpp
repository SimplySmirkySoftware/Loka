#ifndef LOKA_TOOLBOX_UNICODE_TEXT_EDITOR_CONTEXT_HPP
#define LOKA_TOOLBOX_UNICODE_TEXT_EDITOR_CONTEXT_HPP
#include "ToolboxEditorSeat.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
/** Unavailable Unicode seat. Production handler selection remains TextEdit. */
class ToolboxUnicodeTextEditorContext : public loka::app::scene::NativeNodeContext, public ToolboxEditorSeat
{
public:
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
  virtual bool hasNativeHandle() const;
  virtual void focus(bool);
};
#endif
