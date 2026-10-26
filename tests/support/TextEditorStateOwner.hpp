#ifndef LOKA_TEST_TEXT_EDITOR_STATE_OWNER_HPP
#define LOKA_TEST_TEXT_EDITOR_STATE_OWNER_HPP
#include "app/nodes/controls/TextEditorRequests.hpp"
#include "support/Headless.hpp"
#include "app/nodes/controls/TextEditor.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      /** Fixture storage uses the allocation/adoption path of state declarations. */
      // Derive from the owner; do not hold it as a member. With the owner as a
      // by-value member this class is non-polymorphic, and GCC 13.3 at -O3
      // (testing-release) proves the second declaration's virtual call has no
      // target, replaces it with __builtin_unreachable, and the constructor
      // spins or corrupts the heap. The C++ is valid (UBSan and clang -O3 pass);
      // see #877 for the dump that shows it. The same symptom elsewhere: a
      // release-only hang or crash that -fno-devirtualize removes; grep the
      // TU's -fdump-ipa-inline-details output for "No devirtualization target".
      class TextEditorStateOwner : private scene::HeadlessStateOwner
      {
      public:
        core::PushStateTracker &tracker;
        scene::Reported<LineCursor> cursor;
        scene::RequestWithReply<LineCursor> request;
        scene::Request<LineCursor> otherRequest;
        scene::RequestQueue<LineCursor, 4> queue;
        scene::RequestQueue<EditorCommand, 4> commands;
        TextEditorStateOwner()
            : scene::HeadlessStateOwner(),
              tracker(*scene::HeadlessStateOwner::tracker()->asPushTracker()),
              cursor(),
              request()
        {
          scene::StateBatchBase::CreateImmediateState(this, this->cursor, LineCursor::None());
          scene::StateBatchBase::CreateImmediateState(this, this->request, LineCursor::None());
          scene::StateBatchBase::CreateImmediateState(this, this->otherRequest, LineCursor::None());
          scene::StateBatchBase::CreateImmediateState(this, this->queue, LineCursor::None());
          scene::StateBatchBase::CreateImmediateState(this, this->commands, EditorCommand::None());
        }
      };
    } // namespace testing
  } // namespace app
} // namespace loka
#endif
