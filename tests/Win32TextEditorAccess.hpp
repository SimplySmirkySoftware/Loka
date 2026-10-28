#ifndef LOKA_TEST_WIN32_TEXT_EDITOR_ACCESS_HPP
#define LOKA_TEST_WIN32_TEXT_EDITOR_ACCESS_HPP
#include "context/Win32TextEditorContext.hpp"
namespace loka
{
  namespace testing
  {
    class Win32TextEditorAccess
    {
    public:
      static app::TextEditorNode *node(const Win32TextEditorContext &context)
      {
        return context.node_;
      }
      static const app::scene::SeamKey<app::TextEditorNode> &key(const Win32TextEditorContext &context)
      {
        return context.key_;
      }
      static const std::string &projectedText(const Win32TextEditorContext &context)
      {
        return context.projection_.text;
      }
      static const std::wstring &projectedWide(const Win32TextEditorContext &context)
      {
        return context.projection_.wide;
      }
      static unsigned restores(const Win32TextEditorContext &context)
      {
        return context.restores_;
      }
      static app::EditorResult status(const Win32TextEditorContext &context)
      {
        return context.status_;
      }
      static void status(Win32TextEditorContext &context, app::EditorResult value)
      {
        context.status_ = value;
      }
      static WNDPROC nativeProcedure(const Win32TextEditorContext &context)
      {
        return context.previousProc_;
      }
      // Instrument native input below the existing rail entry, not a new production door.
      static WNDPROC replaceNativeProcedure(Win32TextEditorContext &context, WNDPROC procedure)
      {
        WNDPROC old = context.previousProc_;
        context.previousProc_ = procedure;
        return old;
      }
      static bool pending(const Win32TextEditorContext &context)
      {
        return context.phase_ == Win32TextEditorContext::RETRY;
      }
    };
  } // namespace testing
} // namespace loka
#endif
