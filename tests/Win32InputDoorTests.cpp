#include "Win32InputDoorTests.hpp"
#include "Win32InputDoor.hpp"
#include "support/TestVerify.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/Text.hpp"
#include "app/RectSurface.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <cstdio>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;

  enum InputKind
  {
    EDIT_INPUT,
    POPUP_INPUT,
    WHEEL_INPUT,
    EXTENT_INPUT
  };

  /** Fixture owns input/extent storage and the sequence beyond Scene teardown;
      visibility is owned by the selected Boundary. */
  struct InputFacts
  {
    PushStateTracker tracker;
    NodeState<bool> *shown;
    MutableState<String> text;
    MutableState<int> selection;
    MutableState<Frame> offset;
    MutableState<Frame> firstExtent, secondExtent;
    NodeState<String> textFact;
    NodeState<int> selectionFact;
    NodeState<Frame> offsetFact;
    NodeState<Frame> firstFact, secondFact;
    EmitterState change;
    const InputKind kind;
    const bool parentVisibility;
    bool cancelHide;
    WindowAdmissionTestApp *app;
    Win32Window *closeOnExtent;
    Win32ScenePlatformController *controller;
    unsigned sequence, destroyed, returned, writes, emits, secondWrites;
    explicit InputFacts(InputKind value, bool child)
        : shown(0),
          text(String("before")),
          selection(0),
          offset(Frame(0, 0, 0, 0)),
          firstExtent(Frame()),
          secondExtent(Frame()),
          textFact(&text, &tracker),
          selectionFact(&selection, &tracker),
          offsetFact(&offset, &tracker),
          firstFact(&firstExtent, &tracker),
          secondFact(&secondExtent, &tracker),
          kind(value),
          parentVisibility(child),
          cancelHide(false),
          app(0),
          closeOnExtent(0),
          controller(0),
          sequence(0),
          destroyed(0),
          returned(0),
          writes(0),
          emits(0),
          secondWrites(0)
    {
      this->tracker.addState(&this->text);
      this->tracker.addState(&this->selection);
      this->tracker.addState(&this->offset);
      this->tracker.addState(&this->firstExtent);
      this->tracker.addState(&this->secondExtent);
    }
    static void changed(void *data)
    {
      InputFacts &f = *static_cast<InputFacts *>(data);
      ++f.writes;
      LOKA_VERIFY(f.controller->borrowPhase().open());
      if (f.closeOnExtent)
      {
        f.app->requestWindowClose(f.closeOnExtent);
        f.app->flush();
        LOKA_VERIFY(f.destroyed == 0);
        return;
      }
      // Exercise immediate observers and an attempted owner flush. Each value
      // reaches Show synchronously; all structural application stays held.
      if (f.writes == 1)
      {
        f.shown->set(false);
        f.app->flush();
        f.shown->set(true);
        f.app->flush();
        if (!f.cancelHide)
        {
          f.shown->set(false);
        }
        f.app->flush();
      }
      LOKA_VERIFY(f.destroyed == 0);
    }
    static void emitted(void *data)
    {
      InputFacts &f = *static_cast<InputFacts *>(data);
      ++f.emits;
      LOKA_VERIFY(f.writes == 1 && f.selection.get() == 1);
      LOKA_VERIFY(f.controller->borrowPhase().open() && f.destroyed == 0);
    }
    static void secondPublished(void *data)
    {
      InputFacts &f = *static_cast<InputFacts *>(data);
      ++f.secondWrites;
      LOKA_VERIFY(f.writes == 1 && f.controller->borrowPhase().open());
    }
    void bind()
    {
      if (this->kind == EDIT_INPUT)
        this->text.bind(&changed, this, false);
      if (this->kind == POPUP_INPUT)
        this->selection.bind(&changed, this, false);
      if (this->kind == WHEEL_INPUT)
        this->offset.bind(&changed, this, false);
      if (this->kind == EXTENT_INPUT)
      {
        this->firstExtent.bind(&changed, this, false);
        this->secondExtent.bind(&secondPublished, this, false);
      }
      this->change.bind(&emitted, this, false);
    }
    void unbind()
    {
      this->text.unbind(&changed, this);
      this->selection.unbind(&changed, this);
      this->offset.unbind(&changed, this);
      this->firstExtent.unbind(&changed, this);
      this->secondExtent.unbind(&secondPublished, this);
      this->change.unbind(&emitted, this);
    }
  };

  class InputRoot;
  class InputChild;
  template <class T> struct InputProps : NodePropsBase<InputProps<T> >
  {
    typedef T NodeType;
    struct TypeTag
    {
    };
    InputFacts *facts;
    explicit InputProps(InputFacts *value = 0)
        : facts(value)
    {
    }
    bool operator<(const PropsBase &rhs) const
    {
      return this->facts < static_cast<const InputProps &>(rhs).facts;
    }
  };

  void declareField(NodeComposition &composition, InputFacts &facts)
  {
    ShowDefinition seat = Show(*facts.shown->state()).destroyOnDetach();
    const char *items[] = {"before", "after"};
    switch (facts.kind)
    {
    case EDIT_INPUT:
      composition.declare(seat << EditText(EditTextProps(facts.textFact)));
      break;
    case POPUP_INPUT:
      composition.declare(
          seat << PopupMenu(
              PopupMenuProps().items(items, 2).selectedIndex(facts.selectionFact).onChange(&facts.change)));
      break;
    case WHEEL_INPUT:
      composition.declare(seat << (Box().size(240, 80) << ScrollView(facts.offsetFact)));
      break;
    case EXTENT_INPUT:
      break;
    }
  }
  class InputChild : public StdCompositionBoundaryNodeBase<InputProps<InputChild> >
  {
    typedef StdCompositionBoundaryNodeBase<InputProps<InputChild> > Base;

  public:
    NodeState<bool> shown;
    explicit InputChild(const InputProps<InputChild> &props)
        : Base(props)
    {
      this->state(this->shown, true);
      if (!this->props.facts->parentVisibility)
        this->props.facts->shown = &this->shown;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      declareField(composition, *this->props.facts);
    }
  };
  class InputRoot : public StdCompositionBoundaryNodeBase<InputProps<InputRoot> >
  {
    typedef StdCompositionBoundaryNodeBase<InputProps<InputRoot> > Base;

  public:
    NodeState<bool> shown;
    explicit InputRoot(const InputProps<InputRoot> &props)
        : Base(props)
    {
      this->state(this->shown, true);
      this->props.facts->shown = &this->shown;
    }
    virtual void composeNode(NodeComposition &composition)
    {
      InputFacts &facts = *this->props.facts;
      if (facts.kind == EXTENT_INPUT)
        composition.declare(Column() << RectSurface().laidOutExtent(facts.firstFact)
                                     << (Show(*facts.shown->state()).destroyOnDetach()
                                         << RectSurface().laidOutExtent(facts.secondFact)));
      else
        composition.declare(Boundary<InputChild>(InputProps<InputChild>(&facts)));
    }
  };

  Node *findField(Node *node, InputKind kind)
  {
    if (!node)
      return 0;
    if ((kind == EDIT_INPUT && node->asEditTextNode()) || (kind == POPUP_INPUT && node->asPopupMenuNode())
        || (kind == WHEEL_INPUT && node->asScrollViewNode()))
      return node;
    INestable *nestable = node->asNestable();
    if (nestable)
      for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
      {
        Node *found = findField(child, kind);
        if (found)
          return found;
      }
    return 0;
  }

  unsigned surfaceCount(Node *node)
  {
    if (!node)
      return 0;
    unsigned count = node->asRectSurfaceNode() ? 1u : 0u;
    INestable *nestable = node->asNestable();
    for (Node *child = nestable ? nestable->childrenHead() : 0; child; child = child->nextInComposition)
      count += surfaceCount(child);
    return count;
  }

  template <class Context, class Logical> class ObservedContext : public Context
  {
    InputFacts &facts_;

  public:
    ObservedContext(Win32ScenePlatformController &rail, HWND parent, Logical *node, InputFacts &facts)
        : Context(&rail, parent, 0, 0, 240, 80, node),
          facts_(facts)
    {
    }
    virtual ~ObservedContext()
    {
      this->facts_.destroyed = ++this->facts_.sequence;
    }
  };

  struct InputFixture
  {
    InputFacts facts;
    NullPlatformContext platform;
    Win32Window window;
    WindowAdmissionTestApp app;
    static WindowProps props(InputFacts &facts)
    {
      return WindowProps()
          .frame(50, 50, 360, 260)
          .visible(true)
          .scene(new Scene(Boundary<InputRoot>(InputProps<InputRoot>(&facts))));
    }
    InputFixture(InputKind kind, bool child)
        : facts(kind, child),
          window(&platform, props(facts)),
          app(window)
    {
      this->app.flush();
      LOKA_VERIFY(this->window.hwnd());
      this->facts.app = &this->app;
      this->facts.controller = static_cast<Win32ScenePlatformController *>(
          loka::dsl::testing::SceneTestAccess::platformController(*this->window.scene()));
      LOKA_VERIFY(this->facts.controller);
    }
    Node *root()
    {
      return loka::dsl::testing::SceneTestAccess::rootBoundary(*this->window.scene());
    }
    template <class Context, class Logical> HWND observe(Logical *node)
    {
      LOKA_VERIFY(node && node->getContext());
      // Fixture-only resource retirement before replacing a live projection.
      // setContext alone is silent on a live node and cannot retire an HWND.
      // The ScrollView is empty, so there are no child native handles to move.
      node->getContext()->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
      node->setContext(0);
      ObservedContext<Context, Logical> *context =
          new ObservedContext<Context, Logical>(*this->facts.controller, this->window.hwnd(), node, this->facts);
      node->setContext(context);
      context->readLifecycleFactOnAttach();
      this->facts.controller->relayout(360, 260);
      this->app.flush(); // Drain the replaced native projection before input observation.
      return context->hwnd();
    }
    void returned()
    {
      this->facts.returned = ++this->facts.sequence;
      LOKA_VERIFY(this->facts.writes > 0 && this->facts.destroyed == 0);
      LOKA_VERIFY(!this->facts.controller->borrowPhase().open());
      this->facts.unbind();
      this->app.flush();
      LOKA_VERIFY(this->facts.destroyed > this->facts.returned);
      LOKA_VERIFY(!findField(this->root(), this->facts.kind));
    }
  };
} // namespace

