#ifndef LOKA_APP_NODES_CONTROLS_TEXT_EDITOR_REQUESTS_HPP
#define LOKA_APP_NODES_CONTROLS_TEXT_EDITOR_REQUESTS_HPP
#include "app/scene/state/Request.hpp"
#include "app/nodes/controls/TextEditorDocument.hpp"
namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <> struct RequestTraits<LineCursor>
      {
        enum
        {
          coalescable = 1
        };
        typedef EditorResult Reason;
        static Reason accepted()
        {
          return EDITOR_OK;
        }
        static Reason unavailable()
        {
          return EDITOR_UNAVAILABLE;
        }
      };
      template <> struct RequestTraits<EditorCommand>
      {
        enum
        {
          coalescable = 0
        };
        typedef EditorResult Reason;
        static Reason accepted()
        {
          return EDITOR_OK;
        }
        static Reason unavailable()
        {
          return EDITOR_UNAVAILABLE;
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
