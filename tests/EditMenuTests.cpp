#include "EditMenuTests.hpp"
#include "platform/null/context/NullTextEditorContext.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/context/NullEditTextContext.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/TextBufferFixture.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  class Root : public BoundaryNodeFor<Root>
  {
  public:
    explicit Root(const BoundaryPropsFor<Root> &p)
        : BoundaryNodeFor<Root>(p)
    {
    }
    virtual void composeNode(NodeComposition &) {}
  };
  struct Fixture
  {
    NullScenePlatformController controller;
    NullPlatformContext platform;
    NullWindow window;
    EditTextNode editor;
    EmitterState binding;
    unsigned calls;
    static WindowProps props()
    {
      WindowProps p;
      p.scene(new Scene(Boundary<Root>()));
      return p;
    }
    Fixture()
        : window(&platform, props(), &controller),
          editor(EditTextProps()),
          calls(0)
    {
      ComponentContext context;
      BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene());
      context.setBoundary(root);
      context.setStateOwner(root);
      context.setScene(window.scene());
      BoundaryNode::composeSubtree(&editor, context, COMPOSE_EVENT_ATTACH, root);
      LayoutState bounds;
      bounds.width = 200;
      bounds.height = 100;
      controller.projectLayoutForTesting(&editor, bounds);
      LOKA_VERIFY(editor.getContext());
      binding.bind(&count, this, false);
    }
    ~Fixture()
    {
      controller.menuAttachment().disconnect();
      binding.unbind(&count, this);
      LifecycleFactTestAccess::MarkSubtreeRetired(&editor);
      editor.setContext(0);
    }
    static void count(void *data)
    {
      ++static_cast<Fixture *>(data)->calls;
    }
    void open(MenuActionType action, bool bound = true)
    {
      MenuBarDefinition bar;
      bar << (Menu("Edit") << MenuItem("").actionType(action).onClick(bound ? &binding : 0));
      LOKA_VERIFY(controller.menuAttachment().open(bar));
    }
    void dispatch()
    {
      controller.menuAttachment().dispatch(1, &window);
    }
    const std::vector<EditVerb> &verbs()
    {
      return static_cast<NullEditTextContext *>(editor.getContext())->editCommands.verbs();
    }
  };
  void disconnect(void *data)
  {
    static_cast<NullMenuAttachment *>(data)->disconnect();
  }
} // namespace

void testEditMenuMappingExhaustive()
{
  const EditVerb expected[] = {EDIT_UNDO, EDIT_CUT, EDIT_COPY, EDIT_PASTE, EDIT_SELECT_ALL};
  unsigned edits = 0;
  for (int i = MENU_ACTION_NONE; i < MENU_ACTION_COUNT; ++i)
  {
    EditVerb verb = EDIT_UNDO;
    const bool mapped = menuEditVerb(static_cast<MenuActionType>(i), verb);
    const bool isEdit = i >= MENU_ACTION_EDIT_UNDO && i <= MENU_ACTION_EDIT_SELECT_ALL;
    LOKA_VERIFY(mapped == isEdit);
    if (mapped)
    {
      LOKA_VERIFY(edits < 5 && verb == expected[edits]);
      ++edits;
    }
  }
  LOKA_VERIFY(edits == 5);
}
void testEditMenuFocusedEditorSkipsBinding()
{
  Fixture f;
  f.open(MENU_ACTION_EDIT_UNDO);
  f.controller.simulateNativeFocus(f.editor.getContext());
  LOKA_VERIFY(f.window.handleEditCommand(EDIT_UNDO));
  f.dispatch();
  LOKA_VERIFY(f.calls == 0 && f.verbs().size() == 2);
  LOKA_VERIFY(!f.controller.borrowPhase().open());
}
void testEditMenuNoFocusRunsBindingOnce()
{
  Fixture f;
  f.open(MENU_ACTION_EDIT_PASTE);
  LOKA_VERIFY(!f.window.handleEditCommand(EDIT_PASTE));
  f.dispatch();
  LOKA_VERIFY(f.calls == 1 && f.verbs().empty());
}
void testEditMenuNoBindingIsNoOp()
{
  Fixture f;
  f.open(MENU_ACTION_EDIT_COPY, false);
  f.dispatch();
  LOKA_VERIFY(f.calls == 0 && f.verbs().empty());
}
void testEditMenuAllVerbsReachFocusedContext()
{
  Fixture f;
  f.controller.simulateNativeFocus(f.editor.getContext());
  const MenuActionType actions[] = {MENU_ACTION_EDIT_UNDO,
                                    MENU_ACTION_EDIT_CUT,
                                    MENU_ACTION_EDIT_COPY,
                                    MENU_ACTION_EDIT_PASTE,
                                    MENU_ACTION_EDIT_SELECT_ALL};
  const EditVerb expected[] = {EDIT_UNDO, EDIT_CUT, EDIT_COPY, EDIT_PASTE, EDIT_SELECT_ALL};
  for (unsigned i = 0; i < 5; ++i)
  {
    f.open(actions[i]);
    f.dispatch();
    LOKA_VERIFY(f.verbs().size() == i + 1 && f.verbs()[i] == expected[i]);
  }
  LOKA_VERIFY(f.calls == 0);
}
void testEditMenuRetiredFocusFallsBack()
{
  Fixture f;
  f.open(MENU_ACTION_EDIT_CUT);
  f.controller.simulateNativeFocus(f.editor.getContext());
  LifecycleFactTestAccess::MarkSubtreeRetired(&f.editor);
  f.dispatch();
  LOKA_VERIFY(f.calls == 1 && f.verbs().empty());
}
void testEditMenuBindingCanDisconnectAttachment()
{
  Fixture f;
  f.open(MENU_ACTION_EDIT_UNDO);
  f.binding.bind(&disconnect, &f.controller.menuAttachment(), false);
  f.dispatch();
  LOKA_VERIFY(f.calls == 1 && !f.controller.menuAttachment().connected());
  f.binding.unbind(&disconnect, &f.controller.menuAttachment());
}