void testWin32InputDoorEditTextLifetime()
{
  for (int child = 0; child != 2; ++child)
  {
    InputFixture fixture(EDIT_INPUT, child != 0);
    HWND field = fixture.observe<Win32EditTextContext>(findField(fixture.root(), EDIT_INPUT)->asEditTextNode());
    fixture.facts.bind();
    LOKA_VERIFY(SetWindowTextW(field, L"after"));
    LOKA_VERIFY(fixture.facts.text.get().equals(String("after")));
    fixture.returned();
  }
}

void testWin32InputDoorPopupLifetime()
{
  for (int child = 0; child != 2; ++child)
  {
    InputFixture fixture(POPUP_INPUT, child != 0);
    HWND field = fixture.observe<Win32PopupMenuContext>(findField(fixture.root(), POPUP_INPUT)->asPopupMenuNode());
    fixture.facts.bind();
    LOKA_VERIFY(SendMessageW(field, CB_SETCURSEL, 1, 0) == 1);
    SendMessageW(fixture.window.hwnd(), WM_COMMAND, MAKEWPARAM(0, CBN_SELCHANGE), reinterpret_cast<LPARAM>(field));
    LOKA_VERIFY(fixture.facts.emits == 1);
    fixture.returned();
  }
}

void testWin32InputDoorWheelLifetime()
{
  UINT lines = 3;
  if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0))
    lines = 3;
  if (lines == 0)
  {
    std::printf("[skip] testWin32InputDoorWheelLifetime: system wheel scrolling is disabled\n");
    return;
  }
  for (int child = 0; child != 2; ++child)
  {
    InputFixture fixture(WHEEL_INPUT, child != 0);
    HWND field = fixture.observe<Win32ScrollViewContext>(findField(fixture.root(), WHEEL_INPUT)->asScrollViewNode());
    static_cast<Win32ScrollViewContext *>(findField(fixture.root(), WHEEL_INPUT)->getContext())
        ->setScrollMetrics(SB_VERT, 2000, 80, 0);
    fixture.facts.bind();
    SendMessageW(field, WM_MOUSEWHEEL, MAKEWPARAM(0, -3 * WHEEL_DELTA), 0);
    LOKA_VERIFY(fixture.facts.writes >= 3);
    fixture.returned();
  }
}

