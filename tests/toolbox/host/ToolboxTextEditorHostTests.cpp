#include "app/nodes/controls/TextEditorRequests.hpp"
#include "ToolboxTextCapacity.hpp"
#include "support/TextBufferFixture.hpp"
#include "support/TextAlphabetCases.hpp"
#include "ToolboxGround.hpp"
#include "ToolboxEditKey.hpp"
#include "ToolboxInputDoor.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/TextEditorStateOwner.hpp"
#include "support/TextEditorReportRefusal.hpp"
#include "app/scene/state/RequestSettlement.hpp"
#include "support/TextEditorAccess.hpp"
#include "context/ToolboxTextEditorContext.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "support/TestVerify.hpp"
#include "support/TextEditorContractSnapshot.hpp"
#include "support/LokaAllocFailure.hpp"
#include <Sound.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "toolbox/ToolboxTextEditorAccess.hpp"
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
using loka::testing::ToolboxTextEditorAccess;
namespace
{
  struct Fixture : HeadlessStateOwner
  {
    PushStateTracker &tracker;
    Reported<LineCursor> cursor;
    RequestWithReply<LineCursor> request;
    Request<LineCursor> otherRequest;
    RequestQueue<LineCursor, 4> queue;
    RequestQueue<EditorCommand, 4> commands;
    loka::testing::TextBufferFixture lines;
    ToolboxWindow window;
    ToolboxScenePlatformController controller;
    TextEditorNode node;
    ToolboxTextEditorContext *context;
    Fixture(unsigned short count = 3,
            const std::string &text = "abcd",
            unsigned short capacity = 256,
            bool preContextRequest = false,
            std::size_t byteCapacity = 8192)
        : tracker(*HeadlessStateOwner::tracker()->asPushTracker()),
          controller(&window),
          node(TextEditorProps(lines, cursor)),
          context(0)
    {
      StateBatchBase::CreateImmediateState(this, this->cursor, LineCursor::None());
      StateBatchBase::CreateImmediateState(this, this->request, LineCursor::None());
      StateBatchBase::CreateImmediateState(this, this->otherRequest, LineCursor::None());
      StateBatchBase::CreateImmediateState(this, this->queue, LineCursor::None());
      StateBatchBase::CreateImmediateState(this, this->commands, EditorCommand::None());
      this->node.props = TextEditorProps(lines, cursor).moveCaretTo(request).command(commands);
      LOKA_VERIFY(lines.attach(&tracker, capacity, byteCapacity) == ATTACH_OK);
      for (unsigned short i = 0; i < count; ++i)
        LOKA_VERIFY(lines.insert(i, String(text)) == EDIT_OK);
      if (preContextRequest)
      {
        StateTrackerGuard guard(&this->tracker);
        this->request.set(LineCursor(this->lines.at(1).id, 2));
      }
      NotifySubtreeNodeAttached(&this->node);
      LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
      LayoutState state;
      state.x = 10;
      state.y = 20;
      state.width = 200;
      state.height = 80;
      IPlatformNodeHandler *handler = controller.nodeHandlerRegistry_.find(&node);
      LOKA_VERIFY(handler);
      context = static_cast<ToolboxTextEditorContext *>(handler->ensureContext(&node, &controller, state));
      LOKA_VERIFY(context);
      state.spacing = 9;
      LOKA_VERIFY(context->layout(&controller, state) == 200);
      LOKA_VERIFY(state.y == 100);
      LOKA_VERIFY(!ToolboxTextEditorAccess::te(*context));
      context->render(&controller);
      // Seed native editing through its input seam; request tests post explicitly.
      if (!preContextRequest && count && ToolboxTextEditorAccess::status(*context) == EDITOR_OK)
      {
        Point initial = {
            static_cast<short>((**this->te()).viewRect.top),
            static_cast<short>((**this->te()).viewRect.left + 6 * std::min(2, static_cast<int>(text.size())))};
        LOKA_VERIFY(context->click(initial) == EDITOR_OK);
      }
    }
    ~Fixture()
    {
      if (context)
        context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
      controller.flushTE();
    }
    TEHandle te()
    {
      return ToolboxTextEditorAccess::te(*context);
    }
    std::string native()
    {
      return (**te()).text;
    }
    unsigned restores()
    {
      return ToolboxTextEditorAccess::restores(*context);
    }
  };
  struct CommandObserver
  {
    Fixture &f;
    unsigned replies;
    explicit CommandObserver(Fixture &fixture)
        : f(fixture),
          replies(0)
    {
      f.commands.reply().state()->bind(&changed, this, false);
    }
    ~CommandObserver()
    {
      f.commands.reply().state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      CommandObserver &o = *static_cast<CommandObserver *>(data);
      ++o.replies;
      LOKA_VERIFY(o.f.commands.reply().state()->get().kind() == Reply<EditorCommand>::GRANTED);
    }
  };
  template <unsigned Length> void wrapOnCommandTake(void *data)
  {
    Fixture &f = *static_cast<Fixture *>(data);
    if (!f.commands.state()->get().isNone())
      return;
    for (unsigned short i = 0; i < f.lines.size(); ++i)
      LOKA_VERIFY(f.lines.update(f.lines.at(i).id, String(std::string(Length, 'a'))) == EDIT_OK);
  }
  // Run the same key through an isolated fake TE before the rail sees it.
  // Expectations come from native text/selection, never offsetOf/cursorAt.
  void nativeKey(Fixture &f, char key)
  {
    TERec expected = **f.te();
    TERec *record = &expected;
    TEKey(key, &record);
    unsigned short row = 0;
    short start = 0;
    for (short i = 0; i < expected.selStart; ++i)
      if (expected.text[i] == '\r')
      {
        ++row;
        start = i + 1;
      }
    const int selections = toolbox_host::selections;
    LOKA_VERIFY(f.context->key(key) == EDITOR_OK);
    f.context->onPropsApplied();
    LOKA_VERIFY(toolbox_host::selections == selections);
    LOKA_VERIFY(f.native() == expected.text);
    LOKA_VERIFY((**f.te()).selStart == expected.selStart);
    LOKA_VERIFY((**f.te()).selEnd == expected.selEnd);
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(row).id, expected.selStart - start));
    std::string committed;
    LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(f.node).project(committed) == EDITOR_OK);
    LOKA_VERIFY(committed == expected.text);
  }
  struct Snapshot : loka::testing::TextEditorContractSnapshot
  {
    std::string projection;
    explicit Snapshot(Fixture &f)
        : loka::testing::TextEditorContractSnapshot(f)
    {
      LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(f.node).project(this->projection) == EDITOR_OK);
    }
    void unchanged(Fixture &f) const
    {
      loka::testing::TextEditorContractSnapshot::unchanged(f);
      LOKA_VERIFY(f.native() == this->projection);
    }
  };
  void ignoreKey(void *value)
  {
    Fixture &f = *static_cast<Fixture *>(value);
    const ListRevision revision = f.lines.revision().get();
    const LineCursor cursor = f.cursor.state()->get();
    const std::string native = f.native();
    const short start = (**f.te()).selStart, end = (**f.te()).selEnd;
    const int sets = toolbox_host::sets, selections = toolbox_host::selections;
    const unsigned restores = f.restores();
    const unsigned settles = loka::app::testing::SettleTrace<LineCursor>::instance().size();
    LOKA_VERIFY(f.context->key(0x0C) == EDITOR_OK);
    LOKA_VERIFY(!(revision != f.lines.revision().get()) && cursor == f.cursor.state()->get());
    LOKA_VERIFY(f.native() == native && (**f.te()).selStart == start && (**f.te()).selEnd == end);
    LOKA_VERIFY(toolbox_host::sets == sets && toolbox_host::selections == selections);
    LOKA_VERIFY(f.restores() == restores);
    LOKA_VERIFY(loka::app::testing::SettleTrace<LineCursor>::instance().size() == settles);
  }
  /** Like TextEditorReportRefusal, enters through the real string projection seam. */
  class IgnoreDuringProjection : public loka::platform::String
  {
  public:
    explicit IgnoreDuringProjection(Fixture &f)
        : fixture_(f)
    {
    }
    virtual bool queryUtf8(loka::platform::Utf8View &out) const
    {
      ignoreKey(&this->fixture_);
      out.bytes = "abcd";
      out.length = 4;
      return true;
    }
    virtual bool appendUtf8(std::string &out) const
    {
      out.append("abcd");
      return true;
    }

  private:
    Fixture &fixture_;
  };
  struct Observer
  {
    Fixture &f;
    int count;
    bool nested;
    bool external;
    EditorResult result;
    Observer(Fixture &value)
        : f(value),
          count(0),
          nested(false),
          external(false),
          result(EDITOR_OK)
    {
      const_cast<State<ListRevision> &>(f.lines.revision()).bind(&changed, this, false);
    }
    ~Observer()
    {
      const_cast<State<ListRevision> &>(f.lines.revision()).unbind(&changed, this);
    }
    static void changed(void *p)
    {
      Observer &o = *static_cast<Observer *>(p);
      ++o.count;
      if (o.nested)
      {
        o.nested = false;
        const int sets = toolbox_host::sets;
        o.result = o.f.context->key('z');
        LOKA_VERIFY(toolbox_host::sets == sets);
      }
      if (o.external)
      {
        o.external = false;
        loka::core::testing::PushStateTrackerTestAccess::defer(o.f.tracker, &settled, &o);
      }
    }
    static void settled(void *p)
    {
      Observer &o = *static_cast<Observer *>(p);
      LOKA_VERIFY(o.f.lines.update(o.f.lines.at(1).id, String("owner")) == EDIT_OK);
      o.f.context->onPropsApplied();
    }
  };
  struct DoorSettlementObserver
  {
    Fixture &fixture;
    unsigned reports;
    explicit DoorSettlementObserver(Fixture &f) : fixture(f), reports(0) {}
    static void report(void *data)
    {
      DoorSettlementObserver &self = *static_cast<DoorSettlementObserver *>(data);
      ++self.reports;
      LOKA_VERIFY(self.fixture.controller.borrowPhase().open());
    }
  };
  struct RequestObserver
  {
    Fixture &f;
    LineCursor repost;
    bool onClear, onReport;
    unsigned requests, reports;
    RequestObserver(Fixture &fixture)
        : f(fixture),
          repost(),
          onClear(false),
          onReport(false),
          requests(0),
          reports(0)
    {
      f.request.state()->bind(&requestChanged, this, false);
      f.cursor.state()->bind(&factChanged, this, false);
    }
    ~RequestObserver()
    {
      f.request.state()->unbind(&requestChanged, this);
      f.cursor.state()->unbind(&factChanged, this);
    }
    static void requestChanged(void *value)
    {
      RequestObserver &o = *static_cast<RequestObserver *>(value);
      ++o.requests;
      LOKA_VERIFY(o.requests < 20);
      if (o.onClear && o.f.request.get().isNone())
      {
        o.onClear = false;
        o.f.request.set(o.repost);
        const int selections = toolbox_host::selections;
        o.f.context->onPropsApplied();
        LOKA_VERIFY(toolbox_host::selections == selections);
      }
    }
    static void factChanged(void *value)
    {
      RequestObserver &o = *static_cast<RequestObserver *>(value);
      ++o.reports;
      if (o.onReport)
      {
        LOKA_VERIFY(o.f.request.get().isNone());
        o.onReport = false;
        o.f.request.set(o.repost);
        const int selections = toolbox_host::selections;
        o.f.context->onPropsApplied();
        LOKA_VERIFY(toolbox_host::selections == selections);
        LOKA_VERIFY(o.f.request.get() == o.repost);
      }
    }
  };
  /** Alternate cursor values so every take produces a report. The bound
      assertion stops an unbounded consumer before it can exhaust memory. */
  struct RepostingObserver
  {
    Fixture &f;
    const int repostLimit;
    unsigned delivered;
    unsigned reports;
    RepostingObserver(Fixture &fixture, int limit)
        : f(fixture), repostLimit(limit), delivered(0), reports(0)
    {
      f.cursor.state()->bind(&changed, this, false);
    }
    ~RepostingObserver()
    {
      f.cursor.state()->unbind(&changed, this);
    }
    static void changed(void *value)
    {
      RepostingObserver &o = *static_cast<RepostingObserver *>(value);
      ++o.delivered;
      LOKA_VERIFY(o.delivered <= 2);
      ++o.reports;
      LOKA_VERIFY(o.f.request.get().isNone());
      if (o.repostLimit < 0 || o.reports <= static_cast<unsigned>(o.repostLimit))
      {
        o.f.request.set(LineCursor(o.f.lines.at(1).id, o.f.cursor.state()->get().column == 1 ? 2 : 1));
        o.f.context->onPropsApplied();
        LOKA_VERIFY(!o.f.request.get().isNone());
      }
    }
  };
  /** Synchronous app work invalidating a borrowed binding or native offsets. */
  struct RequestAction
  {
    enum Action
    {
      EDIT_ON_TAKE,
      EDIT_ON_REPORT,
      DETACH_ON_TAKE,
      REBIND_ON_TAKE,
      NESTED_ON_TAKE
    };
    Fixture &f;
    const Action action;
    bool armed;
    RequestAction(Fixture &fixture, Action value)
        : f(fixture),
          action(value),
          armed(true)
    {
      f.request.state()->bind(&taken, this, false);
      f.cursor.state()->bind(&reported, this, false);
    }
    ~RequestAction()
    {
      f.request.state()->unbind(&taken, this);
      f.cursor.state()->unbind(&reported, this);
    }
    static void taken(void *value)
    {
      RequestAction &o = *static_cast<RequestAction *>(value);
      if (!o.armed || !o.f.request.get().isNone() || o.action == EDIT_ON_REPORT)
        return;
      o.armed = false;
      switch (o.action)
      {
      case EDIT_ON_TAKE:
        LOKA_VERIFY(o.f.lines.update(o.f.lines.at(0).id, String("longer")) == EDIT_OK);
        break;
      case EDIT_ON_REPORT:
        break;
      case DETACH_ON_TAKE:
        NotifySubtreeNodeDetached(&o.f.node);
        break;
      case REBIND_ON_TAKE:
      {
        const bool applied =
            NodePropsApplier<TextEditorNode, TextEditorProps>::apply(&o.f.node, TextEditorProps(o.f.lines, o.f.cursor));
        LOKA_VERIFY(applied);
        break;
      }
      case NESTED_ON_TAKE:
        LOKA_VERIFY(o.f.context->key('x') == EDITOR_REENTRANT);
        break;
      }
      o.f.context->onPropsApplied();
    }
    static void reported(void *value)
    {
      RequestAction &o = *static_cast<RequestAction *>(value);
      if (o.action == EDIT_ON_TAKE)
      {
        LOKA_VERIFY(o.f.native() == "longer\rabcd\rabcd");
        LOKA_VERIFY((**o.f.te()).selStart == 10);
      }
      if (!o.armed || o.action != EDIT_ON_REPORT)
        return;
      o.armed = false;
      LOKA_VERIFY(o.f.lines.update(o.f.lines.at(0).id, String("longer")) == EDIT_OK);
      o.f.context->onPropsApplied();
    }
  };
  void invalidated(void *value)
  {
    ++*static_cast<unsigned *>(value);
  }
  void post(Fixture &f, LineCursor cursor)
  {
    StateTrackerGuard guard(&f.tracker);
    f.request.set(cursor);
  }
  void retireContext(void *value)
  {
    Fixture &f = *static_cast<Fixture *>(value);
    LifecycleFactTestAccess::MarkSubtreeRetired(&f.node);
    f.node.setContext(0);
    f.context = 0;
  }
  void editOnTake(void *value)
  {
    Fixture &f = *static_cast<Fixture *>(value);
    if (f.request.get().isNone())
      LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("longer")) == EDIT_OK);
  }
  void editOnReply(void *value)
  {
    Fixture &f = *static_cast<Fixture *>(value);
    LOKA_VERIFY(f.context->key('x') == EDITOR_REENTRANT);
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("longer")) == EDIT_OK);
    f.context->onPropsApplied();
  }
  void scrollOnReply(void *value)
  {
    Fixture &f = *static_cast<Fixture *>(value);
    OffsetRect(&(**f.te()).destRect, 0, -16);
  }
  void backspaceAtOrigin(void *value)
  {
    Fixture &f = *static_cast<Fixture *>(value);
    // An origin no-op must not enter the seam during an owner publication.
    LOKA_VERIFY(f.context->key('\b') == EDITOR_OK);
  }
  void pin(const char *s)
  {
    std::printf("[pin] %s\n", s);
    std::fflush(stdout);
  }

  /** Raw rows deliberately bypass String normalization, as in TextEditorTests. */
  class AlphabetBytes : public loka::platform::String
  {
  public:
    explicit AlphabetBytes(const text_alphabet_cases::Case &value)
        : bytes_(value.bytes, value.length)
    {
    }
    virtual bool queryUtf8(loka::platform::Utf8View &out) const
    {
      out.bytes = this->bytes_.data();
      out.length = this->bytes_.size();
      return true;
    }
    virtual bool appendUtf8(std::string &out) const
    {
      out.append(this->bytes_);
      return true;
    }

  private:
    const std::string bytes_;
  };

  void alphabetProjection()
  {
    Fixture f;
    const TextBuffer *source = ToolboxTextEditorAccess::source(*f.context);
    const ListRevision revision = ToolboxTextEditorAccess::revision(*f.context);
    const std::string native = f.native();
    const int sets = toolbox_host::sets;
    const unsigned beeps = toolbox_host::beeps;
    const String raw =
        String::FromPlatform(Managed<loka::platform::String>::Wrap(new AlphabetBytes(text_alphabet_cases::cases[0])));
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, raw) == EDIT_OK);
    f.context->onPropsApplied();
    LOKA_VERIFY(ToolboxTextEditorAccess::status(*f.context) == EDITOR_NON_ASCII);
    LOKA_VERIFY(toolbox_host::sets == sets && f.native() == native);
    LOKA_VERIFY(ToolboxTextEditorAccess::source(*f.context) == source);
    LOKA_VERIFY(!(ToolboxTextEditorAccess::revision(*f.context) != revision));
    post(f, LineCursor(f.lines.at(0).id, 0));
    f.context->onPropsApplied();
    LOKA_VERIFY(f.request.get().isNone());
    LOKA_VERIFY(f.request.reply().state()->get().kind() == Reply<LineCursor>::REFUSED);
    LOKA_VERIFY(f.request.reply().state()->get().reason() == EDITOR_NON_ASCII);
    LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_NON_ASCII);
    toolbox_host::draws.clear();
    f.context->render(&f.controller);
    LOKA_VERIFY(!toolbox_host::draws.empty());
    LOKA_VERIFY(toolbox_host::draws.back().bytes == "Editor unavailable");
    LOKA_VERIFY(toolbox_host::beeps == beeps && toolbox_host::sets == sets);
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("ascii")) == EDIT_OK);
    f.context->onPropsApplied();
    LOKA_VERIFY(ToolboxTextEditorAccess::status(*f.context) == EDITOR_OK);
    LOKA_VERIFY(f.native() == "ascii\rabcd\rabcd");
    LOKA_VERIFY(!(ToolboxTextEditorAccess::revision(*f.context) != f.lines.revision().get()));
    pin("TE alphabet outbound: refusal preserves projection, unavailable request/paint, ASCII recovery");
  }

  void alphabetPaste()
  {
    const char *const payloads[] = {0, 0, "\303\251", "a\0b"};
    const std::size_t lengths[] = {1, 0, 2, 3};
    for (unsigned i = 0; i < 4; ++i)
    {
      Fixture f;
      const Snapshot before(f);
      const short start = (**f.te()).selStart, end = (**f.te()).selEnd;
      const unsigned beeps = toolbox_host::beeps;
      LOKA_VERIFY(f.context->paste(payloads[i], lengths[i]) == (i == 1 ? EDITOR_OK : EDITOR_NON_ASCII));
      before.unchanged(f);
      LOKA_VERIFY((**f.te()).selStart == start && (**f.te()).selEnd == end);
      LOKA_VERIFY(ToolboxTextEditorAccess::idle(*f.context));
      LOKA_VERIFY(toolbox_host::beeps == beeps + (i >= 2 ? 1u : 0u));
      LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_OK);
    }
    pin("TE alphabet paste: null delegation, high byte/NUL refusal, unchanged facts/collapsed selection, one beep, "
        "idle "
        "recovery");
    {
      Fixture f;
      const Snapshot before(f);
      const unsigned beeps = toolbox_host::beeps;
      loka::core::testing::failLokaAllocRaw("TextEditor", "Scratch", 1);
      LOKA_VERIFY(f.context->paste("\303\251", 2) == EDITOR_NON_ASCII);
      before.unchanged(f);
      LOKA_VERIFY(toolbox_host::beeps == beeps + 1);
      LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_ALLOCATION);
      before.unchanged(f);
      LOKA_VERIFY(toolbox_host::beeps == beeps + 1);
      loka::core::testing::allowLokaAllocRaw();
      LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_OK);
    }
    for (int stale = 0; stale < 2; ++stale)
    {
      Fixture f;
      if (stale)
        LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("owner")) == EDIT_OK);
      const unsigned beeps = toolbox_host::beeps;
      const std::string payload(stale ? 2 : 8193, static_cast<char>(0x80));
      LOKA_VERIFY(f.context->paste(payload.data(), payload.size()) == (stale ? EDITOR_STALE_ID : EDITOR_CAPACITY));
      LOKA_VERIFY(toolbox_host::beeps == beeps);
      LOKA_VERIFY(ToolboxTextEditorAccess::idle(*f.context));
    }
    pin("TE alphabet paste: capacity/stale precede alphabet; alphabet precedes scratch");
    {
      Fixture f;
      const unsigned beeps = toolbox_host::beeps;
      f.cursor.state()->bind(&retireContext, &f, false);
      LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_OK);
      f.cursor.state()->unbind(&retireContext, &f);
      LOKA_VERIFY(!f.node.getContext() && toolbox_host::beeps == beeps);
    }
    {
      Fixture f;
      const unsigned beeps = toolbox_host::beeps;
      post(f, LineCursor(f.lines.at(1).id, 1));
      f.request.reply().state()->bind(&retireContext, &f, false);
      LOKA_VERIFY(f.context->paste("\303\251", 2) == EDITOR_NON_ASCII);
      f.request.reply().state()->unbind(&retireContext, &f);
      LOKA_VERIFY(!f.node.getContext() && toolbox_host::beeps == beeps);
    }
    pin("TE alphabet paste: context loss in common callback or completion never beeps");
  }
} // namespace
#include "ToolboxEditorScrollBarPins.hpp"
#include "ToolboxScrollKeyPins.hpp"