void testEditMenuFacadeCanDisconnectAttachment()
{
  class DisconnectingWindow : public Window
  {
  public:
    NullMenuAttachment &attachment;
    DisconnectingWindow(PlatformContext *platform, NullMenuAttachment &menu)
        : Window(platform, WindowProps()),
          attachment(menu)
    {
    }
    virtual bool handleEditCommand(EditVerb)
    {
      this->attachment.disconnect();
      return false;
    }
  };
  Fixture f;
  f.open(MENU_ACTION_EDIT_COPY);
  DisconnectingWindow window(&f.platform, f.controller.menuAttachment());
  f.controller.menuAttachment().dispatch(1, &window);
  LOKA_VERIFY(f.calls == 1 && !f.controller.menuAttachment().connected());
}

void testEditMenuTextEditorRecordsVerb()
{
  Fixture f;
  loka::testing::TextBufferFixture lines;
  Reported<LineCursor> cursor;
  BoundaryNode *root = loka::dsl::testing::SceneTestAccess::rootBoundary(*f.window.scene());
  StateBatchBase::CreateImmediateState(root, cursor, LineCursor::None());
  LOKA_VERIFY(lines.attach(root->tracker()->asPushTracker(), 4) == ATTACH_OK);
  LOKA_VERIFY(lines.insert(0, String("edit")) == EDIT_OK);
  TextEditorNode editor(TextEditorProps(lines, cursor));
  editor.setPropsTypeId(TextEditorProps::staticTypeId());
  ComponentContext context;
  context.setBoundary(root);
  context.setStateOwner(root);
  context.setScene(f.window.scene());
  BoundaryNode::composeSubtree(&editor, context, COMPOSE_EVENT_ATTACH, root);
  LayoutState bounds;
  bounds.width = 200;
  bounds.height = 100;
  f.controller.projectLayoutForTesting(&editor, bounds);
  LOKA_VERIFY(editor.getContext());
  f.controller.simulateNativeFocus(editor.getContext());
  f.open(MENU_ACTION_EDIT_SELECT_ALL);
  f.dispatch();
  const std::vector<EditVerb> &verbs = static_cast<NullTextEditorContext *>(editor.getContext())->editCommands.verbs();
  LOKA_VERIFY(verbs.size() == 1 && verbs[0] == EDIT_SELECT_ALL && f.calls == 0);
  LifecycleFactTestAccess::MarkSubtreeRetired(&editor);
  editor.setContext(0);
}