void testWin32InputDoorExtentOrdering()
{
  for (int cancel = 0; cancel != 2; ++cancel)
  {
    InputFixture fixture(EXTENT_INPUT, false);
    fixture.facts.cancelHide = cancel != 0;
    LOKA_VERIFY(surfaceCount(fixture.root()) == 2);
    fixture.facts.bind();
    SendMessageW(fixture.window.hwnd(), WM_SIZE, SIZE_RESTORED, MAKELPARAM(480, 320));
    LOKA_VERIFY(fixture.facts.writes == 1 && fixture.facts.secondWrites == 1);
    LOKA_VERIFY(surfaceCount(fixture.root()) == 2);
    LOKA_VERIFY(!fixture.facts.controller->borrowPhase().open());
    fixture.facts.unbind();
    fixture.app.flush();
    LOKA_VERIFY(fixture.facts.shown->get() == (cancel != 0));
    LOKA_VERIFY(surfaceCount(fixture.root()) == (cancel ? 2u : 1u));
  }
}

namespace
{
  class ClosingInputWindow : public Win32Window
  {
    InputFacts &facts_;

  public:
    ClosingInputWindow(PlatformContext *platform, InputFacts &facts)
        : Win32Window(platform, InputFixture::props(facts)),
          facts_(facts)
    {
    }
    virtual ~ClosingInputWindow()
    {
      this->facts_.destroyed = ++this->facts_.sequence;
    }
  };
} // namespace

