#include "ToolboxUnicodeTextEditorContext.hpp"
Rect ToolboxUnicodeTextEditorContext::chromeRect() const
{
  Rect rect = {0, 0, 0, 0};
  return rect;
}
void ToolboxUnicodeTextEditorContext::idleTick() {}
void ToolboxUnicodeTextEditorContext::retryProjection() {}
void ToolboxUnicodeTextEditorContext::invalidateNativePresentation() {}
loka::app::EditorResult ToolboxUnicodeTextEditorContext::scrollKey(ToolboxScrollKey)
{
  return loka::app::EDITOR_UNAVAILABLE;
}
loka::app::EditorResult ToolboxUnicodeTextEditorContext::key(char)
{
  return loka::app::EDITOR_UNAVAILABLE;
}
loka::app::EditorResult ToolboxUnicodeTextEditorContext::paste(const char *, std::size_t)
{
  return loka::app::EDITOR_UNAVAILABLE;
}
void ToolboxUnicodeTextEditorContext::repaintNative() {}
loka::app::EditorResult ToolboxUnicodeTextEditorContext::click(const Point &, const EventRecord &)
{
  return loka::app::EDITOR_UNAVAILABLE;
}
bool ToolboxUnicodeTextEditorContext::offerKeyEvent(const EventRecord &)
{
  return false;
}
bool ToolboxUnicodeTextEditorContext::editVerb(loka::app::EditVerb)
{
  return false;
}
bool ToolboxUnicodeTextEditorContext::hasNativeHandle() const
{
  return false;
}