static void countParkedEditorBeneath(void *data)
{
  ++*static_cast<unsigned *>(data);
}
static void parkedEditorPress()
{
  for (int bar = 0; bar != 2; ++bar)
    for (int phase = 0; phase != 3; ++phase)
    {
      Fixture f(30);
      NotifySubtreeNodeAttached(&f.node);
      LifecycleFactTestAccess::DeliverFacts(&f.node);
      MutableState<String> text(String("other"));
      EditTextNode other((EditTextProps()));
      NotifySubtreeNodeAttached(&other);
      ToolboxEditTextContext *context = new ToolboxEditTextContext(&other);
      other.setContext(context);
      const Rect field = {150, 0, 170, 100};
      LOKA_VERIFY(f.controller.ensureEditTextControl(context, field, &text, NATIVE_HINT_DEFAULT).te);
      LOKA_VERIFY(f.controller.applyNativeFocus(*context));
      const Rect rect = bar ? f.controller.scrollControl(0).rect : f.context->geometry().text;
      Point point = {static_cast<short>(rect.top + 2), static_cast<short>(rect.left + 2)};
      if (phase)
      {
        NotifySubtreeNodeDetached(&f.node);
        if (phase == 2)
          LifecycleFactTestAccess::DeliverFacts(&f.node);
      }
      unsigned beneathCalls = 0;
      EmitterState beneathAction;
      beneathAction.bind(&countParkedEditorBeneath, &beneathCalls, false);
      ButtonNode beneath((ButtonProps()));
      NotifySubtreeNodeAttached(&beneath);
      ToolboxButtonContext *beneathContext = new ToolboxButtonContext();
      beneath.setContext(beneathContext);
      beneathContext->rect_ = rect;
      beneathContext->emitter_ = &beneathAction;
      ToolboxHitLedger::ButtonHit hit;
      hit.rect = rect;
      hit.context = beneathContext;
      hit.emitter = &beneathAction;
      hit.enabled = 0;
      hit.boundary = 0;
      f.controller.installHit(hit);
      const unsigned tracks = toolbox_host::tracks;
      f.controller.publishFixturePressOrder();
      ToolboxInputDoor::mouseDown(f.controller, point);
      LOKA_VERIFY(beneathCalls == (phase == 2 ? 1u : 0u));
      beneathAction.unbind(&countParkedEditorBeneath, &beneathCalls);
      NodeContext *actual = 0;
      LOKA_VERIFY(f.controller.readNativeFocus(actual));
      if (phase == 1)
      {
        LOKA_VERIFY(actual == context);
        LOKA_VERIFY(toolbox_host::tracks == tracks);
      }
      else if (phase == 2)
      {
        LOKA_VERIFY(actual == 0);
        LOKA_VERIFY(!f.controller.scrollControl(0).editor.editor());
        LOKA_VERIFY(!f.controller.scrollControl(0).control->visible);
      }
      else if (bar)
        LOKA_VERIFY(toolbox_host::tracks == tracks + 1);
      else
        LOKA_VERIFY(actual == f.context);
      f.controller.retireEditTextControl(context, NATIVE_HINT_EAGER_RELEASE);
      std::printf("[pin] parked TextEditor bar=%d phase=%d passed\n", bar, phase);
    }
}
/** Before the parked fact is delivered the editor keeps its row and native focus;
    neither a text key nor a view key may reach it. After delivery the row is gone. */