void testWin32InputDoorCloseDuringLayout()
{
  InputFacts facts(EXTENT_INPUT, false);
  NullPlatformContext platform;
  // Ownership transfers to App's close queue when the first extent arrives.
  ClosingInputWindow *window = new ClosingInputWindow(&platform, facts);
  WindowAdmissionTestApp app(*window);
  app.flush();
  facts.app = &app;
  facts.closeOnExtent = window;
  facts.controller = static_cast<Win32ScenePlatformController *>(
      loka::dsl::testing::SceneTestAccess::platformController(*window->scene()));
  LOKA_VERIFY(facts.controller);
  facts.bind();
  SendMessageW(window->hwnd(), WM_SIZE, SIZE_RESTORED, MAKELPARAM(480, 320));
  facts.returned = ++facts.sequence;
  LOKA_VERIFY(facts.writes == 1 && facts.secondWrites == 1 && facts.destroyed == 0);
  LOKA_VERIFY(!facts.controller->borrowPhase().open());
  facts.unbind();
  app.flush();
  LOKA_VERIFY(facts.destroyed > facts.returned);
}

void testWin32InputDoorHorizontalScrollLifetime()
{
  for (int child = 0; child != 2; ++child)
  {
    InputFixture fixture(WHEEL_INPUT, child != 0);
    HWND field = fixture.observe<Win32ScrollViewContext>(findField(fixture.root(), WHEEL_INPUT)->asScrollViewNode());
    static_cast<Win32ScrollViewContext *>(findField(fixture.root(), WHEEL_INPUT)->getContext())
        ->setScrollMetrics(SB_HORZ, 2000, 240, 0);
    fixture.facts.bind();
    SendMessageW(FindWindowExW(field, NULL, L"LOKA_SCROLL_VIEW", NULL), WM_HSCROLL, MAKEWPARAM(SB_LINERIGHT, 0), 0);
    LOKA_VERIFY(fixture.facts.offset.get().x == 16 && fixture.facts.offset.get().y == 0);
    LOKA_VERIFY(fixture.facts.writes == 1);
    fixture.returned();
  }
}

void testWin32InputDoorVerticalScrollLifetime()
{
  for (int child = 0; child != 2; ++child)
  {
    InputFixture fixture(WHEEL_INPUT, child != 0);
    HWND frame = fixture.observe<Win32ScrollViewContext>(findField(fixture.root(), WHEEL_INPUT)->asScrollViewNode());
    HWND bar = FindWindowExW(frame, NULL, L"SCROLLBAR", NULL);
    LOKA_VERIFY(bar);
    static_cast<Win32ScrollViewContext *>(findField(fixture.root(), WHEEL_INPUT)->getContext())
        ->setScrollMetrics(SB_VERT, 2000, 80, 0);
    fixture.facts.bind();
    SendMessageW(frame, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), reinterpret_cast<LPARAM>(bar));
    LOKA_VERIFY(fixture.facts.offset.get().y == 16 && fixture.facts.writes == 1);
    fixture.returned();
  }
}

void testWin32InputDoorCloseDuringVerticalScroll()
{
  InputFacts facts(WHEEL_INPUT, false);
  NullPlatformContext platform;
  ClosingInputWindow *window = new ClosingInputWindow(&platform, facts);
  WindowAdmissionTestApp app(*window);
  app.flush();
  facts.app = &app;
  facts.closeOnExtent = window;
  facts.controller = static_cast<Win32ScenePlatformController *>(
      loka::dsl::testing::SceneTestAccess::platformController(*window->scene()));
  LOKA_VERIFY(facts.controller);
  Node *root = loka::dsl::testing::SceneTestAccess::rootBoundary(*window->scene());
  Win32ScrollViewContext *context = static_cast<Win32ScrollViewContext *>(findField(root, WHEEL_INPUT)->getContext());
  LOKA_VERIFY(context);
  context->setScrollMetrics(SB_VERT, 2000, 80, 0);
  HWND frame = context->hwnd();
  HWND bar = FindWindowExW(frame, NULL, L"SCROLLBAR", NULL);
  LOKA_VERIFY(bar);
  facts.bind();
  SendMessageW(frame, WM_VSCROLL, MAKEWPARAM(SB_LINEDOWN, 0), reinterpret_cast<LPARAM>(bar));
  facts.returned = ++facts.sequence;
  LOKA_VERIFY(facts.writes == 1 && facts.offset.get().y == 16 && facts.destroyed == 0);
  LOKA_VERIFY(!facts.controller->borrowPhase().open());
  facts.unbind();
  app.flush();
  LOKA_VERIFY(facts.destroyed > facts.returned);
}
