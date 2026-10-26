#include "app/nodes/controls/TextEditorRequests.hpp"
#include <cstdio>
#include <Processes.h>
#include "ToolboxProbeTiming.hpp"
#include "app/bootstrap/PlatformBootstrap.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "app/scene/boundary/BoundaryInnerStateOwner.hpp"
#include "app/scene/state/StateBatchBase.hpp"
#include "core/io/File.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  const unsigned kIterations = 100;

  /** Fixed-cost policies isolate the production runner from native editing and
      TEST_BUILD tracing. Failed-arm is synthetic: Toolbox itself has no arm. */
  template <class Request> class ProbeSeat : public SeatOperation<Request, LineCursor>
  {
  public:
    ProbeSeat(const RequestBinding<Request> &binding, bool defer)
        : binding_(binding),
          defer_(defer),
          applications_(0)
    {
    }
    virtual Admission admit(Node &, RequestBinding<Request> &binding)
    {
      binding = this->binding_;
      if (this->defer_)
        return ADMISSION_DEFERRED;
      return binding.state()->get().isNone() ? ADMISSION_EMPTY : ADMISSION_TAKE;
    }
    virtual bool current(Node &, const RequestBinding<Request> &binding)
    {
      return this->binding_.same(binding);
    }
    virtual EditorResult resolve(Node &, const RequestBinding<Request> &)
    {
      return EDITOR_OK;
    }
    virtual EditorResult validate(Node &, const Request &)
    {
      return EDITOR_OK;
    }
    virtual RequestApplication<LineCursor, EditorResult> apply(Node &, const Request &)
    {
      ++this->applications_;
      return RequestApplication<LineCursor, EditorResult>(LineCursor(loka::core::ItemId(1, 1), 0), EDITOR_OK);
    }
    virtual EditorResult report(Node &, const LineCursor &)
    {
      return EDITOR_OK;
    }
    virtual FollowUp finishTake(Node &, const Reply<Request> &, const RequestApplication<LineCursor, EditorResult> &)
    {
      return FOLLOW_NONE;
    }
    unsigned applications() const
    {
      return this->applications_;
    }

  private:
    const RequestBinding<Request> binding_;
    const bool defer_;
    unsigned applications_;
  };
  /** Fixture-owned projection: production settlement without native editing. */
  class ProbeController : public IPlatformController
  {
  public:
    virtual void onChange(Node *, NodeDirtyFlags, bool) {}
    virtual void synchronize() {}
    virtual bool hasPendingSync() const { return false; }
    virtual void destroy() {}
  };
  class ProbeOwner : public SettleOwner<LineCursor>
  {
  public:
    explicit ProbeOwner(IPlatformController &controller, Node &node, bool failed)
        : SettleOwner<LineCursor>(controller, &node, node.getContext()), failed_(failed)
    {
    }
    virtual FollowUpResult finishSettle(Node &, const FollowUps &)
    {
      return this->failed_ ? FOLLOW_UP_FAILED : FOLLOW_UP_NONE;
    }

  private:
    const bool failed_;
  };
  struct Fixture : BoundaryInnerStateOwner
  {
    virtual void noteStateAllocationFailure()
    {
      assert(false && "unexpected state OOM in settlement cost probe");
    }
    Reported<LineCursor> cursor;
    RequestQueue<LineCursor, 4> carets;
    RequestQueue<EditorCommand, 4> commands;
    loka::core::ObservableList<loka::core::String> lines;
    ProbeController controller;
    TextEditorNode node;
    Fixture()
        : node(TextEditorProps(lines, cursor))
    {
      StateBatchBase::CreateImmediateState(this, this->cursor, LineCursor::None());
      StateBatchBase::CreateImmediateState(this, this->carets, LineCursor::None());
      StateBatchBase::CreateImmediateState(this, this->commands, EditorCommand::None());
      this->node.props = TextEditorProps(this->lines, this->cursor).moveCaretTo(this->carets).command(this->commands);
      this->node.setContext(new NodeContext());
    }
  };

  bool run(std::FILE *log)
  {
    const char *const names[] = {"empty", "two-caret", "two-command", "failed-arm-tail"};
    const loka_toolbox_probe::Timer deadline;
    Fixture f;
    for (unsigned mode = 0; mode < 4; ++mode)
    {
      unsigned long elapsed = 0;
      for (unsigned i = 0; i < kIterations; ++i)
      {
        // Setup and logging are outside the timed interval.
        const LineCursor caret(loka::core::ItemId(1, 1), 0);
        const EditorCommand command(EditorCommand::PAGE_DOWN);
        if (mode == 1 && (f.carets.post(caret) != POST_ACCEPTED || f.carets.post(caret) != POST_ACCEPTED))
          return false;
        if (mode == 2 && (f.commands.post(command) != POST_ACCEPTED || f.commands.post(command) != POST_ACCEPTED))
          return false;
        if (mode == 3 && (f.carets.post(caret) != POST_ACCEPTED || f.commands.post(command) != POST_ACCEPTED))
          return false;
        ProbeOwner owner(f.controller, f.node, mode == 3);
        ProbeSeat<LineCursor> carets(f.node.props.moveCaretTo_, mode == 3);
        ProbeSeat<EditorCommand> commands(f.node.props.command_, mode == 3);
        const loka_toolbox_probe::Timer timer;
        const FollowUpResult result = RequestSettlement<LineCursor>::settle(
            owner, carets, commands, SETTLE_DEFERRED);
        elapsed += timer.elapsed();
        if (result != (mode == 3 ? FOLLOW_UP_FAILED : FOLLOW_UP_NONE) || carets.applications() != (mode == 1 ? 2u : 0u)
            || commands.applications() != (mode == 2 ? 2u : 0u) || !f.carets.state()->get().isNone()
            || !f.commands.state()->get().isNone() || f.carets.pending() || f.commands.pending())
          return false;
        if (mode == 3
            && (f.carets.reply().state()->get().kind() != Reply<LineCursor>::REFUSED
                || f.commands.reply().state()->get().kind() != Reply<EditorCommand>::REFUSED))
          return false;
        if (deadline.elapsed() > 100000000UL)
          return false;
      }
      if (std::fprintf(log, "phase=%s iterations=%u us=%lu\r", names[mode], kIterations, elapsed) < 0)
        return false;
    }
    return true;
  }
} // namespace
int main(int, char **)
{
  loka::platform::InitPlatformRuntime();
  loka::platform::file::FileHandle file;
  if (!loka::platform::file::ResolveApplicationSidecar(loka::file::File::Application() << loka::file::File("LOG.TXT"),
                                                       file))
    return 1;
  std::FILE *log = loka::platform::file::OpenWriteTruncate(file);
  if (!log)
    return 1;
  const bool passed = run(log);
  std::fprintf(log, "settle-probe %s\r", passed ? "PASS" : "FAIL");
  const bool flushed = loka::platform::file::FlushWrite(log, file);
  std::fclose(log);
  ExitToShell();
  return passed && flushed ? 0 : 1;
}
