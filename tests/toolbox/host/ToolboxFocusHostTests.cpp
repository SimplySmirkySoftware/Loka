#include "platform/String.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <Script.h>
#include <Sound.h>
#include "ToolboxInputDoor.hpp"
#include "support/TestVerify.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "support/Headless.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "support/StandaloneMountTestSupport.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "testing/scene/SceneFocusTestAccess.hpp"
#include "toolbox/ToolboxTextEditorAccess.hpp"
#include "ToolboxBuiltInSupport.hpp"
#include <cstdio>
#include <cstring>
using namespace loka::app;
using namespace loka::app::scene;
using namespace loka::core;
enum FieldKey
{
  FIRST = 1,
  SECOND = 2,
  THIRD = 3
};
namespace loka
{
  namespace app
  {
    template <> struct FocusKeyTraits<FieldKey> : UnsignedFocusKeyTraits<FieldKey>
    {
    };
  } // namespace app
} // namespace loka
#include "ToolboxActivationPhase.hpp"
#include "ToolboxPresentHost.hpp"
namespace
{
  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &p)
        : BoundaryNodeFor<Root>(p)
    {
    }
    virtual void composeNode(NodeComposition &) {}
  };
  class FocusWindow : public Window
  {
  public:
    FocusWindow(PlatformContext *context, ToolboxScenePlatformController &controller)
        : Window(context, props()),
          native_(controller.window_)
    {
      this->scene()->mount(&controller);
    }
    ~FocusWindow()
    {
      this->unmountSceneForTeardown(*this->scene());
    }
    static WindowProps props()
    {
      WindowProps p;
      p.scene(new Scene(Boundary<Root>()));
      return p;
    }
    virtual bool hasLiveScenePlatform() const
    {
      return true;
    }
    virtual ToolboxWindow *asToolboxWindow()
    {
      return this->native_;
    }

  private:
    ToolboxWindow *native_;
  };
  struct Fixture : HeadlessStateOwner
  {
    loka::app::Focus<FieldKey> focus;
    NodeState<String> text, replacement;
    ToolboxWindow nativeWindow;
    ToolboxScenePlatformController controller;
    loka::testing::StandaloneMountTestPlatformContext platform;
    FocusWindow window;
    ToolboxApp app;
    EditTextNode first, second;
    Fixture()
        : controller(&nativeWindow),
          window(&platform, controller),
          app(window),
          first(EditTextProps()),
          second(EditTextProps())
    {
      app.flush();
      StateBatchBase::CreateImmediateState(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()), focus);
      StateBatchBase::CreateImmediateState(this, text, String::Literal("a"));
      StateBatchBase::CreateImmediateState(this, replacement, String::Literal("b"));
      first.props = EditTextProps(text).focusedAs(focus, FIRST);
      second.props = EditTextProps(text).focusedAs(focus, SECOND);
      app.flush();
      project(first);
      project(second);
    }
    ~Fixture()
    {
      retire(first);
      retire(second);
    }
    void attach(Node &node)
    {
      ComponentContext context;
      BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene());
      context.setBoundary(root);
      context.setStateOwner(root);
      context.setScene(window.scene());
      BoundaryNode::composeSubtree(&node, context, COMPOSE_EVENT_ATTACH, root);
    }
    void project(EditTextNode &node)
    {
      attach(node);
      node.setContext(new ToolboxEditTextContext(&node));
    }
    ToolboxEditTextContext *context(EditTextNode &node)
    {
      return static_cast<ToolboxEditTextContext *>(node.getContext());
    }
    Rect rect(int x = 0)
    {
      Rect r;
      SetRect(&r, x, 0, x + 40, 20);
      return r;
    }
    void hit(EditTextNode &node, int x = 0)
    {
      controller.recordEditHit(rect(x), context(node)->projectedTextState(), 0, context(node));
    }
    void native(EditTextNode &node, int x = 0)
    {
      LOKA_VERIFY(controller.ensureEditTextControl(
          context(node), rect(x), context(node)->projectedTextState(), NATIVE_HINT_DEFAULT).te);
    }
    void click(int x = 0)
    {
      Point p = {5, static_cast<short>(x + 5)};
      LOKA_VERIFY(ToolboxInputDoor::mouseDown(controller, p));
    }
    void expect(EditTextNode *node)
    {
      NodeContext *out = 0;
      LOKA_VERIFY(controller.readNativeFocus(out));
      LOKA_VERIFY(out == (node ? node->getContext() : 0));
      {
        loka::core::Operation turn;
        app.present(ACTIVATION_FOREGROUND, turn);
        LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
      }
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
      LOKA_VERIFY(!loka::core::Operation::isSettling());
      const Focused<FieldKey> value = focus.state()->get();
      LOKA_VERIFY(!(value != (node ? Focused<FieldKey>(node == &first ? FIRST : SECOND) : Focused<FieldKey>::none())));
    }
    void retire(EditTextNode &node)
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&node);
      controller.retireEditTextControl(node.getContext(), NATIVE_HINT_EAGER_RELEASE);
      node.setContext(0);
    }
  };
  struct TabFixture : Fixture
  {
    NodeState<String> thirdText;
    EditTextNode third;
    TabFixture()
        : third(EditTextProps())
    {
      StateBatchBase::CreateImmediateState(this, this->thirdText, String::Literal("ccc"));
      this->second.props.text(this->replacement);
      this->third.props = EditTextProps(this->thirdText).focusedAs(this->focus, THIRD);
      this->project(this->third);
      // Registration order is bottom, top, middle.
      this->place(this->third, 0, 80);
      this->place(this->first, 0, 0);
      this->place(this->second, 0, 40);
    }
    ~TabFixture()
    {
      this->retire(this->third);
    }
    void place(EditTextNode &node, short left, short top)
    {
      Rect r;
      SetRect(&r, left, top, left + 40, top + 20);
      LOKA_VERIFY(this->controller
                      .ensureEditTextControl(
                          this->context(node), r, this->context(node)->projectedTextState(), NATIVE_HINT_DEFAULT)
                      .te);
    }
    void tab(ToolboxTabDirection direction, EditTextNode &target)
    {
      LOKA_VERIFY(ToolboxInputDoor::tabKey(this->controller, direction));
      LOKA_VERIFY(this->controller.editControls_.focused()->ownerContext == target.getContext());
    }
  };
  void tabGeometry()
  {
    TabFixture f;
    f.tab(TAB_FORWARD, f.first);
    f.tab(TAB_FORWARD, f.second);
    f.tab(TAB_FORWARD, f.third);
    f.tab(TAB_FORWARD, f.first);
    f.tab(TAB_BACKWARD, f.third);
    f.tab(TAB_BACKWARD, f.second);
    f.tab(TAB_BACKWARD, f.first);
    f.controller.activateEditControl(f.controller.editControls_.size());
    f.tab(TAB_BACKWARD, f.third);
    // Equal top: left wins; equal top-left: ledger index wins.
    f.place(f.first, 80, 0);
    f.place(f.second, 40, 0);
    f.place(f.third, 40, 0);
    f.controller.activateEditControl(f.controller.editControls_.size());
    f.tab(TAB_FORWARD, f.third);
    f.tab(TAB_FORWARD, f.second);
    f.tab(TAB_FORWARD, f.first);
    f.tab(TAB_BACKWARD, f.second);
    f.tab(TAB_BACKWARD, f.third);
  }
  void tabLanding()
  {
    TabFixture f;
    f.tab(TAB_FORWARD, f.first);
    f.expect(&f.first);
    TEHandle old = f.controller.editControls_.focused()->te;
    GrafPort ambient;
    GrafPtr before = 0, after = 0;
    GetPort(&before);
    SetPort(&ambient);
    f.tab(TAB_FORWARD, f.second);
    TEHandle te = f.controller.editControls_.focused()->te;
    GetPort(&after);
    LOKA_VERIFY(after == &ambient);
    SetPort(before);
    LOKA_VERIFY(!(**old).active && (**te).active);
    const unsigned beeps = toolbox_host::beeps;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.replacement.get().equals(String::Literal("x")));
    LOKA_VERIFY(f.text.get().equals(String::Literal("a")));
    LOKA_VERIFY(f.thirdText.get().equals(String::Literal("ccc")));
    LOKA_VERIFY(toolbox_host::beeps == beeps);
    f.tab(TAB_BACKWARD, f.first);
    te = f.controller.editControls_.focused()->te;
    LOKA_VERIFY((**te).selStart == 0 && (**te).selEnd == (**te).teLength);
    f.tab(TAB_FORWARD, f.second);
    LOKA_VERIFY(f.focus.state()->get().is(FIRST));
    f.focus.post(FIRST);
    f.expect(&f.second); // Published first + native move consumes the post.
    TabFixture fresh;
    fresh.tab(TAB_FORWARD, fresh.first);
    fresh.focus.post(SECOND);
    {
      loka::core::Operation turn;
      fresh.app.present(ACTIVATION_FOREGROUND, turn);
    }
    fresh.expect(&fresh.second); // No publication yet: the post wins.
  }
  void tabEligibility()
  {
    TabFixture f;
    f.controller.editControls_[1].usedThisFrame = false;
    f.tab(TAB_FORWARD, f.second);
    f.controller.editControls_[1].usedThisFrame = true;
    SetRect(&f.controller.editControls_[1].rect, 0, 0, 0, 0);
    f.tab(TAB_BACKWARD, f.third);
    f.place(f.first, 0, 0);
    TEHandle te = f.controller.editControls_[1].te;
    f.controller.editControls_[1].te = 0;
    f.tab(TAB_FORWARD, f.second);
    f.controller.editControls_[1].te = te;
    f.place(f.first, 0, -40);
    f.tab(TAB_BACKWARD, f.third);
    f.place(f.first, 0, 0);
    LifecycleFactTestAccess::MarkSubtreeRetired(&f.first);
    f.tab(TAB_FORWARD, f.second);
    f.controller.editControls_[0].usedThisFrame = false;
    te = f.controller.editControls_.focused()->te;
    TESetSelect((**te).teLength, (**te).teLength, te);
    f.tab(TAB_FORWARD, f.second); // Single candidate wraps to itself.
    te = f.controller.editControls_.focused()->te;
    LOKA_VERIFY((**te).selStart == 0 && (**te).selEnd == (**te).teLength);
    f.controller.editControls_[2].usedThisFrame = false;
    LOKA_VERIFY(!ToolboxInputDoor::tabKey(f.controller, TAB_FORWARD));
    ToolboxWindow *window = f.controller.window_;
    f.controller.window_ = 0;
    LOKA_VERIFY(!ToolboxInputDoor::tabKey(f.controller, TAB_FORWARD));
    f.controller.window_ = window;
  }
  void tabFallback()
  {
    TabFixture f;
    f.controller.retireEditTextControl(f.context(f.first), NATIVE_HINT_EAGER_RELEASE);
    f.hit(f.first);
    f.click();
    f.tab(TAB_FORWARD, f.second);
    f.expect(&f.second);
    f.click();
    f.tab(TAB_BACKWARD, f.third);
    f.controller.editControls_[0].usedThisFrame = false;
    f.controller.editControls_[1].usedThisFrame = false;
    f.click();
    LOKA_VERIFY(!ToolboxInputDoor::tabKey(f.controller, TAB_FORWARD));
  }
  void tabDecoder()
  {
    for (unsigned i = 0; i < 256; ++i)
      for (unsigned shift = 0; shift < 2; ++shift)
      {
        ToolboxTabDirection direction = TAB_BACKWARD;
        const bool decoded = QueryToolboxTabKey(static_cast<unsigned char>(i), shift != 0, direction);
        LOKA_VERIFY(decoded == (i == 9));
        LOKA_VERIFY(direction == (i == 9 && !shift ? TAB_FORWARD : TAB_BACKWARD));
      }
  }
  void postedNative()
  {
    Fixture f;
    f.native(f.first);
    f.native(f.second, 80);
    f.click();
    f.expect(&f.first);
    TEHandle oldTE = f.controller.editControls_[0].te;
    TEHandle target = f.controller.editControls_[1].te;
    TESetSelect(0, 1, target);
    TEScroll(0, -7, target);
    const Rect dest = (**target).destRect;
    const int selections = toolbox_host::selections;
    GrafPtr before = 0;
    GetPort(&before);
    GrafPort ambient = {{0, 0, {0, 0, 0, 0}}, 7, 12, 0, blackColor, HostPortState(), 0};
    SetPort(&ambient);
    f.focus.post(SECOND);
    {
      loka::core::Operation turn;
      f.app.present(ACTIVATION_FOREGROUND, turn);
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    }
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    LOKA_VERIFY(!loka::core::Operation::isSettling());
    LOKA_VERIFY(!(**oldTE).active && (**target).active);
    LOKA_VERIFY(f.controller.editControls_.focused() == &f.controller.editControls_[1]);
    LOKA_VERIFY((**target).selStart == 0 && (**target).selEnd == 1);
    LOKA_VERIFY(EqualRect(&dest, &(**target).destRect));
    LOKA_VERIFY(toolbox_host::selections == selections);
    LOKA_VERIFY(f.text.get().equals(String::Literal("a")));
    GrafPtr after = 0;
    GetPort(&after);
    LOKA_VERIFY(after == &ambient);
    SetPort(before);
    LOKA_VERIFY(toolbox_host::activationPort == f.nativeWindow.window());
    LOKA_VERIFY(toolbox_host::deactivationPort == f.nativeWindow.window());
    f.expect(&f.second);
    f.click();
    f.expect(&f.first);
    LOKA_VERIFY((**oldTE).active && !(**target).active);
  }
  void postedEditor()
  {
    TabFixture f;
    ObservableList<String> lines;
    Reported<LineCursor> cursor;
    StateBatchBase::CreateImmediateState(&f, cursor, LineCursor::None());
    LOKA_VERIFY(lines.attach(f.tracker()->asPushTracker(), 16) == ATTACH_OK);
    for (int i = 0; i < 10; ++i)
      LOKA_VERIFY(lines.insert(i, String::Literal("abcdef")) == EDIT_OK);
    TextEditorNode editor(TextEditorProps(lines, cursor).focusedAs(f.focus, SECOND));
    // Use the editor as the second key, with no duplicate attached binding.
    f.retire(f.second);
    f.attach(editor);
    LayoutState layout;
    layout.x = 80; layout.y = 0; layout.width = 80; layout.height = 40;
    LOKA_VERIFY(RegisterToolboxBuiltInSupport(f.controller));
    IPlatformNodeHandler *handler = f.controller.nodeHandlerRegistry_.find(&editor);
    LOKA_VERIFY(handler);
    ToolboxTextEditorContext *context = static_cast<ToolboxTextEditorContext *>(
        handler->ensureContext(&editor, &f.controller, layout));
    LOKA_VERIFY(context);
    layout.y = 40;
    context->layout(&f.controller, layout);
    context->render(&f.controller);
    TEHandle te = loka::testing::ToolboxTextEditorAccess::te(*context);
    LOKA_VERIFY(te);
    std::size_t editorIndex = 0;
    LOKA_VERIFY(f.controller.editControls_.find(context, editorIndex));
    LOKA_VERIFY(f.controller.editControls_[editorIndex].rect.top > f.controller.editControls_[1].rect.top);
    LOKA_VERIFY(f.controller.editControls_[editorIndex].rect.top < f.controller.editControls_[0].rect.top);
    f.controller.activateEditControl(1);
    f.tab(TAB_FORWARD, f.third); // The editor between the fields is not a target.
    TESetSelect(8, 10, te);
    TEScroll(0, -16, te);
    const Rect dest = (**te).destRect;
    const LineCursor before = cursor.state()->get();
    const ListRevision revision = lines.revision().get();
    f.focus.post(SECOND);
    {
      loka::core::Operation turn;
      f.app.present(ACTIVATION_FOREGROUND, turn);
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    }
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    LOKA_VERIFY(!loka::core::Operation::isSettling());
    LOKA_VERIFY(f.focus.state()->get().is(SECOND));
    LOKA_VERIFY((**te).active);
    LOKA_VERIFY((**te).selStart == 8 && (**te).selEnd == 10);
    LOKA_VERIFY(EqualRect(&dest, &(**te).destRect));
    LOKA_VERIFY(cursor.state()->get() == before);
    LOKA_VERIFY(lines.revision().get().content == revision.content);
    Point click = {45, 85};
    LOKA_VERIFY(ToolboxInputDoor::mouseDown(f.controller, click));
    LOKA_VERIFY((**te).selStart == (**te).selEnd);
    LOKA_VERIFY(!ToolboxInputDoor::tabKey(f.controller, TAB_FORWARD));
    LOKA_VERIFY(!ToolboxInputDoor::tabKey(f.controller, TAB_BACKWARD));
    const long length = (**te).teLength;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 0x09));
    LOKA_VERIFY((**te).teLength == length + 1);
    LOKA_VERIFY((**te).text.find('\t') != std::string::npos);
    LifecycleFactTestAccess::MarkSubtreeRetired(&editor);
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    editor.setContext(0);
  }
  void admissionGuards()
  {
    Fixture f;
    f.native(f.first);
    f.native(f.second, 80);
    f.click();
    f.expect(&f.first);
    toolbox_host::frontWindow = 0;
    LOKA_VERIFY(!f.controller.applyNativeFocus(*f.context(f.second)));
    toolbox_host::frontWindow = f.nativeWindow.window();
    f.controller.editControls_[1].usedThisFrame = false;
    LOKA_VERIFY(!f.controller.applyNativeFocus(*f.context(f.second)));
    f.controller.editControls_[1].usedThisFrame = true;
    const Rect rect = f.controller.editControls_[1].rect;
    SetRect(&f.controller.editControls_[1].rect, 0, 0, 0, 0);
    LOKA_VERIFY(!f.controller.applyNativeFocus(*f.context(f.second)));
    f.controller.editControls_[1].rect = rect;
    const TEHandle te = f.controller.editControls_[1].te;
    f.controller.editControls_[1].te = 0;
    LOKA_VERIFY(!f.controller.applyNativeFocus(*f.context(f.second)));
    f.controller.editControls_[1].te = te;
    ToolboxEditTextContext stale(&f.second);
    LOKA_VERIFY(!f.controller.applyNativeFocus(stale));
    ToolboxScenePlatformController foreign(&f.nativeWindow);
    LOKA_VERIFY(!foreign.applyNativeFocus(*f.context(f.second)));
    f.expect(&f.first);
    LifecycleFactTestAccess::MarkSubtreeRetired(&f.second);
    LOKA_VERIFY(!f.controller.applyNativeFocus(*f.context(f.second)));
  }
  void postedFallback()
  {
    Fixture f;
    f.second.props.text(f.replacement);
    f.native(f.first);
    f.hit(f.second, 80);
    f.click();
    f.expect(&f.first);
    TEHandle native = f.controller.editControls_[0].te;
    f.focus.post(SECOND);
    {
      loka::core::Operation turn;
      f.app.present(ACTIVATION_FOREGROUND, turn);
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    }
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    LOKA_VERIFY(!loka::core::Operation::isSettling());
    LOKA_VERIFY(!(**native).active);
    LOKA_VERIFY(!f.controller.editControls_.focused());
    f.expect(&f.second);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.replacement.get().equals(String::Literal("bx")));
    f.focus.post(FIRST);
    {
      loka::core::Operation turn;
      f.app.present(ACTIVATION_FOREGROUND, turn);
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    }
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    LOKA_VERIFY(!loka::core::Operation::isSettling());
    f.expect(&f.first);
    f.controller.retireEditTextControlAt(0, NATIVE_HINT_EAGER_RELEASE);
    // Retiring the native destination must not resurrect the old fallback.
    LOKA_VERIFY(!ToolboxInputDoor::keyDown(f.controller, 'y'));
    LOKA_VERIFY(f.replacement.get().equals(String::Literal("bx")));
  }
  void postedRefusal()
  {
    Fixture f;
    f.native(f.first);
    f.click();
    f.expect(&f.first);
    SetRect(&f.controller.projectionClip, 0, 0, 40, 20);
    LOKA_VERIFY(!f.controller.ensureEditTextControl(f.context(f.second), f.rect(80),
                                                  f.text.state(), NATIVE_HINT_DEFAULT).te);
    f.hit(f.second, 80);
    f.focus.post(SECOND);
    {
      loka::core::Operation turn;
      f.app.present(ACTIVATION_FOREGROUND, turn);
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    }
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    LOKA_VERIFY(!loka::core::Operation::isSettling());
    f.expect(&f.first);
    // Materializing later cannot replay the refused, consumed request.
    SetRect(&f.controller.projectionClip, 0, 0, 200, 200);
    f.native(f.second, 80);
    {
      loka::core::Operation turn;
      f.app.present(ACTIVATION_FOREGROUND, turn);
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    }
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    LOKA_VERIFY(!loka::core::Operation::isSettling());
    f.expect(&f.first);
  }
  void renderClick(void *data)
  {
    Fixture *f = static_cast<Fixture *>(data);
    f->click(80);
  }
  void completion()
  {
    Fixture f;
    f.hit(f.first);
    f.hit(f.second, 80);
    f.click();
    f.expect(&f.first);
    f.nativeWindow.onFlush = &renderClick;
    f.nativeWindow.flushData = &f;
    {
      loka::core::Operation turn;
      f.app.present(ACTIVATION_BACKGROUND, turn);
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    }
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    LOKA_VERIFY(!loka::core::Operation::isSettling());
    LOKA_VERIFY(f.focus.state()->get().is(FIRST));
    {
      loka::core::Operation turn;
      f.app.present(ACTIVATION_FOREGROUND, turn);
      LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    }
    LOKA_VERIFY(!loka::core::testing::OperationTestAccess::active());
    LOKA_VERIFY(!loka::core::Operation::isSettling());
    LOKA_VERIFY(f.focus.state()->get().is(SECOND));
  }
  void endpointDeath()
  {
    Fixture f;
    {
      ToolboxScenePlatformController reader(&f.nativeWindow);
      reader.recordEditHit(f.rect(), f.text.state(), 0, f.context(f.first));
      Point p = {5, 5};
      LOKA_VERIFY(ToolboxInputDoor::mouseDown(reader, p));
      LOKA_VERIFY(SceneFocusTestAccess::sourced(*f.first.asFocusParticipant()));
    }
    LOKA_VERIFY(!SceneFocusTestAccess::sourced(*f.first.asFocusParticipant()));
    EditTextNode *field = new EditTextNode(EditTextProps(f.text));
    f.project(*field);
    f.hit(*field);
    f.click();
    delete field;
    f.expect(0);
    LOKA_VERIFY(!ToolboxInputDoor::keyDown(f.controller, 'x'));
  }
  void nativeFocus()
  {
    Fixture f;
    f.native(f.first);
    f.click();
    f.expect(&f.first);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.text.get().equals(String::Literal("xa")));
    f.controller.editControls_.clearFocus();
    f.expect(0);
    f.click();
    f.controller.retireEditTextBinding(f.controller.editControls_[0], NATIVE_HINT_EAGER_RELEASE);
    f.controller.editControls_.clear();
    f.expect(0);
    f.native(f.first);
    f.click();
    f.controller.retireEditTextControlAt(0, NATIVE_HINT_EAGER_RELEASE);
    f.expect(0);
    f.native(f.first);
    f.click();
    f.retire(f.first);
    f.expect(0);
    LOKA_VERIFY(!ToolboxInputDoor::keyDown(f.controller, 'x'));
  }
  void sharedTextKey()
  {
    Fixture f;
    f.hit(f.first);
    f.click();
    f.hit(f.second, 80);
    f.second.props.text(f.replacement);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.text.get().equals(String::Literal("ax")));
    LOKA_VERIFY(f.replacement.get().equals(String::Literal("b")));
  }
  void fallback()
  {
    Fixture f;
    toolbox_host::failNew = 1;
    LOKA_VERIFY(!f.controller.ensureEditTextControl(f.context(f.first), f.rect(), f.text.state(), NATIVE_HINT_DEFAULT).te);
    f.hit(f.first);
    f.click();
    f.expect(&f.first);
    f.expect(&f.first);
    toolbox_host::frontWindow = 0;
    NodeContext *out = 0;
    LOKA_VERIFY(!f.controller.readNativeFocus(out));
    f.app.reconcileFocus();
    LOKA_VERIFY(f.focus.state()->get().is(FIRST));
    toolbox_host::frontWindow = f.nativeWindow.window();
    // Reprojecting another field with the same text must not change identity.
    f.hit(f.second, 80);
    f.expect(&f.first);
    f.second.props.text(f.replacement);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.text.get().equals(String::Literal("ax")));
    LOKA_VERIFY(f.replacement.get().equals(String::Literal("b")));
    // Current props, not cached text state, are the key delivery source.
    f.first.props.text(f.replacement);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'y'));
    LOKA_VERIFY(f.replacement.get().equals(String::Literal("by")));
    // Moving the hit before retirement cannot leave a dangling text target.
    f.hit(f.first, 160);
    f.retire(f.first);
    f.expect(0);
    LOKA_VERIFY(!ToolboxInputDoor::keyDown(f.controller, 'z'));
  }
  void nativeWins()
  {
    Fixture f;
    f.hit(f.first);
    f.click();
    f.native(f.second, 80);
    f.click(80);
    f.expect(&f.second);
    f.controller.retireEditTextControlAt(0, NATIVE_HINT_EAGER_RELEASE);
    f.expect(0);
  }
  void promotion()
  {
    Fixture f;
    f.hit(f.first);
    f.click();
    f.native(f.first);
    f.expect(0);
    LOKA_VERIFY(!ToolboxInputDoor::keyDown(f.controller, 'x'));
  }
  void blankClick()
  {
    Fixture f;
    f.hit(f.first);
    f.click();
    f.expect(&f.first);
    Point outside = {1000, 1000};
    LOKA_VERIFY(!ToolboxInputDoor::mouseDown(f.controller, outside));
    f.expect(0);
    LOKA_VERIFY(!ToolboxInputDoor::keyDown(f.controller, 'x'));
  }
  void clipping()
  {
    Fixture f;
    f.hit(f.first);
    f.click();
    SetRect(&f.controller.projectionClip, 200, 200, 220, 220);
    f.hit(f.second, 80);
    f.expect(&f.first);
    f.hit(f.first);
    f.expect(0);
    LOKA_VERIFY(!ToolboxInputDoor::keyDown(f.controller, 'x'));
  }
  class RefusableText : public loka::platform::String
  {
  public:
    bool refuse;
    mutable unsigned reads;
    std::string bytes;
    explicit RefusableText(const std::string &value) : refuse(false), reads(0), bytes(value) {}
    virtual bool appendUtf8(std::string &out) const
    { ++this->reads; out += this->bytes; return !this->refuse; }
  };
  void write(Fixture &f, const String &value)
  { StateTrackerGuard guard(f.tracker()); f.text.set(value); }
  void ordinaryRoundTrip()
  {
    toolbox_host::systemScript = toolbox_host::keyboardScript = smRoman;
    Fixture f;
    write(f, String::Literal("caf\xC3\xA9"));
    f.native(f.first);
    f.controller.activateEditControl(0);
    TEHandle te = f.controller.editControls_[0].te;
    LOKA_VERIFY((**te).text == std::string("caf\x8E", 4) && (**te).selStart == 4);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, static_cast<char>(0x8E)));
    LOKA_VERIFY(f.text.get().equals(String::Literal("caf\xC3\xA9\xC3\xA9")));
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 8));
    LOKA_VERIFY(f.text.get().equals(String::Literal("caf\xC3\xA9")));
    // Tab here describes the no-candidate fallback: focused EditText consumes it.
    const int controls[] = {13, 3, 9, 27, 0x10, 0};
    const int sets = toolbox_host::sets;
    for (unsigned i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i)
      LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, static_cast<char>(controls[i])));
    LOKA_VERIFY((**te).text == std::string("caf\x8E", 4) && toolbox_host::sets == sets);
    for (int script = 0; script < 2; ++script)
    {
      toolbox_host::systemScript = script ? 0 : 1;
      toolbox_host::keyboardScript = script ? 1 : 0;
      const unsigned beeps = toolbox_host::beeps;
      LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, static_cast<char>(0xC9)));
      LOKA_VERIFY(toolbox_host::beeps == beeps + 1 && (**te).text == std::string("caf\x8E", 4));
    }
    toolbox_host::systemScript = toolbox_host::keyboardScript = smRoman;
    write(f, String::Literal("abcd")); f.native(f.first);
    TESetSelect(1, 1, te);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 0x7F));
    LOKA_VERIFY(f.text.get().equals(String::Literal("acd")) && (**te).selStart == 1);
    TESetSelect(1, 3, te);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 0x7F));
    LOKA_VERIFY(f.text.get().equals(String::Literal("a")));
    const unsigned beeps = toolbox_host::beeps;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 0x7F));
    LOKA_VERIFY(toolbox_host::beeps == beeps && f.text.get().equals(String::Literal("a")));
    write(f, String(std::string(32767, 'x'))); f.native(f.first);
    const int fullSets = toolbox_host::sets;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'y'));
    LOKA_VERIFY((**te).teLength == 32767 && toolbox_host::beeps == beeps + 1);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 8));
    LOKA_VERIFY((**te).teLength == 32766 && toolbox_host::sets == fullSets);
    write(f, String(std::string(32768, 'z'))); f.native(f.first);
    LOKA_VERIFY((**te).teLength == 32766 && toolbox_host::sets == fullSets);
    write(f, String(std::string(32767, 'x'))); f.native(f.first);
    TESetSelect(0, 32767, te);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'y'));
    LOKA_VERIFY((**te).text == "y");
    toolbox_host::systemScript = 1;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'z'));
    LOKA_VERIFY(f.text.get().equals(String::Literal("yz")));
    toolbox_host::systemScript = 0;
    write(f, String::Literal("\xE6\xBC\xA2\xE5\xAD\x97")); f.native(f.first);
    LOKA_VERIFY((**te).text == "??");
    const String lossy = f.text.get();
    const unsigned beforeBeep = toolbox_host::beeps;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(toolbox_host::beeps == beforeBeep + 1 && f.text.get().equals(lossy));
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 28));
    LOKA_VERIFY((**te).selStart == 1 && f.text.get().equals(lossy));
    // A verified empty installation is not a never-installed row.
    ToolboxEditInstalled none;
    LOKA_VERIFY(!none.holds(String()));
    write(f, String()); f.native(f.first);
    LOKA_VERIFY(f.controller.editControls_[0].installed.holds(String()));
    f.controller.retireEditTextBinding(f.controller.editControls_[0], NATIVE_HINT_DEFAULT);
    LOKA_VERIFY(!f.controller.editControls_[0].installed.holds(String()));
    f.controller.editControls_.clear(); f.controller.flushTE();
    f.native(f.first);
    LOKA_VERIFY((**f.controller.editControls_[0].te).text.empty());
    std::puts("ordinary round trip, key classification, capacity, certificate pins passed");
  }
  void ordinaryRefusal()
  {
    Fixture f; f.native(f.first); f.controller.activateEditControl(0);
    TEHandle te = f.controller.editControls_[0].te;
    RefusableText *source = new RefusableText("B");
    const String refused = String::FromPlatform(Managed<loka::platform::String>::Wrap(source));
    write(f, refused); source->refuse = true;
    f.native(f.first);
    const unsigned beeps = toolbox_host::beeps;
    LOKA_VERIFY((**te).text == "a" && f.controller.editControls_[0].installed.holds(String::Literal("a")));
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY((**te).text == "a" && toolbox_host::beeps == beeps + 1);
    source->refuse = false;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY((**te).text == "Bx" && f.text.get().equals(String::Literal("Bx")));
    // Same row, new props and seat, refused collection: the old seat is never written.
    { StateTrackerGuard guard(f.tracker()); f.replacement.set(refused); }
    source->refuse = true;
    f.first.props.text(f.replacement); f.native(f.first);
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'q'));
    LOKA_VERIFY(f.text.get().equals(String::Literal("Bx")) && (**te).text == "Bx");
    source->refuse = false;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'q'));
    LOKA_VERIFY(f.replacement.get().equals(String::Literal("Bq")) && f.text.get().equals(String::Literal("Bx")));
    std::puts("refusal recovery without State write and props rebind pins passed");
  }
  void ordinaryNoPublication()
  {
    Fixture f;
    RefusableText *source = new RefusableText("z");
    const String logical = String::FromPlatform(Managed<loka::platform::String>::Wrap(source));
    write(f, logical); f.native(f.first); f.controller.activateEditControl(0);
    TEHandle te = f.controller.editControls_[0].te;
    source->reads = 0;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 28));
    LOKA_VERIFY(source->reads == 0);
    // Admission collects once and decoded-vs-before equality collects once.
    // Keep the transaction open: a redundant seat.set marks its tracker dirty
    // even when State equality suppresses observer notification.
    source->reads = 0;
    { StateTrackerGuard guard(f.tracker());
      LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 8));
      LOKA_VERIFY(!static_cast<PushStateTracker *>(f.tracker())->transactionDirty()); }
    LOKA_VERIFY(source->reads == 2);
    f.controller.editControls_[0].installed.commit(logical);
    source->reads = 0; TESetSelect(0, 1, te);
    { StateTrackerGuard guard(f.tracker());
      LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'z'));
      LOKA_VERIFY(!static_cast<PushStateTracker *>(f.tracker())->transactionDirty()); }
    LOKA_VERIFY(source->reads == 2 && (**te).selStart == 1);
    std::puts("arrows, start Backspace and same-byte replacement never enter the seat");
  }
  void refuseAfterKey(TEHandle te)
  {
    (**te).teLength = -1;
    toolbox_host::afterKey = 0;
  }
  void refuseReadAfterKey(TEHandle)
  { toolbox_host::refuseReads = 1; toolbox_host::afterKey = 0; }
  void ordinaryBoundedRepair()
  {
    Fixture f; f.native(f.first); f.controller.activateEditControl(0);
    TEHandle te = f.controller.editControls_[0].te;
    f.controller.editControls_[0].installed.revoke();
    const int sets = toolbox_host::sets;
    toolbox_host::afterKey = refuseAfterKey;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(toolbox_host::sets == sets + 2 && (**te).text == "a");
    LOKA_VERIFY(f.text.get().equals(String::Literal("a")));
    toolbox_host::afterKey = refuseReadAfterKey;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.text.get().equals(String::Literal("a")) && (**te).text == "a");
    f.controller.editControls_[0].installed.revoke();
    toolbox_host::corruptSets = 1;
    f.controller.syncEditTextFromState(f.controller.editControls_[0]);
    LOKA_VERIFY(!f.controller.editControls_[0].installed.holds(f.text.get()));
    LOKA_VERIFY((**te).teLength == 1 && (**te).text != "a");
    f.controller.syncEditTextFromState(f.controller.editControls_[0]);
    // Pool a nonempty native record, then reuse it for a certified empty value.
    f.controller.retireEditTextControl(f.context(f.first), NATIVE_HINT_DEFAULT);
    f.controller.flushTE(); write(f, String()); f.native(f.first);
    LOKA_VERIFY(f.controller.editControls_[0].te == te && (**te).text.empty());
    LOKA_VERIFY(f.controller.editControls_[0].installed.holds(String()));
  }
  struct SyncEdits
  {
    ToolboxScenePlatformController *controller;
    explicit SyncEdits(ToolboxScenePlatformController &c) : controller(&c) {}
    static void changed(void *data)
    {
      SyncEdits &self = *static_cast<SyncEdits *>(data);
      for (std::size_t i = 0; i < self.controller->editControls_.size(); ++i)
        self.controller->syncEditTextFromState(self.controller->editControls_[i]);
    }
  };
  void ordinaryFanout()
  {
    Fixture f; write(f, String::Literal("abcd")); f.native(f.first); f.native(f.second, 80);
    f.controller.activateEditControl(0);
    TEHandle source = f.controller.editControls_[0].te;
    TESetSelect(2, 2, source);
    SyncEdits sync(f.controller);
    f.text.state()->bind(SyncEdits::changed, &sync);
    const int sets = toolbox_host::sets;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'X'));
    LOKA_VERIFY((**source).text == "abXcd" && (**source).selStart == 3);
    LOKA_VERIFY((**f.controller.editControls_[1].te).text == "abXcd");
    LOKA_VERIFY(toolbox_host::sets == sets + 1);
    f.text.state()->unbind(SyncEdits::changed, &sync);
  }
  void ordinaryDecodeRepair()
  {
    Fixture f;
    write(f, String::Literal("caf\xC3\xA9")); f.native(f.first); f.controller.activateEditControl(0);
    TEHandle te = f.controller.editControls_[0].te;
    toolbox_host::systemScript = 1;
    const unsigned beeps = toolbox_host::beeps;
    const int sets = toolbox_host::sets;
    f.controller.pendingDirtyRects_.clear();
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.text.get().equals(String::Literal("caf\xC3\xA9")) && (**te).text == "caf?");
    LOKA_VERIFY(toolbox_host::beeps == beeps + 1 && toolbox_host::sets == sets + 1);
    LOKA_VERIFY(!f.controller.pendingDirtyRects_.empty());
    toolbox_host::systemScript = smRoman;
    // Failed readback clears the certificate; no absent certificate may skip retry.
    f.controller.editControls_[0].installed.revoke();
    toolbox_host::failSets = 1;
    f.controller.syncEditTextFromState(f.controller.editControls_[0]);
    LOKA_VERIFY(!f.controller.editControls_[0].installed.holds(f.text.get()));
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.text.get().equals(String::Literal("caf\xC3\xA9x")));
    std::puts("decode refusal preserves State, repairs native text and records damage");
  }
  RefusableText *gRepairSource = 0;
  void refuseReadAndRepairAfterKey(TEHandle)
  {
    toolbox_host::refuseReads = 1;
    gRepairSource->refuse = true;
    toolbox_host::afterKey = 0;
  }
  void ordinaryFailedRepair()
  {
    Fixture f;
    RefusableText *source = new RefusableText("ab");
    const String value = String::FromPlatform(Managed<loka::platform::String>::Wrap(source));
    write(f, value); f.native(f.first); f.controller.activateEditControl(0);
    TEHandle te = f.controller.editControls_[0].te;
    LOKA_VERIFY((**te).text == "ab");
    gRepairSource = source;
    toolbox_host::afterKey = refuseReadAndRepairAfterKey;
    const unsigned beeps = toolbox_host::beeps;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    // Neither the readback nor the repair committed; the unaccepted "abx" must not stay visible.
    LOKA_VERIFY(f.text.get().equals(value) && toolbox_host::beeps == beeps + 1);
    LOKA_VERIFY((**te).text.empty() && !f.controller.editControls_[0].installed.holds(value));
    source->refuse = false;
    gRepairSource = 0;
    std::puts("a refused repair degrades the native projection to empty, never to the unaccepted edit");
  }
  void ordinaryFallback()
  {
    Fixture f; f.hit(f.first); f.click();
    write(f, String());
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, static_cast<char>(0x8E)));
    LOKA_VERIFY(f.text.get().equals(String::Literal("\xC3\xA9")));
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 0x7F));
    LOKA_VERIFY(f.text.get().equals(String::Literal("\xC3\xA9")));
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 8));
    LOKA_VERIFY(f.text.get().equals(String()));
    const char *bad[] = {"\x80", "\xC0\xAF", "\xE2\x82", "\xED\xA0\x80"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
      const String value(bad[i]); write(f, value);
      const unsigned beeps = toolbox_host::beeps;
      LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 8));
      LOKA_VERIFY(f.text.get().equals(value) && toolbox_host::beeps == beeps + 1);
    }
    RefusableText *source = new RefusableText("partial");
    const String value = String::FromPlatform(Managed<loka::platform::String>::Wrap(source));
    write(f, value); source->refuse = true;
    const unsigned beeps = toolbox_host::beeps;
    LOKA_VERIFY(ToolboxInputDoor::keyDown(f.controller, 'x'));
    LOKA_VERIFY(f.text.get().equals(value) && toolbox_host::beeps == beeps + 1);
    std::puts("fallback scalar insertion/deletion and refusal pins passed");
  }
} // namespace
int main(int argc, char **argv)
{
  if (argc == 2)
  {
    if (std::strcmp(argv[1], "tab-geometry") == 0)
    {
      tabGeometry();
      return 0;
    }
    if (std::strcmp(argv[1], "tab-landing") == 0)
    {
      tabLanding();
      return 0;
    }
    if (std::strcmp(argv[1], "tab-eligibility") == 0)
    {
      tabEligibility();
      return 0;
    }
    if (std::strcmp(argv[1], "tab-fallback") == 0)
    {
      tabFallback();
      return 0;
    }
    if (std::strcmp(argv[1], "tab-decoder") == 0)
    {
      tabDecoder();
      return 0;
    }
    if (std::strcmp(argv[1], "tab-editor") == 0)
    {
      postedEditor();
      return 0;
    }
  }
  if (argc == 2 && std::strcmp(argv[1], "shared-text-key") == 0)
  {
    sharedTextKey();
    return 0;
  }
  tabGeometry();
  tabLanding();
  tabEligibility();
  tabFallback();
  tabDecoder();
  ordinaryRoundTrip();
  ordinaryRefusal();
  ordinaryNoPublication();
  ordinaryBoundedRepair();
  ordinaryFanout();
  ordinaryDecodeRepair();
  ordinaryFailedRepair();
  ordinaryFallback();
  postedNative();
  admissionGuards();
  postedEditor();
  postedFallback();
  postedRefusal();
  sharedTextKey();
  nativeFocus();
  fallback();
  nativeWins();
  promotion();
  clipping();
  blankClick();
  completion();
  endpointDeath();
  std::puts("Toolbox focus host pins passed");
}
