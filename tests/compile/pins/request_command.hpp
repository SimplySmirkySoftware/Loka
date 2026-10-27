#ifndef LOKA_TEST_COMPILE_REQUEST_COMMAND_HPP
#define LOKA_TEST_COMPILE_REQUEST_COMMAND_HPP
#include "app/scene/node/ComposableNode.hpp"
#include "app/nodes/controls/TextEditorRequests.hpp"
struct TestCommand
{
  int value;
  TestCommand() : value(0) {}
  static TestCommand None() { return TestCommand(); }
  bool isNone() const { return this->value == 0; }
  bool operator!=(const TestCommand &other) const { return this->value != other.value; }
};
struct UntypedCommand : TestCommand
{
  static UntypedCommand None() { return UntypedCommand(); }
};
namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <> struct RequestTraits<TestCommand>
      {
        enum
        {
          coalescable = 0
        };
        typedef loka::app::EditorResult Reason;
        static Reason accepted()
        {
          return loka::app::EDITOR_OK;
        }
        static Reason unavailable()
        {
          return loka::app::EDITOR_UNAVAILABLE;
        }
      };
      /** Queue replies need a reason, but plain slots still lack a coalescing policy. */
      template <> struct RequestTraits<UntypedCommand>
      {
        typedef loka::app::EditorResult Reason;
        static Reason accepted()
        {
          return loka::app::EDITOR_OK;
        }
        static Reason unavailable()
        {
          return loka::app::EDITOR_UNAVAILABLE;
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