static void parkedEditorKeys()
{
  for (int phase = 0; phase != 3; ++phase)
  {
    Fixture f(20);
    NotifySubtreeNodeAttached(&f.node);
    LifecycleFactTestAccess::DeliverFacts(&f.node);
    LOKA_VERIFY(f.controller.applyNativeFocus(*f.context));
    const TEHandle te = f.te();
    const short length = (**te).teLength;
    const Rect dest = (**te).destRect;
    if (phase)
    {
      NotifySubtreeNodeDetached(&f.node);
      if (phase == 2)
        LifecycleFactTestAccess::DeliverFacts(&f.node);
    }
    const bool typed = ToolboxInputDoor::keyDown(f.controller, 'x');
    const bool scrolled = ToolboxInputDoor::scrollKey(f.controller, SCROLL_END);
    NodeContext *actual = 0;
    LOKA_VERIFY(f.controller.readNativeFocus(actual));
    if (phase == 2)
      LOKA_VERIFY(actual == 0 && !typed && !scrolled);
    else
    {
      LOKA_VERIFY(actual == f.context && typed && scrolled);
      LOKA_VERIFY((**te).teLength == length + (phase ? 0 : 1));
      LOKA_VERIFY(phase ? EqualRect(&dest, &(**te).destRect) : dest.top != (**te).destRect.top);
    }
    std::printf("[pin] parked TextEditor keys phase=%d passed\n", phase);
  }
}
int main(int argc, char **argv)
{
  if (argc == 1 || (argc == 2 && std::strcmp(argv[1], "alphabet") == 0))
  {
    alphabetProjection();
    alphabetPaste();
    if (argc == 2)
      return 0;
  }
  LOKA_VERIFY(ToolboxTextCapacity().lines == 256 && ToolboxTextCapacity().bytes == 8192);
  if (argc == 2 && std::strcmp(argv[1], "parked-keys") == 0)
  {
    parkedEditorKeys();
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "parked-press") == 0)
  {
    parkedEditorPress();
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "render-bar") == 0)
  {
    Fixture fixture(30);
    HostPortScope port(fixture.window.window());
    for (unsigned reach = 0; reach < 2; ++reach)
    {
      toolbox_host::paintOperations.clear();
      toolbox_host::controlCalls = toolbox_host::ControlCalls();
      fixture.controller.scrollControl(0).control->calls.clear();
      fixture.context->render(&fixture.controller);
      LOKA_VERIFY(toolbox_host::controlCalls.draws == 1);
      LOKA_VERIFY(!toolbox_host::paintOperations.empty());
      LOKA_VERIFY(toolbox_host::paintOperations.back() == "bar");
      const std::vector<toolbox_host::ControlCall> &calls = fixture.controller.scrollControl(0).control->calls;
      for (std::size_t i = 0; i < calls.size(); ++i)
        if (calls[i].painted)
          LOKA_VERIFY(calls[i].operation == "draw");
    }
    return 0;
  }

  if (argc == 2 && RunScrollKeyPin(argv[1]))
    return 0;
  if (argc == 2 && std::strcmp(argv[1], "key-classification") == 0)
  {
    // Independent tables: columns cover each low-control byte in numeric order.
    const ToolboxTextEditorKey controls[] = {
        TEXT_KEY_IGNORE, TEXT_KEY_IGNORE, TEXT_KEY_IGNORE,          TEXT_KEY_IGNORE, TEXT_KEY_IGNORE, TEXT_KEY_IGNORE,
        TEXT_KEY_IGNORE, TEXT_KEY_IGNORE, TEXT_KEY_DELETE_BACKWARD, TEXT_KEY_INSERT, TEXT_KEY_IGNORE, TEXT_KEY_IGNORE,
        TEXT_KEY_IGNORE, TEXT_KEY_INSERT, TEXT_KEY_IGNORE,          TEXT_KEY_IGNORE, TEXT_KEY_IGNORE, TEXT_KEY_IGNORE,
        TEXT_KEY_IGNORE, TEXT_KEY_IGNORE, TEXT_KEY_IGNORE,          TEXT_KEY_IGNORE, TEXT_KEY_IGNORE, TEXT_KEY_IGNORE,
        TEXT_KEY_IGNORE, TEXT_KEY_IGNORE, TEXT_KEY_IGNORE,          TEXT_KEY_IGNORE, TEXT_KEY_CARET,  TEXT_KEY_CARET,
        TEXT_KEY_CARET,  TEXT_KEY_CARET};
    const ToolboxEditKey editControls[] = {
        EDIT_KEY_CONSUME,  EDIT_KEY_CONSUME, EDIT_KEY_CONSUME, EDIT_KEY_CONSUME,         EDIT_KEY_CONSUME,
        EDIT_KEY_CONSUME,  EDIT_KEY_CONSUME, EDIT_KEY_CONSUME, EDIT_KEY_DELETE_BACKWARD, EDIT_KEY_CONSUME,
        EDIT_KEY_CONSUME,  EDIT_KEY_CONSUME, EDIT_KEY_CONSUME, EDIT_KEY_CONSUME,         EDIT_KEY_CONSUME,
        EDIT_KEY_CONSUME,  EDIT_KEY_CONSUME, EDIT_KEY_CONSUME, EDIT_KEY_CONSUME,         EDIT_KEY_CONSUME,
        EDIT_KEY_CONSUME,  EDIT_KEY_CONSUME, EDIT_KEY_CONSUME, EDIT_KEY_CONSUME,         EDIT_KEY_CONSUME,
        EDIT_KEY_CONSUME,  EDIT_KEY_CONSUME, EDIT_KEY_CONSUME, EDIT_KEY_NAVIGATE,        EDIT_KEY_NAVIGATE,
        EDIT_KEY_NAVIGATE, EDIT_KEY_NAVIGATE};
    for (unsigned byte = 0; byte < 256; ++byte)
    {
      const ToolboxTextEditorKey expected = byte < 32     ? controls[byte]
                                            : byte == 127 ? TEXT_KEY_DELETE_FORWARD
                                                          : TEXT_KEY_INSERT;
      LOKA_VERIFY(ClassifyTextEditorKey(static_cast<unsigned char>(byte)) == expected);
      for (unsigned roman = 0; roman < 2; ++roman)
      {
        const ToolboxEditKey edit = byte < 32               ? editControls[byte]
                                    : byte == 127           ? EDIT_KEY_DELETE_FORWARD
                                    : byte >= 128 && !roman ? EDIT_KEY_REFUSE_HIGH
                                                            : EDIT_KEY_INSERT;
        LOKA_VERIFY(ClassifyEditKey(static_cast<unsigned char>(byte), roman != 0) == edit);
      }
    }
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "ignored-keys") == 0)
  {
    // Start with form feed to pin the pre-fix document mutation explicitly.
    const unsigned char bytes[] = {0x0C, 0x00, 0x01, 0x03, 0x04, 0x05, 0x0A, 0x0B, 0x10, 0x1B};
    Fixture f;
    f.controller.activateEditControl(0);
    for (unsigned i = 0; i < sizeof(bytes); ++i)
    {
      const Snapshot before(f);
      LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, static_cast<char>(bytes[i])));
      std::printf("ignored byte 0x%02X: content revision %lu -> %lu\n",
                  bytes[i],
                  static_cast<unsigned long>(before.revision.content),
                  static_cast<unsigned long>(f.lines.revision().get().content));
      std::fflush(stdout);
      before.unchanged(f);
      loka::app::testing::SettleTrace<LineCursor>::instance().clear();
      LOKA_VERIFY(f.context->key(static_cast<char>(bytes[i])) == EDITOR_OK);
      before.unchanged(f);
      LOKA_VERIFY(loka::app::testing::SettleTrace<LineCursor>::instance().size() == 0);
    }
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "ignore-phases") == 0)
  {
    Fixture f;
    State<ListRevision> &revision = const_cast<State<ListRevision> &>(f.lines.revision());
    revision.bind(&ignoreKey, &f, false);
    const unsigned restores = f.restores();
    LOKA_VERIFY(f.context->key('x') == EDITOR_OK);
    revision.unbind(&ignoreKey, &f);
    LOKA_VERIFY(f.native() == "abxcd\rabcd\rabcd" && f.restores() == restores);
    const String text = String::FromPlatform(Managed<loka::platform::String>::Wrap(new IgnoreDuringProjection(f)));
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, text) == EDIT_OK);
    f.context->onPropsApplied();
    LOKA_VERIFY(f.native() == "abcd\rabcd\rabcd" && f.restores() == restores);
    // Release the callback-bearing string while its fixture is still alive.
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("abcd")) == EDIT_OK);
    toolbox_host::failNew = 1;
    Fixture unavailable;
    LOKA_VERIFY(!unavailable.te());
    const loka::testing::TextEditorContractSnapshot before(unavailable);
    loka::app::testing::SettleTrace<LineCursor>::instance().clear();
    LOKA_VERIFY(unavailable.context->key(0x0C) == EDITOR_OK);
    before.unchanged(unavailable);
    LOKA_VERIFY(!unavailable.te());
    LOKA_VERIFY(loka::app::testing::SettleTrace<LineCursor>::instance().size() == 0);
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "forward-delete") == 0)
  {
    for (unsigned scenario = 0; scenario < 5; ++scenario)
    {
      Fixture f(scenario == 4 ? 0 : 2);
      const short start = scenario == 0 ? 2 : scenario == 1 ? 4 : scenario == 2 ? 9 : scenario == 3 ? 1 : 0;
      const short end = scenario == 3 ? 7 : start;
      if (scenario != 4)
      {
        post(f, LineCursor(f.lines.at(scenario == 2 ? 1 : 0).id, scenario == 2 ? 4 : start));
        f.context->onPropsApplied();
      }
      TESetSelect(start, end, f.te());
      const Snapshot before(f);
      const int sets = toolbox_host::sets, selections = toolbox_host::selections;
      LOKA_VERIFY(f.context->key(0x7F) == EDITOR_OK);
      std::string document;
      LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(f.node).project(document) == EDITOR_OK);
      LOKA_VERIFY(f.native() == document);
      const ListRevision after = f.lines.revision().get();
      if (scenario == 2 || scenario == 4)
      {
        before.unchanged(f);
        LOKA_VERIFY(toolbox_host::sets == sets && toolbox_host::selections == selections);
      }
      else
      {
        LOKA_VERIFY(document == (scenario == 0 ? "abd\rabcd" : scenario == 1 ? "abcdabcd" : "acd"));
        LOKA_VERIFY(after.content == before.revision.content + 1);
        LOKA_VERIFY(after.structure == before.revision.structure + (scenario == 0 ? 0 : 1));
        LOKA_VERIFY(toolbox_host::sets == sets);
        if (scenario == 3)
        {
          Fixture backward(2);
          TESetSelect(start, end, backward.te());
          LOKA_VERIFY(backward.context->key('\b') == EDITOR_OK);
          LOKA_VERIFY(backward.native() == document);
        }
      }
    }
    return 0;
  }
  if (argc == 2 && RunBarPin(argv[1]))
    return 0;
  if (argc == 2 && std::strcmp(argv[1], "scroll-bar") == 0)
  {
    Fixture f(20);
    LOKA_VERIFY(f.controller.scrollControls().size() == 1);
    ControlRef bar = f.controller.scrollControl(0).control;
    LOKA_VERIFY(bar->visible && bar->maximum > 0);
    LOKA_VERIFY((**f.te()).viewRect.right == 192);
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "shrink-repaint") == 0)
  {
    // #1429: TEUpdate redraws only the lines the text has. After a projection
    // shortens the text, the plain retained replay paints the document ground
    // below the last line before TEUpdate; a view the text fills gets none.
    Fixture f(20);
    HostPortScope port(f.window.window());
    f.controller.rootNode_ = &f.node;
    f.controller.renderContext = f.context;
    f.window.scenePlatformController_ = &f.controller;
    f.controller.activateEditControl(0);
    toolbox_host::paintOperations.clear();
    ToolboxInputDoor::renderDirty(f.controller, f.context->chromeRect());
    for (std::size_t i = 0; i < toolbox_host::paintOperations.size(); ++i)
      LOKA_VERIFY(toolbox_host::paintOperations[i] != "erase");
    TESetSelect(0, 32767, f.te());
    LOKA_VERIFY(f.context->paste("bc", 2) == EDITOR_OK);
    LOKA_VERIFY(f.native() == "bc" && (**f.te()).nLines == 1);
    loka::toolbox::ToolboxPlanarColor documentGround, windowGround;
    LOKA_VERIFY(loka::toolbox::QueryToolboxGroundColor(SURFACE_GROUND_DOCUMENT, documentGround));
    LOKA_VERIFY(loka::toolbox::QueryToolboxGroundColor(SURFACE_GROUND_WINDOW, windowGround));
    toolbox_host::paintOperations.clear();
    toolbox_host::erasedRects.clear();
    toolbox_host::backgroundColors.clear();
    ToolboxInputDoor::renderDirty(f.controller, f.context->chromeRect());
    const std::vector<std::string> &ops = toolbox_host::paintOperations;
    LOKA_VERIFY(ops.size() == 4 && ops[0] == "frame" && ops[1] == "erase" && ops[2] == "text" && ops[3] == "bar");
    const TERec &te = **f.te();
    Rect below = te.viewRect;
    below.top = static_cast<short>(te.destRect.top + te.lineHeight);
    LOKA_VERIFY(toolbox_host::erasedRects.size() == 1 && EqualRect(&toolbox_host::erasedRects[0], &below));
    LOKA_VERIFY(toolbox_host::backgroundColors.size() == 2);
    LOKA_VERIFY(toolbox_host::backgroundColors[0] == static_cast<long>(documentGround));
    LOKA_VERIFY(toolbox_host::backgroundColors[1] == static_cast<long>(windowGround));
    return 0;
  }
  if (argc == 2 && (std::strcmp(argv[1], "pre-context-request") == 0
                    || std::strcmp(argv[1], "pre-context-refused") == 0))
  {
    const bool refused = std::strcmp(argv[1], "pre-context-refused") == 0;
    loka::app::testing::SettleTrace<LineCursor> &trace = loka::app::testing::SettleTrace<LineCursor>::instance();
    trace.clear();
    if (refused)
      toolbox_host::failNew = 1;
    Fixture f(3, "abcd", 256, true);
    const Reply<LineCursor> reply = f.request.reply().state()->get();
    std::printf("pre-context: reply=%d (GRANTED=%d REFUSED=%d), reason=%d (EDITOR_UNAVAILABLE=%d), cursorNone=%d\n",
                reply.kind(), Reply<LineCursor>::GRANTED, Reply<LineCursor>::REFUSED,
                reply.kind() == Reply<LineCursor>::REFUSED ? reply.reason() : EDITOR_OK,
                EDITOR_UNAVAILABLE, f.cursor.state()->get().isNone());
    std::fflush(stdout);
    LOKA_VERIFY(f.request.get().isNone());
    if (refused)
    {
      LOKA_VERIFY(!f.te());
      LOKA_VERIFY(reply.kind() == Reply<LineCursor>::REFUSED);
      LOKA_VERIFY(reply.reason() == EDITOR_UNAVAILABLE);
      LOKA_VERIFY(f.cursor.state()->get().isNone());
    }
    else
    {
      LOKA_VERIFY(reply.kind() == Reply<LineCursor>::GRANTED);
      LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 2));
      LOKA_VERIFY(f.te());
      LOKA_VERIFY((**f.te()).selStart == 7);
      LOKA_VERIFY(trace.size() == 1);
      LOKA_VERIFY(trace.at(0).stimulus == SETTLE_ATTACH);
      LOKA_VERIFY(trace.at(0).count == 1);
      LOKA_VERIFY(trace.at(0).takes[0].kind() == Reply<LineCursor>::GRANTED);
      LOKA_VERIFY(trace.at(0).before.isNone());
      LOKA_VERIFY(trace.at(0).after == LineCursor(f.lines.at(1).id, 2));
    }
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "frame-inset") == 0)
  {
    Fixture f(30);
    Rect expected;
    SetRect(&expected, 13, 23, 192, 97);
    LOKA_VERIFY(EqualRect(&(**f.te()).destRect, &expected));
    LOKA_VERIFY(EqualRect(&(**f.te()).viewRect, &expected));
    const Rect frame = f.context->chromeRect();
    LOKA_VERIFY(expected.left > frame.left && expected.top > frame.top
                && expected.right < frame.right && expected.bottom < frame.bottom);
    // The visible box is the hit target: a click in the padding between the
    // frame and the text rect focuses the editor instead of blurring it.
    Point padding = {21, 11};
    LOKA_VERIFY(!PtInRect(padding, &expected) && PtInRect(padding, &frame));
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(ToolboxInputDoor::mouseDown(f.controller, padding));
    NodeContext *focusedEditor = 0;
    LOKA_VERIFY(f.controller.readNativeFocus(focusedEditor) && focusedEditor == f.context);
    LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 200;
    state.height = 56;
    f.context->layout(&f.controller, state);
    SetRect(&expected, 13, 23, 192, 73);
    LOKA_VERIFY(EqualRect(&(**f.te()).destRect, &expected));
    LOKA_VERIFY(EqualRect(&(**f.te()).viewRect, &expected));
    // Monaco fake metrics: 9 ascent + 3 descent + 2 leading = 14.
    // 56 / 14 = 4 frame rows; (56 - 6) / 14 = 3 complete text rows.
    LOKA_VERIFY((**f.te()).lineHeight == 14);
    unsigned visible = 0;
    LOKA_VERIFY(ToolboxTextEditorAccess::visibleLines(*f.context, visible) && visible == 3);
    TEScroll(0, -1, f.te());
    LOKA_VERIFY(ToolboxTextEditorAccess::visibleLines(*f.context, visible) && visible == 2);
    TEScroll(0, 1, f.te());
    post(f, LineCursor(f.lines.at(2).id, 2));
    f.context->onPropsApplied();
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    f.context->onPropsApplied();
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(4).id, 2));
    LOKA_VERIFY((**f.te()).destRect.top == 3); // 73 - 5 * 14; scroll offset -20.
    LOKA_VERIFY(EqualRect(&(**f.te()).viewRect, &expected));
    state.y = 40;
    f.context->layout(&f.controller, state);
    SetRect(&expected, 13, 43, 192, 93);
    LOKA_VERIFY((**f.te()).destRect.top == 23 && (**f.te()).destRect.bottom == 73);
    LOKA_VERIFY(EqualRect(&(**f.te()).viewRect, &expected));
    f.context->render(&f.controller);
    LOKA_VERIFY(EqualRect(&(**f.te()).viewRect, &expected));
    // After relayout the refreshed binding still hits on its padding.
    f.controller.activateEditControl(f.controller.editControls_.size());
    Point movedPadding = {41, 11};
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!PtInRect(movedPadding, &expected) && ToolboxInputDoor::mouseDown(f.controller, movedPadding));
    LOKA_VERIFY(f.controller.readNativeFocus(focusedEditor) && focusedEditor == f.context);
    // Clip includes frame pixels on two edges; only the text intersection is visible.
    SetRect(&f.controller.projectionClip, 10, 40, 100, 80);
    state.y = 40;
    f.context->layout(&f.controller, state);
    SetRect(&expected, 13, 43, 100, 80);
    LOKA_VERIFY(EqualRect(&(**f.te()).viewRect, &expected));
    f.context->render(&f.controller);
    LOKA_VERIFY(EqualRect(&(**f.te()).viewRect, &expected));
    // Degenerate width, height, and exact-fit frames collapse without inversion.
    for (short dimension = 0; dimension <= 6; ++dimension)
    {
      Rect narrow, shallow, empty;
      SetRect(&narrow, 10, 40, 10 + dimension, 96);
      SetRect(&shallow, 10, 40, 210, 40 + dimension);
      SetRect(&empty, 10, 40, 10, 40);
      const Rect narrowText = ToolboxTextEditorGeometry(narrow, Rect(), false, Rect()).text;
      const Rect shallowText = ToolboxTextEditorGeometry(shallow, Rect(), false, Rect()).text;
      LOKA_VERIFY(EqualRect(&narrowText, &empty) && EqualRect(&shallowText, &empty));
      LOKA_VERIFY(!f.controller.ensureTextEditorControl(f.context, narrow, NATIVE_HINT_DEFAULT));
      LOKA_VERIFY(!f.controller.ensureTextEditorControl(f.context, shallow, NATIVE_HINT_DEFAULT));
    }
    pin("frame inset survives creation, layout, page scroll, movement and binding refresh");
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "input-door") == 0)
  {
    Fixture f;
    f.controller.activateEditControl(0);
    Observer observer(f);
    observer.external = true;
    observer.nested = true;
    DoorSettlementObserver settlement(f);
    f.cursor.state()->bind(&DoorSettlementObserver::report, &settlement, false);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    f.cursor.state()->unbind(&DoorSettlementObserver::report, &settlement);
    LOKA_VERIFY(observer.count > 0 && observer.result == EDITOR_REENTRANT);
    LOKA_VERIFY(settlement.reports > 0 && f.native() == "abxcd\rowner\rabcd");
    LOKA_VERIFY(!f.controller.borrowPhase().open());
    pin("TextEditor RailOperation settles under input door and restores outer phase");
    return 0;
  }

  if (argc == 2 && std::strcmp(argv[1], "refused-report") == 0)
  {
    Fixture f(1);
    const String text = String::FromPlatform(Managed<loka::platform::String>::Wrap(
        new loka::app::testing::TextEditorReportRefusal(f.request)));
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, text) == EDIT_OK);
    f.context->onPropsApplied();
    const LineCursor before = f.cursor.state()->get();
    const short factOffset = (**f.te()).selStart;
    LOKA_VERIFY(factOffset == before.column && factOffset != 4);
    const int selections = toolbox_host::selections, sets = toolbox_host::sets;
    post(f, LineCursor(before.line, 4));
    f.context->onPropsApplied();
    const Reply<LineCursor> reply = f.request.reply().state()->get();
    LOKA_VERIFY(reply.kind() == Reply<LineCursor>::REFUSED && reply.reason() == EDITOR_ALLOCATION);
    LOKA_VERIFY(f.cursor.state()->get() == before);
    LOKA_VERIFY(toolbox_host::selections > selections);
    LOKA_VERIFY((**f.te()).selStart == factOffset);
    LOKA_VERIFY((**f.te()).selEnd == factOffset);
    LOKA_VERIFY(toolbox_host::sets == sets);
    return 0;
  }
  if (argc == 2 && (std::strcmp(argv[1], "retire-key") == 0 || std::strcmp(argv[1], "retire-click") == 0
                    || std::strcmp(argv[1], "retire-paste") == 0))
  {
    Fixture f;
    f.cursor.state()->bind(&retireContext, &f, false);
    if (std::strcmp(argv[1], "retire-key") == 0)
      LOKA_VERIFY(f.context->key('x') == EDITOR_OK);
    else if (std::strcmp(argv[1], "retire-click") == 0)
    {
      Point point = {40, 31};
      LOKA_VERIFY(f.context->click(point) == EDITOR_OK);
    }
    else
      LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_OK);
    f.cursor.state()->unbind(&retireContext, &f);
    LOKA_VERIFY(!f.node.getContext());
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "retire-render") == 0)
  {
    Fixture f;
    f.controller.retireTextEditorControl(f.context, NATIVE_HINT_DEFAULT);
    f.request.reply().state()->bind(&retireContext, &f, false);
    post(f, LineCursor(f.lines.at(1).id, 3));
    loka::app::testing::SettleTrace<LineCursor>::instance().clear();
    f.context->render(&f.controller);
    f.request.reply().state()->unbind(&retireContext, &f);
    LOKA_VERIFY(!f.node.getContext());
    LOKA_VERIFY(loka::app::testing::SettleTrace<LineCursor>::instance().size() == 0);
    return 0;
  }
  if (argc == 2 && (std::strcmp(argv[1], "take-refused-edit") == 0
                    || std::strcmp(argv[1], "reply-edit") == 0 || std::strcmp(argv[1], "scroll-cleanup") == 0))
  {
    const bool scroll = std::strcmp(argv[1], "scroll-cleanup") == 0;
    Fixture f(scroll ? 256 : 3);
    const LineCursor before = f.cursor.state()->get();
    if (std::strcmp(argv[1], "take-refused-edit") == 0)
    {
      const LineCursor stale(f.lines.at(2).id, 1);
      LOKA_VERIFY(f.lines.remove(stale.line) == EDIT_OK);
      f.context->onPropsApplied();
      f.request.state()->bind(&editOnTake, &f, false);
      post(f, stale);
      f.context->onPropsApplied();
      f.request.state()->unbind(&editOnTake, &f);
      LOKA_VERIFY(f.native() == "longer\rabcd");
      LOKA_VERIFY(f.cursor.state()->get() == before);
    }
    else
    {
      f.request.reply().state()->bind(scroll ? &scrollOnReply : &editOnReply, &f, false);
      post(f, LineCursor(f.lines.at(1).id, 3));
      if (scroll)
      {
        const Rect beforeScroll = (**f.te()).destRect;
        LOKA_VERIFY(f.context->key('\r') == EDITOR_CAPACITY);
        LOKA_VERIFY(f.request.reply().state()->get().kind() == Reply<LineCursor>::GRANTED);
        LOKA_VERIFY(EqualRect(&beforeScroll, &(**f.te()).destRect));
      }
      else
      {
        f.context->onPropsApplied();
        LOKA_VERIFY(f.native() == "longer\rabcd\rabcd");
        LOKA_VERIFY((**f.te()).selStart == 10);
      }
      f.request.reply().state()->unbind(scroll ? &scrollOnReply : &editOnReply, &f);
    }
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "sizeof") == 0)
  {
    std::printf("sizeof(ToolboxTextEditorContext)=%lu\n", static_cast<unsigned long>(sizeof(ToolboxTextEditorContext)));
    return 0;
  }
  if (argc == 2 && (std::strcmp(argv[1], "trace") == 0 || std::strcmp(argv[1], "reply-clamp") == 0
                    || std::strcmp(argv[1], "reply-stale") == 0 || std::strcmp(argv[1], "empty-settle") == 0))
  {
    Fixture f;
    loka::app::testing::SettleTrace<LineCursor> &trace = loka::app::testing::SettleTrace<LineCursor>::instance();
    trace.clear();
    const LineCursor before = f.cursor.state()->get();
    const LineCursor desired(f.lines.at(1).id, 99);
    if (std::strcmp(argv[1], "empty-settle") == 0)
    {
      const int selections = toolbox_host::selections;
      const unsigned paints = toolbox_host::invalidations;
      f.context->onPropsApplied();
      LOKA_VERIFY(trace.size() == 0);
      LOKA_VERIFY(toolbox_host::selections == selections);
      LOKA_VERIFY(toolbox_host::invalidations == paints);
    }
    else if (std::strcmp(argv[1], "reply-stale") == 0)
    {
      const LineCursor stale(f.lines.at(2).id, 1);
      LOKA_VERIFY(f.lines.remove(stale.line) == EDIT_OK);
      post(f, stale);
      f.context->onPropsApplied();
      LOKA_VERIFY(f.request.reply().state()->get().kind() == Reply<LineCursor>::REFUSED);
      LOKA_VERIFY(f.request.reply().state()->get().reason() == EDITOR_STALE_ID);
      LOKA_VERIFY(f.cursor.state()->get() == before);
    }
    else
    {
      post(f, desired);
      f.context->onPropsApplied();
      if (std::strcmp(argv[1], "trace") == 0)
      {
        LOKA_VERIFY(trace.size() == 1);
        LOKA_VERIFY(trace.at(0).stimulus == SETTLE_PROPS);
        LOKA_VERIFY(trace.at(0).count == 1);
        LOKA_VERIFY(trace.at(0).takes[0].kind() == Reply<LineCursor>::CLAMPED);
        LOKA_VERIFY(trace.at(0).seam[0] == EDITOR_OK);
        LOKA_VERIFY(trace.at(0).before == before);
        LOKA_VERIFY(trace.at(0).after == LineCursor(desired.line, 4));
      }
      else
      {
        LOKA_VERIFY(f.request.reply().state()->get().kind() == Reply<LineCursor>::CLAMPED);
        LOKA_VERIFY(f.request.reply().state()->get().requested() == desired);
        LOKA_VERIFY(f.request.reply().state()->get().applied() == LineCursor(desired.line, 4));
      }
    }
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "request") == 0)
  {
    {
      Fixture f;
      pin("PR2 acceptance: app request is consumed and moves native caret after a commit");
      nativeKey(f, '\b');
      const LineCursor desired(f.lines.at(1).id, 2);
      {
        StateTrackerGuard guard(&f.tracker);
        f.request.set(desired);
      }
      f.context->onPropsApplied();
      LOKA_VERIFY(f.request.get().isNone());
      const short expected = static_cast<short>(f.native().find('\r') + 1 + desired.column);
      LOKA_VERIFY((**f.te()).selStart == expected && (**f.te()).selEnd == expected);
      LOKA_VERIFY(f.cursor.state()->get() == desired);
      nativeKey(f, 28);
    }
    {
      Fixture f;
      pin("stale projection and endless repost take exactly twice per props entry");
      RepostingObserver observer(f, -1);
      {
        StateTrackerGuard guard(&f.tracker);
        LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("longer")) == EDIT_OK);
        f.request.set(LineCursor(f.lines.at(1).id, 1));
      }
      for (unsigned delivery = 0; delivery < 2; ++delivery)
      {
        observer.delivered = 0;
        f.context->onPropsApplied();
        LOKA_VERIFY(observer.delivered == 2);
        LOKA_VERIFY(observer.reports == 2 * (delivery + 1));
        LOKA_VERIFY(!f.request.get().isNone());
        LOKA_VERIFY(f.native() == "longer\rabcd\rabcd");
        LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 2));
        LOKA_VERIFY((**f.te()).selStart == 9 && (**f.te()).selEnd == 9);
      }
    }
    return 0;
  }

  if (argc == 2)
  {
    Fixture f;
    const LineCursor desired(f.lines.at(1).id, 3);
    if (std::strcmp(argv[1], "report") == 0)
    {
      pin("fact publication never echoes to native; delete and arrows cross CR");
      Point point = {40, 25};
      LOKA_VERIFY(f.context->click(point) == EDITOR_OK);
      nativeKey(f, '\b');
      nativeKey(f, 28);
      nativeKey(f, 28);
      nativeKey(f, 29);
      const short native = (**f.te()).selStart;
      const int selections = toolbox_host::selections;
      LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(f.node).moveCaret(desired) == EDITOR_OK);
      f.context->onPropsApplied();
      LOKA_VERIFY(toolbox_host::selections == selections);
      LOKA_VERIFY((**f.te()).selStart == native);
      LOKA_VERIFY(f.cursor.state()->get() == desired);
    }
    else if (std::strcmp(argv[1], "project") == 0)
    {
      pin("native creation entry completion consumes request");
      f.controller.retireTextEditorControl(f.context, NATIVE_HINT_DEFAULT);
      post(f, desired);
      f.context->render(&f.controller);
      LOKA_VERIFY(f.request.get().isNone());
      LOKA_VERIFY((**f.te()).selStart == 8);
      LOKA_VERIFY(f.cursor.state()->get() == desired);
    }
    else if (std::strcmp(argv[1], "input") == 0)
    {
      pin("input completion delivers report-time request after cache completion");
      RequestObserver observer(f);
      observer.repost = desired;
      observer.onReport = true;
      LOKA_VERIFY(f.context->key(29) == EDITOR_OK);
      LOKA_VERIFY(f.request.get().isNone());
      LOKA_VERIFY((**f.te()).selStart == 8);
      LOKA_VERIFY(f.cursor.state()->get() == desired);
    }
    else if (std::strcmp(argv[1], "clear") == 0 || std::strcmp(argv[1], "epilogue") == 0)
    {
      pin("take precedes apply and finite synchronous repost survives to outer completion");
      RequestObserver observer(f);
      observer.repost = LineCursor(f.lines.at(2).id, 1);
      observer.onClear = std::strcmp(argv[1], "clear") == 0;
      observer.onReport = !observer.onClear;
      post(f, desired);
      f.context->onPropsApplied();
      LOKA_VERIFY(f.request.get().isNone());
      LOKA_VERIFY(f.cursor.state()->get() == observer.repost);
      LOKA_VERIFY((**f.te()).selStart == 11);
      LOKA_VERIFY(observer.requests == 4 && observer.reports == 2);
    }
    else if (std::strcmp(argv[1], "endless") == 0 || std::strcmp(argv[1], "five-reposts") == 0)
    {
      const bool endless = std::strcmp(argv[1], "endless") == 0;
      pin(endless ? "endless repost yields after two reports per delivery"
                  : "five reposts drain over three explicit deliveries");
      RepostingObserver observer(f, endless ? -1 : 5);
      post(f, LineCursor(f.lines.at(1).id, 1));
      for (unsigned delivery = 0; delivery < 3; ++delivery)
      {
        observer.delivered = 0;
        f.context->onPropsApplied();
        LOKA_VERIFY(observer.delivered == 2);
        LOKA_VERIFY(observer.reports == 2 * (delivery + 1));
        LOKA_VERIFY(f.request.get().isNone() == (!endless && delivery == 2));
        LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 2));
        LOKA_VERIFY((**f.te()).selStart == 7);
      }
    }
    else if (std::strcmp(argv[1], "refuse") == 0)
    {
      pin("missing native and failed projection take without reporting");
      const LineCursor before = f.cursor.state()->get();
      f.controller.retireTextEditorControl(f.context, NATIVE_HINT_DEFAULT);
      post(f, desired);
      f.context->onPropsApplied();
      LOKA_VERIFY(f.request.get().isNone());
      LOKA_VERIFY(f.cursor.state()->get() == before);
      toolbox_host::failNew = 1;
      post(f, desired);
      f.context->render(&f.controller);
      LOKA_VERIFY(f.request.get().isNone() && !f.te());
      LOKA_VERIFY(f.cursor.state()->get() == before);
      toolbox_host::failSets = 1;
      post(f, desired);
      f.context->render(&f.controller);
      LOKA_VERIFY(f.request.get().isNone());
      LOKA_VERIFY(f.cursor.state()->get() == before);
      f.context->retryProjection();
      LOKA_VERIFY(f.cursor.state()->get() == before);
    }
    else if (std::strcmp(argv[1], "take-edit") == 0 || std::strcmp(argv[1], "report-edit") == 0
             || std::strcmp(argv[1], "detach") == 0 || std::strcmp(argv[1], "rebind") == 0
             || std::strcmp(argv[1], "nested") == 0)
    {
      pin("callback invalidation is reconciled under exclusion before next delivery");
      const RequestAction::Action action = std::strcmp(argv[1], "take-edit") == 0     ? RequestAction::EDIT_ON_TAKE
                                           : std::strcmp(argv[1], "report-edit") == 0 ? RequestAction::EDIT_ON_REPORT
                                           : std::strcmp(argv[1], "detach") == 0      ? RequestAction::DETACH_ON_TAKE
                                           : std::strcmp(argv[1], "rebind") == 0      ? RequestAction::REBIND_ON_TAKE
                                                                                      : RequestAction::NESTED_ON_TAKE;
      RequestAction observer(f, action);
      const LineCursor before = f.cursor.state()->get();
      post(f, desired);
      f.context->onPropsApplied();
      LOKA_VERIFY(f.request.get().isNone());
      if (action == RequestAction::EDIT_ON_TAKE)
      {
        LOKA_VERIFY(f.native() == "longer\rabcd\rabcd");
        LOKA_VERIFY((**f.te()).selStart == 10 && f.cursor.state()->get() == desired);
      }
      else if (action == RequestAction::EDIT_ON_REPORT)
      {
        LOKA_VERIFY(f.native() == "longer\rabcd\rabcd");
        LOKA_VERIFY((**f.te()).selStart == 10);
        LOKA_VERIFY(f.cursor.state()->get() == desired);
      }
      else
        LOKA_VERIFY(f.cursor.state()->get() == before);
    }
    else if (std::strcmp(argv[1], "retry") == 0)
    {
      pin("retry entry completion delivers; unavailable input refuses without report");
      const LineCursor before = f.cursor.state()->get();
      toolbox_host::failSets = 1;
      LOKA_VERIFY(f.context->paste(std::string(8193, 'x').data(), 8193) == EDITOR_CAPACITY);
      post(f, desired);
      LOKA_VERIFY(f.context->key('x') == EDITOR_UNAVAILABLE);
      LOKA_VERIFY(f.request.get().isNone() && f.cursor.state()->get() == before);
      post(f, desired);
      f.context->retryProjection();
      LOKA_VERIFY(f.request.get().isNone());
      LOKA_VERIFY((**f.te()).selStart == 8 && f.cursor.state()->get() == desired);
    }
    else if (std::strcmp(argv[1], "clamp") == 0)
    {
      pin("request clamps columns, refuses stale identities, and leaves None silent");
      RequestObserver observer(f);
      post(f, LineCursor(desired.line, 99));
      f.context->onPropsApplied();
      LOKA_VERIFY(f.request.get().isNone());
      LOKA_VERIFY(f.cursor.state()->get() == LineCursor(desired.line, 4));
      LOKA_VERIFY((**f.te()).selStart == 9);
      const unsigned requests = observer.requests, reports = observer.reports;
      const int selections = toolbox_host::selections;
      unsigned invalidations = 0;
      f.tracker.setInvalidateCallback(&invalidated, &invalidations);
      post(f, LineCursor::None());
      LOKA_VERIFY(invalidations == 1);
      invalidations = 0;
      f.context->onPropsApplied();
      LOKA_VERIFY(invalidations == 0);
      f.tracker.setInvalidateCallback(0, 0);
      LOKA_VERIFY(observer.requests == requests && observer.reports == reports);
      LOKA_VERIFY(toolbox_host::selections == selections);
      post(f, LineCursor(desired.line, 99));
      f.context->onPropsApplied();
      LOKA_VERIFY(f.request.get().isNone() && observer.requests == requests + 2);
      LOKA_VERIFY(observer.reports == reports && toolbox_host::selections == selections + 1);
      const int afterRepeat = toolbox_host::selections;
      post(f, LineCursor(ItemId(123, 456), 1));
      f.context->onPropsApplied();
      LOKA_VERIFY(f.request.get().isNone());
      LOKA_VERIFY(observer.reports == reports && toolbox_host::selections == afterRepeat);
    }
    else
      LOKA_VERIFY(false && "unknown request test");
    return 0;
  }

  {
    Fixture f(30);
    CommandObserver observer(f);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    f.context->onPropsApplied();
    LOKA_VERIFY(f.commands.state()->get().isNone() && f.commands.pending() == 0);
    LOKA_VERIFY(observer.replies == 2);
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(8).id, 2));
    LOKA_VERIFY((**f.te()).selStart == 42 && (**f.te()).selEnd == 42);
    LOKA_VERIFY((**f.te()).destRect.top < 20);
    pin("two command posts take twice; native selection equals reported cursor and scrolls");
  }
  {
    Fixture f(30);
    // Begin on the last fully visible row so the page target requires scrolling.
    post(f, LineCursor(f.lines.at(4).id, 2));
    f.context->onPropsApplied();
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    LOKA_VERIFY(f.context->key(static_cast<char>(0x80)) == EDITOR_NON_ASCII);
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(8).id, 2));
    LOKA_VERIFY((**f.te()).destRect.top == -29);
    LOKA_VERIFY(f.restores() == 1);
    pin("refused key cleanup adopts Granted page destination instead of restoring pre-key destRect");
  }
  {
    Fixture f(30);
    // The last fully visible row makes PAGE_DOWN move the native viewport.
    post(f, LineCursor(f.lines.at(4).id, 2));
    f.context->onPropsApplied();
    const String text = String::FromPlatform(Managed<loka::platform::String>::Wrap(
        new loka::app::testing::TextEditorReportRefusal(f.request)));
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, text) == EDIT_OK);
    f.context->onPropsApplied();
    const LineCursor before = f.cursor.state()->get();
    const short beforeTop = (**f.te()).destRect.top;
    LOKA_VERIFY(before == LineCursor(f.lines.at(4).id, 2));
    LOKA_VERIFY(beforeTop == (**f.te()).viewRect.top);
    // Arm the existing report-refusal string without leaving a caret request.
    // Row zero is read by availability, but not by pageTarget's row-eight lookup.
    post(f, before);
    post(f, LineCursor::None());
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    f.context->onPropsApplied();
    const Reply<EditorCommand> reply = f.commands.reply().state()->get();
    LOKA_VERIFY(reply.kind() == Reply<EditorCommand>::REFUSED && reply.reason() == EDITOR_ALLOCATION);
    LOKA_VERIFY(f.cursor.state()->get() == before);
    LOKA_VERIFY((**f.te()).destRect.top == beforeTop);
    pin("refused report after granted native page move restores pre-command viewport");
  }
  {
    toolbox_host::failNew = 1;
    Fixture f;
    LOKA_VERIFY(!f.te());
    const LineCursor before = f.cursor.state()->get();
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    f.context->onPropsApplied();
    LOKA_VERIFY(f.commands.state()->get().isNone());
    LOKA_VERIFY(f.commands.reply().state()->get().kind() == Reply<EditorCommand>::REFUSED);
    LOKA_VERIFY(f.commands.reply().state()->get().reason() == EDITOR_UNAVAILABLE);
    LOKA_VERIFY(f.cursor.state()->get() == before);
    pin("missing TE consumes command with UNAVAILABLE and leaves fact unchanged");
  }

  {
    Fixture f(30, std::string(40, 'a'));
    unsigned visible = 0;
    LOKA_VERIFY(ToolboxTextEditorAccess::visibleLines(*f.context, visible) && visible == 2);
    SetRect(&f.controller.projectionClip, 30, 30, 80, 60);
    LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 200;
    state.height = 80;
    f.context->layout(&f.controller, state);
    f.context->render(&f.controller);
    LOKA_VERIFY(ToolboxTextEditorAccess::visibleLines(*f.context, visible) && visible == 2);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    f.context->onPropsApplied();
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 2));
    (**f.te()).destRect.top -= 1;
    (**f.te()).destRect.bottom -= 1;
    LOKA_VERIFY(ToolboxTextEditorAccess::visibleLines(*f.context, visible) && visible == 1);
    state.height = 10;
    f.context->layout(&f.controller, state);
    LOKA_VERIFY(!ToolboxTextEditorAccess::visibleLines(*f.context, visible) && visible == 0);
    pin("wrapped logical rows must fit completely; clipped view does not shrink page unit; zero declines");
  }
  {
    Fixture f(1, "");
    unsigned visible = 0;
    LOKA_VERIFY(ToolboxTextEditorAccess::visibleLines(*f.context, visible) && visible == 1);
    LOKA_VERIFY(f.lines.insert(1, String("")) == EDIT_OK);
    f.context->onPropsApplied();
    LOKA_VERIFY(ToolboxTextEditorAccess::visibleLines(*f.context, visible) && visible == 2);
    pin("empty document row and trailing empty logical row count once");
  }

  {
    Fixture f(30);
    f.commands.state()->bind(&wrapOnCommandTake<40>, &f, false);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    f.context->onPropsApplied();
    f.commands.state()->unbind(&wrapOnCommandTake<40>, &f);
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 2));
    LOKA_VERIFY((**f.te()).selStart == 43);
    pin("command take subscriber changes wrapping: repair precedes geometry query");
  }
  {
    Fixture f(30);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    {
      RepostingObserver observer(f, -1);
      post(f, LineCursor(f.lines.at(1).id, 1));
      f.context->onPropsApplied();
      LOKA_VERIFY(!f.request.get().isNone() && !f.commands.state()->get().isNone());
      LOKA_VERIFY(f.commands.reply().state()->get().kind() == Reply<EditorCommand>::NO_REPLY);
    }
    f.context->onPropsApplied();
    LOKA_VERIFY(f.request.get().isNone() && f.commands.state()->get().isNone());
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(5).id, 1));
    pin("pending caret feed defers command; draining caret permits command in same settle");
  }

  {
    Fixture f;
    typedef loka::testing::ToolboxTextEditorAdmission Gate;
    bool supplied = false, opened = false;
    LOKA_VERIFY(Gate::probe(*f.context, false, true, supplied, opened) == ADMISSION_DEFERRED);
    LOKA_VERIFY(!supplied && !opened);
    LOKA_VERIFY(Gate::probe(*f.context, false, false, supplied, opened) == ADMISSION_EMPTY);
    LOKA_VERIFY(supplied && !opened);
    LOKA_VERIFY(Gate::probe(*f.context, true, true, supplied, opened) == ADMISSION_DEFERRED);
    LOKA_VERIFY(!supplied && !opened);
    LOKA_VERIFY(Gate::probe(*f.context, true, false, supplied, opened) == ADMISSION_EMPTY);
    LOKA_VERIFY(supplied && !opened);
    post(f, LineCursor(f.lines.at(1).id, 0));
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    LOKA_VERIFY(Gate::probe(*f.context, false, true, supplied, opened) == ADMISSION_DEFERRED);
    LOKA_VERIFY(!supplied && !opened);
    LOKA_VERIFY(Gate::probe(*f.context, false, false, supplied, opened) == ADMISSION_TAKE);
    LOKA_VERIFY(supplied && opened);
    LOKA_VERIFY(Gate::probe(*f.context, true, false, supplied, opened) == ADMISSION_DEFERRED);
    LOKA_VERIFY(!supplied && !opened);
    post(f, LineCursor::None());
    LOKA_VERIFY(Gate::probe(*f.context, true, false, supplied, opened) == ADMISSION_TAKE);
    LOKA_VERIFY(supplied && opened);
    f.context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(!f.te());
    // Native absence is resolved after taking, never by the admission gate.
    LOKA_VERIFY(Gate::probe(*f.context, true, false, supplied, opened) == ADMISSION_TAKE);
    LOKA_VERIFY(supplied && opened);
    pin("Toolbox gate: phase before empty, priority before exclusion, no binding on deferral, resources in resolve");
  }

  {
    Fixture f;
    Fixture foreign;
    const TextEditorProps original = f.node.props;
    const LineCursor before = f.cursor.state()->get();
    f.node.props.command(foreign.commands);
    LOKA_VERIFY(foreign.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    f.context->onPropsApplied();
    LOKA_VERIFY(foreign.commands.reply().state()->get().kind() == Reply<EditorCommand>::REFUSED);
    LOKA_VERIFY(foreign.commands.reply().state()->get().reason() == EDITOR_OWNER_MISMATCH);
    LOKA_VERIFY(f.cursor.state()->get() == before);
    f.node.props = original;
    pin("command resolve checks its own endpoint tracker against document owner");
  }

  {
    Fixture f(30);
    const LineCursor before = f.cursor.state()->get();
    const unsigned invalidations = toolbox_host::invalidations;
    f.commands.state()->bind(&wrapOnCommandTake<200>, &f, false);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    f.context->onPropsApplied();
    f.commands.state()->unbind(&wrapOnCommandTake<200>, &f);
    LOKA_VERIFY(f.commands.reply().state()->get().kind() == Reply<EditorCommand>::REFUSED);
    LOKA_VERIFY(f.commands.reply().state()->get().reason() == EDITOR_UNAVAILABLE);
    LOKA_VERIFY(f.cursor.state()->get() == before);
    LOKA_VERIFY(toolbox_host::invalidations > invalidations);
    pin("projection repair repaint survives command geometry decline when no complete row fits");
  }

  for (int atEnd = 0; atEnd < 2; ++atEnd)
  {
    Fixture f;
    pin(atEnd ? "delete at line 2 end, then Left" : "delete inside line 2, Left across CR, Right back");
    const short column = atEnd ? 4 : 2;
    Point point = {40, static_cast<short>(13 + column * 6)};
    LOKA_VERIFY(f.context->click(point) == EDITOR_OK);
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, column));
    const ListRevision before = f.lines.revision().get();
    nativeKey(f, '\b');
    LOKA_VERIFY(f.lines.revision().get().content == before.content + 1);
    if (atEnd)
      nativeKey(f, 28);
    else
    {
      for (short i = 0; i < column; ++i)
        nativeKey(f, 28);
      nativeKey(f, 29);
    }
  }

  {
    Fixture f;
    Point inside = {24, 14}, outside = {0, 0};
    LOKA_VERIFY(!(**f.te()).active);
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(ToolboxInputDoor::mouseDown(f.controller, inside));
    NodeContext *focused = 0;
    LOKA_VERIFY(f.controller.readNativeFocus(focused) && focused == f.context);
    LOKA_VERIFY((**f.te()).active);
    ToolboxInputDoor::idleTextEdits(f.controller);
    LOKA_VERIFY((**f.te()).idleCalls == 1);
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, outside));
    LOKA_VERIFY(!f.controller.editControls_.focused() && !(**f.te()).active);
    ToolboxInputDoor::idleTextEdits(f.controller);
    LOKA_VERIFY((**f.te()).idleCalls == 1);
    f.controller.publishFixturePressOrder();
    LOKA_VERIFY(ToolboxInputDoor::mouseDown(f.controller, inside));
    LOKA_VERIFY((**f.te()).active);
    ToolboxInputDoor::idleTextEdits(f.controller);
    LOKA_VERIFY((**f.te()).idleCalls == 2);
    pin("focus activates; blur deactivates and receives no TEIdle; refocus activates and resumes TEIdle");
  }
  for (int stay = 0; stay < 2; ++stay)
  {
    const NativeLifetimeHint hint = stay ? NATIVE_HINT_DESIRE_STAY : NATIVE_HINT_DEFAULT;
    Fixture f;
    ToolboxEditTextContext ordinary;
    MutableState<String> text(String("ordinary"));
    const Rect rect = {20, 10, 40, 210};
    TEHandle editor = f.te();
    const int disposals = toolbox_host::disposals;
    f.controller.retireTextEditorControl(f.context, hint);
    LOKA_VERIFY(!f.te() && f.controller.editControls_.empty());
    LOKA_VERIFY(toolbox_host::disposals == disposals);
    f.controller.flushTE();
    LOKA_VERIFY(toolbox_host::disposals == disposals + 1);
    LOKA_VERIFY(f.controller.textEditBucket_.depth() == 0);
    GrafPtr previous;
    GetPort(&previous);
    SetPort(f.window.window());
    TEHandle edit = f.controller.ensureEditTextControl(&ordinary, rect, &text, hint).te;
    SetPort(previous);
    LOKA_VERIFY(edit && edit != editor);
    LOKA_VERIFY((**edit).txFont == 3 && (**edit).txSize == 12);
    f.window.scenePlatformController_ = &f.controller;
    f.controller.activateEditControl(0);
    const int idleBefore = (**edit).idleCalls;
    f.window.idleControls(ACTIVATION_FOREGROUND);
    LOKA_VERIFY((**edit).idleCalls == idleBefore + 1);
    f.window.idleControls(ACTIVATION_BACKGROUND);
    LOKA_VERIFY((**edit).idleCalls == idleBefore + 1);
    LOKA_VERIFY((**edit).lineHeight == 17 && (**edit).fontAscent == 12);
    f.controller.retireEditTextControl(&ordinary, hint);
    f.controller.flushTE();
    LOKA_VERIFY(f.controller.textEditBucket_.depth() == 1);
    f.context->render(&f.controller);
    LOKA_VERIFY(f.te() && f.te() != edit && f.te() != editor);
    LOKA_VERIFY((**f.te()).txFont == 4 && (**f.te()).txSize == 9);
    LOKA_VERIFY((**f.te()).lineHeight == 14 && (**f.te()).fontAscent == 9);
    LOKA_VERIFY(f.controller.textEditBucket_.depth() == 1);
    // The ordinary record remains available only to another ordinary edit.
    SetPort(f.window.window());
    LOKA_VERIFY(f.controller.ensureEditTextControl(&ordinary, rect, &text, hint).te == edit);
    SetPort(previous);
    f.controller.retireEditTextControl(&ordinary, hint);
    LOKA_VERIFY(f.controller.poolIntakeAuditFailCount_ == 0);
  }
  pin("default/desire-stay: editor disposes only at clock drain; cross-kind handles and font metrics stay isolated");
  {
    // #1026: an ordinary EditText whose value refuses UTF-8 conversion keeps
    // the text it already installed, and retries once a value converts.
    struct RefusingUtf8 : public loka::platform::String
    {
      virtual bool appendUtf8(std::string &out) const
      {
        out.append("par");
        return false;
      }
    };
    Fixture f;
    ToolboxEditTextContext ordinary;
    MutableState<String> text(String("ordinary"));
    const Rect rect = {20, 10, 40, 210};
    GrafPtr previous;
    GetPort(&previous);
    SetPort(f.window.window());
    TEHandle edit = f.controller.ensureEditTextControl(&ordinary, rect, &text, NATIVE_HINT_DEFAULT).te;
    LOKA_VERIFY(edit && (**edit).text == "ordinary");
    {
      StateTrackerGuard guard(&f.tracker);
      text.set(String::FromPlatform(Managed<loka::platform::String>::Wrap(new RefusingUtf8())));
    }
    LOKA_VERIFY(f.controller.ensureEditTextControl(&ordinary, rect, &text, NATIVE_HINT_DEFAULT).te == edit);
    std::printf("refused conversion installs \"%s\"\n", (**edit).text.c_str());
    std::fflush(stdout);
    LOKA_VERIFY((**edit).text == "ordinary" && (**edit).teLength == 8);
    {
      StateTrackerGuard guard(&f.tracker);
      text.set(String("next"));
    }
    LOKA_VERIFY(f.controller.ensureEditTextControl(&ordinary, rect, &text, NATIVE_HINT_DEFAULT).te == edit);
    LOKA_VERIFY((**edit).text == "next");
    // The refused pass left the installed record untouched, so the original
    // value converts again and is installed (not skipped as already present).
    {
      StateTrackerGuard guard(&f.tracker);
      text.set(String("ordinary"));
    }
    LOKA_VERIFY(f.controller.ensureEditTextControl(&ordinary, rect, &text, NATIVE_HINT_DEFAULT).te == edit);
    LOKA_VERIFY((**edit).text == "ordinary");
    SetPort(previous);
    f.controller.retireEditTextControl(&ordinary, NATIVE_HINT_DEFAULT);
  }
  pin("#1026: a refused UTF-8 conversion keeps the installed EditText text");
  {
    GrafPtr previous;
    GetPort(&previous);
    const short font = previous->txFont, size = previous->txSize;
    const Style face = previous->txFace;
    Fixture f;
    LOKA_VERIFY((**f.te()).txFont == 4 && (**f.te()).txSize == 9);
    LOKA_VERIFY((**f.te()).lineHeight == 14 && (**f.te()).fontAscent == 9);
    LOKA_VERIFY(f.window.port.txFont == 3 && f.window.port.txSize == 12
                && f.window.port.txFace == 0);
    GrafPtr restored;
    GetPort(&restored);
    LOKA_VERIFY(restored == previous && previous->txFont == font
                && previous->txSize == size && previous->txFace == face);
    pin("Monaco 9: TE captures font 4/size 9 and matching metrics; caller port restored");
  }
  {
    Fixture f;
    Observer o(f);
    const ListRevision before = f.lines.revision().get();
    LOKA_VERIFY(f.context->key('x') == EDITOR_OK && f.native() == "abxcd\rabcd\rabcd");
    LOKA_VERIFY(o.count == 1 && f.lines.revision().get().content == before.content + 1);
    LOKA_VERIFY(f.lines.revision().get().change.kind == LIST_UPDATE);
    LOKA_VERIFY(f.context->key('\r') == EDITOR_OK && f.lines.size() == 4 && o.count == 2);
    LOKA_VERIFY(f.lines.revision().get().change.kind == LIST_BATCH);
    LOKA_VERIFY(f.context->key('\b') == EDITOR_OK && f.lines.size() == 3 && o.count == 3);
    LOKA_VERIFY(f.lines.revision().get().change.kind == LIST_BATCH);
    Point p = {40, 19};
    LOKA_VERIFY(f.context->click(p) == EDITOR_OK && f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 1));
    LOKA_VERIFY(f.context->key(29) == EDITOR_OK && f.cursor.state()->get().column == 2 && o.count == 3);
    pin("update once; split/join one batch; click and arrows publish caret only");
  }
  {
    Fixture f(200, std::string(39, 'a'));
    toolbox_host::copied = 0;
    const int sets = toolbox_host::sets, updates = toolbox_host::updates;
    LOKA_VERIFY(f.context->key('x') == EDITOR_OK);
    LOKA_VERIFY(toolbox_host::copied == 0);
    LOKA_VERIFY(toolbox_host::sets == sets && toolbox_host::updates == updates);
    pin("range door: zero copied bytes for 7999-byte document; no TESetText/TEUpdate on accepted key");
  }
  for (int kind = 0; kind < 3; ++kind)
  {
    Fixture f(kind == 0 ? 256 : 3);
    if (kind == 1)
    {
      LOKA_VERIFY(f.lines.replaceWithOne(String("fresh")) == EDIT_OK);
    }
    Observer o(f);
    Snapshot before(f);
    if (kind == 2)
      loka::core::testing::failLokaAllocRaw("TextEditor", "Scratch", 1);
    LOKA_VERIFY(f.context->key(kind == 1 ? 'x' : '\r')
                == (kind == 0   ? EDITOR_CAPACITY
                    : kind == 1 ? EDITOR_STALE_ID
                                : EDITOR_ALLOCATION));
    before.unchanged(f);
    LOKA_VERIFY(f.restores() == 1 && o.count == 0);
    loka::core::testing::allowLokaAllocRaw();
    LOKA_VERIFY(f.context->key('y') == EDITOR_OK);
  }
  pin("capacity/stale ID/scratch refusal: unchanged facts, native restored once, next key succeeds");
  {
    Fixture f;
    LOKA_VERIFY(f.lines.replaceWithOne(String("fresh")) == EDIT_OK);
    Snapshot before(f);
    LOKA_VERIFY(f.context->paste("x", 1) == EDITOR_STALE_ID);
    before.unchanged(f);
    LOKA_VERIFY(f.restores() == 1);
    LOKA_VERIFY(f.context->paste("y", 1) == EDITOR_OK && f.native() == "fryesh");
    pin("paste refuses stale projected identity, restores current rows, then recovers");
  }
  {
    // The owner inserts a row above the caret without touching the caret's
    // line: every cached identity survives, but TE's offsets now name the
    // wrong rows. Both doors must refuse on the revision, not on the identity.
    Fixture f;
    LOKA_VERIFY(f.lines.insert(0, String("above")) == EDIT_OK);
    Snapshot before(f);
    LOKA_VERIFY(f.context->key('x') == EDITOR_STALE_ID);
    before.unchanged(f);
    LOKA_VERIFY(f.restores() == 1);
    // The restore reprojects the current rows, so the next input is current again.
    LOKA_VERIFY(f.context->paste("y", 1) == EDITOR_OK && f.restores() == 1);
    pin("unprojected owner row insertion above the caret refuses the key on the revision, then recovers");
  }
  {
    Fixture f;
    Snapshot before(f);
    (**f.te()).destRect.top -= 32;
    (**f.te()).destRect.bottom -= 32;
    toolbox_host::failSets = 1;
    LOKA_VERIFY(f.context->paste(std::string(8193, 'x').data(), 8193) == EDITOR_CAPACITY);
    LOKA_VERIFY(ToolboxTextEditorAccess::status(*f.context) == EDITOR_UNAVAILABLE);
    LOKA_VERIFY(f.context->key('y') == EDITOR_UNAVAILABLE && f.restores() == 1);
    LOKA_VERIFY((**f.te()).destRect.top == -9);
    f.context->retryProjection();
    before.unchanged(f);
    LOKA_VERIFY(f.context->key('y') == EDITOR_OK);
    LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 200;
    state.height = 80;
    f.context->layout(&f.controller, state);
    LOKA_VERIFY((**f.te()).destRect.top == 23); // Successful recovery clamps the now-fitting document.
    state.y = 40;
    state.width = 240;
    f.context->layout(&f.controller, state);
    LOKA_VERIFY((**f.te()).destRect.top == 43 && (**f.te()).viewRect.top == 43);
    pin("checked replacement blocks keys until idle retry; scroll survives restore and relayout");
  }
  {
    Fixture f;
    Observer o(f);
    o.nested = true;
    LOKA_VERIFY(f.context->key('x') == EDITOR_OK);
    LOKA_VERIFY(o.result == EDITOR_REENTRANT && o.count == 1 && f.restores() == 1);
    LOKA_VERIFY(f.native() == "abxcd\rabcd\rabcd");
    LOKA_VERIFY(f.context->key('y') == EDITOR_OK);
    const int disposals = toolbox_host::disposals;
    f.controller.editControls_.focus(0);
    f.context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(f.controller.editControls_.empty() && !f.controller.editControls_.focused());
    LOKA_VERIFY(!f.te() && f.controller.retiredTextEdits_.size() == 1 && toolbox_host::disposals == disposals);
    LOKA_VERIFY(f.context->key('x') == EDITOR_UNAVAILABLE);
    f.controller.flushTE();
    LOKA_VERIFY(toolbox_host::disposals == disposals + 1);
    pin("nested input rejected/reconciled; detach drops focus/row and defers disposal");
  }

  for (int action = 0; action < 4; ++action)
  {
    Fixture f(3, "abcd", 3);
    const ItemId first = f.lines.at(0).id;
    const int sets = toolbox_host::sets;
    Observer observer(f);
    TESetSelect(0, 32767, f.te());
    const EditorResult result = action == 3 ? f.context->paste("Q\r\nR", 4)
                                            : f.context->key(action == 0   ? 'b'
                                                             : action == 1 ? '\b'
                                                                           : '\r');
    LOKA_VERIFY(result == EDITOR_OK);
    const std::string expected = action == 0 ? "b" : action == 1 ? "" : action == 2 ? "\r" : "Q\rR";
    std::string projection;
    LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(f.node).project(projection) == EDITOR_OK
                && projection == expected);
    LOKA_VERIFY(f.native() == expected && f.lines.size() == (action < 2 ? 1 : 2));
    LOKA_VERIFY(f.lines.at(0).id == first && observer.count == 1 && f.restores() == 0);
    LOKA_VERIFY(f.cursor.state()->get()
                == LineCursor(f.lines.at(action < 2 ? 0 : 1).id, action == 0 || action == 3 ? 1 : 0));
    LOKA_VERIFY(action == 3 || toolbox_host::sets == sets);
  }
  pin("three-line selection: type, Backspace, Return and paste replace at full capacity; keys never rewrite TE");
  {
    Fixture f;
    TESetSelect(1, 12, f.te());
    const int sets = toolbox_host::sets;
    LOKA_VERIFY(f.context->key('b') == EDITOR_OK && f.native() == "abcd");
    LOKA_VERIFY(f.lines.size() == 1 && f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 2));
    LOKA_VERIFY(toolbox_host::sets == sets);
    TESetSelect(1, 3, f.te());
    LOKA_VERIFY(f.context->key('\r') == EDITOR_OK && f.native() == "a\rd");
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 0));
    LOKA_VERIFY(toolbox_host::sets == sets);
    pin("partial selection preserves prefix/suffix; selected Return splits at its start");
  }
  for (int paste = 0; paste < 2; ++paste)
  {
    Fixture f(3, "abcd", 3);
    Snapshot before(f);
    TESetSelect(1, 3, f.te());
    const unsigned restores = f.restores();
    LOKA_VERIFY((paste ? f.context->paste("Q\rR", 3) : f.context->key('\r')) == EDITOR_CAPACITY);
    before.unchanged(f);
    LOKA_VERIFY(f.restores() == restores + 1);
  }
  pin("capacity refusal with a selection restores native text once for Return and paste");
  {
    Fixture f;
    const int sets = toolbox_host::sets;
    TESetSelect(7, 7, f.te());
    LOKA_VERIFY(f.context->key('\b') == EDITOR_OK && f.native() == "abcd\racd\rabcd");
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 1));
    TESetSelect(5, 5, f.te());
    LOKA_VERIFY(f.context->key('\b') == EDITOR_OK && f.native() == "abcdacd\rabcd");
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 4));
    TESetSelect(0, 0, f.te());
    LOKA_VERIFY(f.context->key(28) == EDITOR_OK);
    Snapshot before(f);
    Observer observer(f);
    LOKA_VERIFY(f.context->key('\b') == EDITOR_OK);
    before.unchanged(f);
    LOKA_VERIFY(observer.count == 0 && toolbox_host::sets == sets);
    State<ListRevision> &revision = const_cast<State<ListRevision> &>(f.lines.revision());
    revision.bind(&backspaceAtOrigin, &f, false);
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("abcdacd")) == EDIT_OK);
    revision.unbind(&backspaceAtOrigin, &f);
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 0));
    pin("empty-selection Backspace uses native endpoint, joins predecessor, and origin publishes nothing");
  }
  {
    Fixture f;
    loka::testing::TextBufferFixture other;
    LOKA_VERIFY(other.attach(&f.tracker, 256) == ATTACH_OK);
    for (unsigned short i = 0; i < 3; ++i)
      LOKA_VERIFY(other.insert(i, String("WXYZ")) == EDIT_OK);
    LOKA_VERIFY(!(other.revision().get() != f.lines.revision().get()));
    f.node.props.buffer_ = &other;
    f.context->onPropsApplied();
    LOKA_VERIFY(f.native() == "WXYZ\rWXYZ\rWXYZ");
    f.node.props.buffer_ = &f.lines;
    f.context->onPropsApplied();
    Observer observer(f);
    observer.external = true;
    LOKA_VERIFY(f.context->key('x') == EDITOR_OK);
    LOKA_VERIFY(f.native() == "abxcd\rowner\rabcd");
    LOKA_VERIFY(f.context->key('y') == EDITOR_OK);
    LOKA_VERIFY(f.native() == "abxycd\rowner\rabcd");
    pin("equal-revision props rebinding and owner settlement writes reconcile");
  }
  {
    Fixture f(1, "a", 4, false, 4);
    loka::testing::TextBufferFixture larger;
    LOKA_VERIFY(larger.attach(&f.tracker, 4) == ATTACH_OK);
    const std::string text(1024, 'b');
    LOKA_VERIFY(larger.insert(0, String(text)) == EDIT_OK);
    f.node.props.buffer_ = &larger;
    f.context->onPropsApplied();
    LOKA_VERIFY(f.native() == text);
    f.node.props.buffer_ = &f.lines;
    f.context->onPropsApplied();
    LOKA_VERIFY(f.native() == "a");
    pin("buffer replacement keeps the fixed TE restore storage large enough");
  }
  {
    Fixture f;
    const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(f.context->queryPaintDamage(query).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
    SetRect(&f.controller.projectionClip, 30, 30, 80, 60);
    LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 200;
    state.height = 80;
    f.context->layout(&f.controller, state);
    f.context->render(&f.controller);
    LOKA_VERIFY((**f.te()).viewRect.left == 30 && (**f.te()).viewRect.right == 80);
    LOKA_VERIFY((**f.te()).destRect.left == 13 && (**f.te()).destRect.right == 192);
    LOKA_VERIFY(f.controller.editControls_[0].rect.left == 30);
    f.controller.editControls_[0].usedThisFrame = false;
    SetRect(&f.controller.projectionClip, 300, 300, 310, 310);
    state.y = 20;
    f.context->layout(&f.controller, state);
    f.context->render(&f.controller);
    LOKA_VERIFY(!f.controller.editControls_[0].usedThisFrame);
    pin("native scheduled paint and real controller installation clip native view/hit bounds");
  }

  {
    Fixture f(1, std::string(8192, 'a'));
    Snapshot before(f);
    LOKA_VERIFY(f.context->key('x') == EDITOR_CAPACITY);
    before.unchanged(f);
    LOKA_VERIFY(f.restores() == 1);
    LOKA_VERIFY(f.context->key('\b') == EDITOR_OK);
    LOKA_VERIFY(f.context->key('x') == EDITOR_OK);
    pin("8192-byte cap refuses native insertion, restores, and permits recovery");
  }
  {
    Fixture f;
    Snapshot before(f);
    LOKA_VERIFY(f.context->key(static_cast<char>(0x80)) == EDITOR_NON_ASCII);
    before.unchanged(f);
    LOKA_VERIFY(f.context->paste("Q\r\nR\nS", 6) == EDITOR_OK);
    std::string projection;
    LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(f.node).project(projection) == EDITOR_OK
                && f.native() == projection);
    pin("non-ASCII refusal and CR/LF paste normalization");
  }
  {
    toolbox_host::backgroundColors.clear();
    toolbox_host::failNew = 1;
    Fixture f;
    LOKA_VERIFY(!f.te() && ToolboxTextEditorAccess::status(*f.context) == EDITOR_UNAVAILABLE);
    // Native creation refusal paints the document ground, then restores the
    // window background. Recording calls discriminates even an all-white table.
    loka::toolbox::ToolboxPlanarColor documentGround, windowGround;
    LOKA_VERIFY(loka::toolbox::QueryToolboxGroundColor(SURFACE_GROUND_DOCUMENT, documentGround));
    LOKA_VERIFY(loka::toolbox::QueryToolboxGroundColor(SURFACE_GROUND_WINDOW, windowGround));
    LOKA_VERIFY(toolbox_host::backgroundColors.size() == 2);
    LOKA_VERIFY(toolbox_host::backgroundColors[0] == static_cast<long>(documentGround));
    LOKA_VERIFY(toolbox_host::backgroundColors[1] == static_cast<long>(windowGround));
    LOKA_VERIFY(f.context->key('x') == EDITOR_UNAVAILABLE);
    f.context->render(&f.controller);
    LOKA_VERIFY(f.te() && f.context->key('x') == EDITOR_OK);
    toolbox_host::failSets = 2;
    LOKA_VERIFY(f.context->paste("", 8193) == EDITOR_CAPACITY);
    const int sets = toolbox_host::sets;
    f.context->retryProjection();
    LOKA_VERIFY(toolbox_host::sets == sets + 1 && ToolboxTextEditorAccess::status(*f.context) == EDITOR_UNAVAILABLE);
    f.context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    f.context->retryProjection();
    LOKA_VERIFY(toolbox_host::sets == sets + 1);
    pin("TENew refusal is unavailable; persistent replacement costs one attempt per idle and detach cancels retry");
  }
  std::puts("Toolbox TextEditor host pins passed (fake TextEdit; native pixels pending)");
}
