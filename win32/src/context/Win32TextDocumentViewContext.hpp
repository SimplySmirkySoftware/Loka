#ifndef LOKA_WIN32_TEXT_DOCUMENT_VIEW_CONTEXT_HPP
#define LOKA_WIN32_TEXT_DOCUMENT_VIEW_CONTEXT_HPP
#include <windows.h>
#include "app/text/TextDocumentViewContext.hpp"
#include "app/nodes/controls/TextDocumentEditor.hpp"
namespace loka
{
  namespace app
  {
    namespace scene
    {
      class PlatformNodeHandlerRegistry;
    }
  } // namespace app
} // namespace loka
class Win32ScenePlatformController;
/** Borrows a projection parent; the claimed store owns the EDIT and its body. */
class Win32TextDocumentViewContext : public loka::app::scene::TextDocumentViewContext
{
public:
  Win32TextDocumentViewContext(Win32ScenePlatformController &,
                               loka::app::scene::SceneTextDocuments &,
                               loka::app::TextDocumentEditorNode &,
                               HWND);
  HWND viewWindow() const
  {
    return this->viewWindow_;
  }
  /** Claim-derived target, including during replacement preparation. */
  HWND hwnd() const;
  /** Typed recognition only; two replacement HWNDs may carry the same mark. */
  static Win32TextDocumentViewContext *fromWindow(HWND);
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &);
  void relayout(const loka::app::scene::LayoutState &);
  virtual loka::app::scene::PaintAnswer queryPaintDamage(const loka::app::scene::PaintQuery &) const;

private:
  const HWND viewWindow_;
};
void RegisterWin32TextDocumentViewNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &);
#endif
