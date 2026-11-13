#include "TextDocumentViewTests.hpp"
#include "app/nodes/controls/TextDocumentEditor.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "app/nodes/nestable/Keyed.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullTextDocumentViewContext.hpp"
#include "support/TextDocumentAccess.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <cstdio>
#include <cstring>
#include "support/LifecycleFactTestAccess.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullPlatformContext.hpp"
namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::app::testing;
  typedef loka::dsl::testing::SceneTestAccess SceneAccess;
  unsigned refusedViewCreations = 0;
  /** A fixture definition that refuses both materialization doors while armed. */
  class ViewDefinition : public TextDocumentEditor
  {
  public:
    explicit ViewDefinition(TextDocument &document)
        : TextDocumentEditor(document)
    {
    }
    virtual NodeDefinitionBase *clone() const
    {
      return new ViewDefinition(*this);
    }
    virtual Node *create() const
    {
      if (refusedViewCreations)
      {
        --refusedViewCreations;
        return 0;
      }
      return TextDocumentEditor::create();
    }
    virtual Node *createInPlace(void *memory) const
    {
      if (refusedViewCreations)
      {
        --refusedViewCreations;
        return 0;
      }
      return TextDocumentEditor::createInPlace(memory);
    }
  };
  class ViewRoot : public BoundaryNodeFor<ViewRoot>
  {
  public:
    TextDocument first, second;
    Focus<int> focus, otherFocus;
    NodeState<TextViewAttachment> result, otherResult;
    NodeState<bool> shown;
    explicit ViewRoot(const BoundaryPropsFor<ViewRoot> &p)
        : BoundaryNodeFor<ViewRoot>(p)
    {
      this->document(this->first);
      this->document(this->second);
      this->state(this->focus);
      this->state(this->otherFocus);
      this->state(this->result, TextViewAttachment());
      this->state(this->otherResult, TextViewAttachment());
      this->state(this->shown, true);
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Show(*this->shown.state())
                << (Box() << ViewDefinition(this->first).attachment(this->result).focusedAs(this->focus, 1)));
    }
    NodeDefinitionBase *definition()
    {
      return this->composition().root();
    }
    bool reconcile(Node *node, NodeDefinitionBase &definition, Scene &scene, IPlatformController &controller)
    {
      ComponentContext context;
      context.setBoundary(this);
      context.setStateOwner(this);
      context.setScene(&scene);
      context.setPlatformController(&controller);
      return this->reconcileParkedBranch(context, node, &definition);
    }
  };
  TextDocumentEditorNode *editor(Node *node)
  {
    if (!node)
      return 0;
    if (node->nodeTypeKey() == NodeTypeToken<TextDocumentEditorNode>())
      return static_cast<TextDocumentEditorNode *>(node);
    INestable *parent = node->asNestable();
    for (Node *child = parent ? parent->childrenHead() : 0; child; child = child->nextInComposition)
    {
      TextDocumentEditorNode *found = editor(child);
      if (found)
        return found;
    }
    return 0;
  }
  Node *box(Node *node)
  {
    if (!node)
      return 0;
    if (node->asBoxNode())
      return node;
    INestable *parent = node->asNestable();
    for (Node *child = parent ? parent->childrenHead() : 0; child; child = child->nextInComposition)
    {
      Node *found = box(child);
      if (found)
        return found;
    }
    return 0;
  }
  unsigned calls(const NullScenePlatformController &controller, NullScenePlatformController::TextViewCall::Kind kind)
  {
    unsigned count = 0;
    for (std::size_t i = 0; i < controller.textViewCalls.size(); ++i)
      if (controller.textViewCalls[i].kind == kind)
        ++count;
    return count;
  }
  struct Fixture
  {
    NullScenePlatformController controller;
    Scene scene;
    Fixture()
        : scene(Boundary<ViewRoot>())
    {
      LOKA_VERIFY(this->scene.mount(&this->controller));
      SceneAccess::updateAttached(this->scene, true);
    }
    ~Fixture()
    {
      SceneAccess::unmount(this->scene);
    }
    ViewRoot &root()
    {
      return *static_cast<ViewRoot *>(SceneAccess::rootBoundary(this->scene));
    }
    void complete()
    {
      TextDocumentAccess::complete(this->scene.textDocuments(), this->controller);
    }
    void show(bool value)
    {
      {
        loka::core::StateTrackerGuard guard(this->root().tracker());
        this->root().shown.set(value);
      }
      this->scene.flushInvalidation();
    }
  };
  TextDocumentEditorProps *editorProps(NodeDefinitionBase *definition)
  {
    if (!definition)
      return 0;
    if (definition->propsBase() && definition->propsBase()->propsTypeId() == TextDocumentEditorProps::staticTypeId())
      return const_cast<TextDocumentEditorProps *>(
          static_cast<const TextDocumentEditorProps *>(definition->propsBase()));
    IBranchSeatDefinition *seat = definition->asBranchSeatDefinition();
    if (seat)
      for (unsigned i = 0; i < seat->armCount(); ++i)
      {
        TextDocumentEditorProps *found = editorProps(seat->armDefinition(i));
        if (found)
          return found;
      }
    INestableDefinition *parent = definition->asNestableDefinition();
    for (NodeDefinitionBase *child = parent ? parent->childrenHead() : 0; child; child = child->nextInComposition)
    {
      TextDocumentEditorProps *found = editorProps(child);
      if (found)
        return found;
    }
    return 0;
  }
  void install(NullScenePlatformController &controller, Scene &scene, TextDocumentEditorNode &node)
  {
    NullTextDocumentViewContext *context = new NullTextDocumentViewContext(controller, scene.textDocuments(), node);
    LOKA_VERIFY(node.adoptContext(context));
    context->readLifecycleFactOnAttach();
  }
  TextReplacement replacement(const char *value)
  {
    loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
    LOKA_VERIFY(bytes.tryAssign(reinterpret_cast<const unsigned char *>(value), std::strlen(value)));
    bytes.sealBytes();
    TextReplacement result;
    LOKA_VERIFY(TextReplacement::FromUtf8(bytes, TextPlacement::End(), result) == TEXT_DOCUMENT_OK);
    return result;
  }
  void countWrite(void *data)
  {
    ++*static_cast<unsigned *>(data);
  }
} // namespace
void testTextViewParkedRootRefuses()
{
  Fixture f;
  ViewRoot &root = f.root();
  TextDocumentEditorNode *old = editor(&root);
  LOKA_VERIFY(old && old->getContext());
  NodeContext *context = old->getContext();
  TextDocumentAccess::body(root.first, "preserved");
  NullTextStore *store = TextDocumentAccess::store(root.first);
  f.show(false);
  const unsigned attaches = calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH);
  TextDocumentEditor next(root.second);
  LOKA_VERIFY(!root.reconcile(old, next, f.scene, f.controller));
  LOKA_VERIFY(old->getContext() == context && old->props.document_ == &root.first);
  LOKA_VERIFY(old->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
  LOKA_VERIFY(store == TextDocumentAccess::store(root.first) && !store->view.place);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "preserved");
  LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH) == attaches);
}
void testTextViewParkedNestedReplacement()
{
  Fixture f;
  ViewRoot &root = f.root();
  Node *parent = box(&root);
  TextDocumentEditorNode *old = editor(parent);
  LOKA_VERIFY(parent && old && old->getContext());
  TextDocumentAccess::body(root.first, "old body");
  NullTextStore *store = TextDocumentAccess::store(root.first);
  f.show(false);
  const unsigned attaches = calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH);
  Box next;
  next << TextDocumentEditor(root.second).attachment(root.otherResult);
  LOKA_VERIFY(root.reconcile(parent, next, f.scene, f.controller));
  TextDocumentEditorNode *fresh = editor(parent);
  LOKA_VERIFY(fresh && fresh != old && fresh->props.document_ == &root.second);
  LOKA_VERIFY(old->lifecycleFact() == NODE_FACT_RETIRED && !old->getContext());
  LOKA_VERIFY(!store->view.place && TextDocumentAccess::store(root.first) == store);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "old body");
  f.scene.invalidate();
  f.scene.flushInvalidation();
  std::fprintf(stderr,
               "parked replacement: context=%d attached=%d attachDelta=%u fact=%d\n",
               fresh->getContext() != 0,
               TextDocumentAccess::store(root.second)->view.place != 0,
               calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH) - attaches,
               static_cast<int>(fresh->lifecycleFact()));
  LOKA_VERIFY(!fresh->getContext() && !TextDocumentAccess::store(root.second)->view.place);
  LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH) == attaches);
  TextDocumentEditorProps *declared = editorProps(root.definition());
  LOKA_VERIFY(declared);
  *declared = TextDocumentEditorProps(root.second).attachment(root.otherResult);
  f.show(true);
  f.complete();
  LOKA_VERIFY(editor(&root) == fresh && fresh->getContext());
  LOKA_VERIFY(TextDocumentAccess::store(root.second)->view.place == fresh->getContext());
  LOKA_VERIFY(root.otherResult.get().value() == TextViewAttachment::VIEW_ATTACHED);
}
void testTextViewCompletionSnapshot()
{
  Fixture f;
  ViewRoot &root = f.root();
  LOKA_VERIFY(root.result.get() == TextViewAttachment());
  LOKA_VERIFY(TextDocumentAccess::store(root.first)->view.place != 0);
  unsigned writes = 0;
  root.result.state()->bind(&countWrite, &writes, false);
  f.complete();
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED && writes == 1);
  f.complete();
  LOKA_VERIFY(writes == 1);
  f.show(false);
  f.show(true);
  f.complete();
  LOKA_VERIFY(writes == 1);
  f.show(false);
  f.complete();
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_DETACHED && writes == 2);
  f.show(true);
  f.complete();
  LOKA_VERIFY(writes == 3);
  root.result.state()->unbind(&countWrite, &writes);
}
void testTextViewOccupiedAndDeliveryOrder()
{
  for (unsigned order = 0; order != 2; ++order)
  {
    Fixture f;
    ViewRoot &root = f.root();
    TextDocumentEditorNode *first = editor(&root);
    TextDocumentEditorNode second(TextDocumentEditorProps(root.first).attachment(root.otherResult));
    install(f.controller, f.scene, second);
    f.complete();
    LOKA_VERIFY(root.otherResult.get().value() == TextViewAttachment::VIEW_REFUSED_OCCUPIED);
    const unsigned attached = calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH);
    f.show(false);
    second.getContext()->onPropsApplied();
    f.complete();
    LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH) == attached);
    LOKA_VERIFY(root.otherResult.get().value() == TextViewAttachment::VIEW_REFUSED_OCCUPIED);
    NotifySubtreeNodeDetached(&second);
    LifecycleFactTestAccess::DeliverFacts(&second);
    // First is logically returning, second logically leaving. Reverse delivery
    // order models both tree orders within one apply without an intermediate completion.
    NotifySubtreeNodeAttached(first);
    NotifySubtreeNodeAttached(&second);
    LifecycleFactTestAccess::DeliverFacts(&second);
    NotifySubtreeNodeDetached(&second);
    if (order)
      LifecycleFactTestAccess::DeliverFacts(&second);
    LifecycleFactTestAccess::DeliverFacts(first);
    if (!order)
      LifecycleFactTestAccess::DeliverFacts(&second);
    LOKA_VERIFY(TextDocumentAccess::store(root.first)->view.place == first->getContext());
    f.complete();
    LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  }
}
void testTextViewForeignAndUnavailable()
{
  Fixture f;
  ViewRoot &root = f.root();
  failTextStoreBirths(TEXT_STORE_REFUSED_ALLOCATION, 2);
  Scene other((Boundary<ViewRoot>()));
  LOKA_VERIFY(other.mount(&f.controller));
  SceneAccess::updateAttached(other, true);
  ViewRoot &otherRoot = *static_cast<ViewRoot *>(SceneAccess::rootBoundary(other));
  LOKA_VERIFY(!TextDocumentAccess::store(otherRoot.first));
  TextDocumentAccess::complete(other.textDocuments(), f.controller);
  LOKA_VERIFY(otherRoot.result.get().value() == TextViewAttachment::VIEW_REFUSED_UNAVAILABLE);
  {
    TextDocumentEditorNode foreign(TextDocumentEditorProps(otherRoot.first).attachment(root.otherResult));
    const unsigned before = calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH);
    install(f.controller, f.scene, foreign);
    f.complete();
    LOKA_VERIFY(root.otherResult.get().value() == TextViewAttachment::VIEW_REFUSED_FOREIGN);
    LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH) == before);
  }
  SceneAccess::unmount(other);
  {
    TextDocument undeclared;
    TextDocumentEditorNode unavailable(TextDocumentEditorProps(undeclared).attachment(root.otherResult));
    install(f.controller, f.scene, unavailable);
    f.complete();
    LOKA_VERIFY(root.otherResult.get().value() == TextViewAttachment::VIEW_REFUSED_UNAVAILABLE);
  }
  Fixture distinct;
  {
    Fixture sameOwner;
    // Same declaration membership, but a different view controller: isolate the second FOREIGN wall.
    TextDocumentEditorNode wrongRail(
        TextDocumentEditorProps(sameOwner.root().second).attachment(sameOwner.root().otherResult));
    const unsigned before = calls(distinct.controller, NullScenePlatformController::TextViewCall::ATTACH);
    install(distinct.controller, sameOwner.scene, wrongRail);
    sameOwner.complete();
    LOKA_VERIFY(sameOwner.root().otherResult.get().value() == TextViewAttachment::VIEW_REFUSED_FOREIGN);
    LOKA_VERIFY(calls(distinct.controller, NullScenePlatformController::TextViewCall::ATTACH) == before);
  }
  TextDocumentEditorNode foreignController(TextDocumentEditorProps(distinct.root().first).attachment(root.otherResult));
  install(f.controller, f.scene, foreignController);
  f.complete();
  LOKA_VERIFY(root.otherResult.get().value() == TextViewAttachment::VIEW_REFUSED_FOREIGN);
}
namespace
{
  class UnsupportedController : public NullScenePlatformController
  {
  public:
    virtual bool attachTextView(TextStoreRow &row, NodeContext &context, const TextViewPresentation &presentation)
    {
      return IPlatformController::attachTextView(row, context, presentation);
    }
  };
} // namespace
void testTextViewUnsupportedAndPresentation()
{
  UnsupportedController controller;
  Scene scene((Boundary<ViewRoot>()));
  LOKA_VERIFY(scene.mount(&controller));
  SceneAccess::updateAttached(scene, true);
  ViewRoot &root = *static_cast<ViewRoot *>(SceneAccess::rootBoundary(scene));
  TextDocumentAccess::complete(scene.textDocuments(), controller);
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_REFUSED_UNSUPPORTED);
  SceneAccess::unmount(scene);
  Fixture f;
  TextDocumentEditorNode *node = editor(&f.root());
  NodeContext *context = node->getContext();
  TextViewPresentation value(TextStyle(TextSizeValue::points(18)), 4, true, false);
  TextDocumentEditor next(TextDocumentEditorProps(f.root().first).attachment(f.root().result).presentation(value));
  failTextViewPreparations(1);
  LOKA_VERIFY(next.applyPropsToNode(node));
  const TextViewPresentation fallback = TextDocumentAccess::store(f.root().first)->view.presentation;
  LOKA_VERIFY(fallback.style() == TextStyle() && fallback.readOnly() && !fallback.wrap());
  LOKA_VERIFY(next.applyPropsToNode(node));
  LOKA_VERIFY(TextDocumentAccess::store(f.root().first)->view.presentation == value && node->getContext() == context);
  const unsigned attached = calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH);
  f.scene.invalidate();
  f.scene.flushInvalidation();
  LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH) == attached);
  f.show(false);
  failTextViewAttaches(1);
  f.show(true);
  f.complete();
  LOKA_VERIFY(f.root().result.get().value() == TextViewAttachment::VIEW_REFUSED_UNSUPPORTED);
}
void testTextViewRetirementAndContextEnd()
{
  Fixture f;
  TextDocumentEditorNode *node = editor(&f.root());
  NullTextStore *store = TextDocumentAccess::store(f.root().first);
  const unsigned before = calls(f.controller, NullScenePlatformController::TextViewCall::DETACH);
  node->setContext(0); // Destruction without a preceding RETIRED delivery.
  LOKA_VERIFY(!store->view.place);
  LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::DETACH) == before + 1);
  install(f.controller, f.scene, *node);
  TextDocumentAccess::withdraw(f.root().first);
  LOKA_VERIFY(!store->view.place);
  LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::DETACH) == before + 2);
  f.complete();
  LOKA_VERIFY(f.root().result.get().value() == TextViewAttachment::VIEW_DETACHED);
  const unsigned count = static_cast<unsigned>(f.controller.textViewCalls.size());
  f.controller.drainNativeRetirements();
  LOKA_VERIFY(f.controller.textViewCalls.size() == count);
}
void testTextViewRetiredPublication()
{
  Fixture f;
  TextDocumentEditorNode *node = editor(&f.root());
  LifecycleFactTestAccess::MarkSubtreeRetired(node);
  // Fact publication precedes terminal context delivery in a structural pass.
  LOKA_VERIFY(node->getContext() && f.root().result.get().value() == TextViewAttachment::VIEW_DETACHED);
  f.complete();
  LOKA_VERIFY(f.root().result.get().value() == TextViewAttachment::VIEW_DETACHED);
}
void testTextViewReplacementAndReadOnlyRequests()
{
  Fixture f;
  f.complete();
  ViewRoot &root = f.root();
  TextDocumentEditorNode *node = editor(&root);
  TextDocumentEditor readonly(TextDocumentEditorProps(root.first)
                                  .attachment(root.result)
                                  .focusedAs(root.focus, 1)
                                  .presentation(TextViewPresentation(TextStyle(), 0, true)));
  LOKA_VERIFY(readonly.applyPropsToNode(node));
  LOKA_VERIFY(f.controller.applyNativeFocus(*node->getContext()));
  LOKA_VERIFY(root.first.replace(replacement("first")) == POST_ACCEPTED);
  f.complete();
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  NullTextStore *old = TextDocumentAccess::store(root.first);
  NodeContext *place = old->view.place;
  const TextDocumentFact fact = root.first.fact()->get();
  failTextStoreBirths(TEXT_STORE_REFUSED_ALLOCATION, 1);
  LOKA_VERIFY(root.first.replace(replacement("refused")) == POST_ACCEPTED);
  f.complete();
  LOKA_VERIFY(TextDocumentAccess::store(root.first) == old && old->view.place == place);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "first");
  LOKA_VERIFY(root.first.fact()->get().caret() == fact.caret());
  NodeContext *focused = 0;
  LOKA_VERIFY(f.controller.readNativeFocus(focused) && focused == place);
  const unsigned detached = calls(f.controller, NullScenePlatformController::TextViewCall::DETACH);
  LOKA_VERIFY(root.first.replace(replacement("new")) == POST_ACCEPTED);
  f.complete();
  LOKA_VERIFY(!old->view.place && TextDocumentAccess::store(root.first)->view.place == place);
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::DETACH) == detached);
  LOKA_VERIFY(f.controller.readNativeFocus(focused) && focused == place);
  TextSplice splice;
  LOKA_VERIFY(
      TextSplice::FromUtf8(
          root.first.fact()->get().caret(), root.first.fact()->get().caret(), "!", 1, TextPlacement::End(), splice)
      == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(root.first.splice(splice) == POST_ACCEPTED);
  f.complete();
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "new!");
  f.show(false);
  LOKA_VERIFY(root.first.replace(replacement("parked")) == POST_ACCEPTED);
  f.complete();
  LOKA_VERIFY(
      TextSplice::FromUtf8(
          root.first.fact()->get().caret(), root.first.fact()->get().caret(), "?", 1, TextPlacement::End(), splice)
      == TEXT_DOCUMENT_OK);
  LOKA_VERIFY(root.first.splice(splice) == POST_ACCEPTED);
  f.complete();
  TextPosition start;
  LOKA_VERIFY(root.first.queryPositionAtUtf8(0, start));
  root.first.post(start);
  f.complete();
  f.show(true);
  f.complete();
  LOKA_VERIFY(root.first.fact()->get().caret() == start);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "parked?");
}
namespace
{
  struct BorrowProbe
  {
    Fixture &fixture;
    unsigned hits;
    bool refused;
    explicit BorrowProbe(Fixture &f)
        : fixture(f),
          hits(0),
          refused(false)
    {
    }
    static void run(void *data)
    {
      BorrowProbe &self = *static_cast<BorrowProbe *>(data);
      ++self.hits;
      LOKA_VERIFY(self.fixture.controller.borrowPhase().open());
      self.fixture.scene.invalidate();
      self.refused = !self.fixture.scene.flushInvalidation();
    }
  };
  struct PublicationProbe
  {
    Fixture &fixture;
    unsigned hits;
    PostResult post;
    explicit PublicationProbe(Fixture &f)
        : fixture(f),
          hits(0),
          post(POST_ACCEPTED)
    {
    }
    static void run(void *data)
    {
      PublicationProbe &self = *static_cast<PublicationProbe *>(data);
      ++self.hits;
      LOKA_VERIFY(self.fixture.controller.borrowPhase().open());
      self.post = self.fixture.root().first.replace(TextReplacement::Empty());
      self.fixture.root().shown.set(false);
      LOKA_VERIFY(!self.fixture.scene.flushInvalidation());
      LOKA_VERIFY(TextDocumentAccess::store(self.fixture.root().first)->view.place != 0);
    }
  };
} // namespace
void testTextViewBorrowAndPublication()
{
  Fixture f;
  BorrowProbe probe(f);
  onTextViewCall(&BorrowProbe::run, &probe, 1);
  f.show(false);
  LOKA_VERIFY(probe.hits == 1 && probe.refused);
  f.show(true);
  PublicationProbe publication(f);
  f.root().result.state()->bind(&PublicationProbe::run, &publication, false);
  f.complete();
  LOKA_VERIFY(publication.hits == 1 && publication.post == POST_QUEUE_FULL);
  f.root().result.state()->unbind(&PublicationProbe::run, &publication);
  f.scene.flushInvalidation();
  f.complete();
  LOKA_VERIFY(f.root().result.get().value() == TextViewAttachment::VIEW_DETACHED);
  for (std::size_t i = 0; i < f.controller.textViewCalls.size(); ++i)
    LOKA_VERIFY(f.controller.textViewCalls[i].borrowed);
}
namespace
{
  WindowProps windowProps()
  {
    WindowProps p;
    p.scene(new Scene(Boundary<ViewRoot>()));
    return p;
  }
  struct WindowFixture
  {
    NullScenePlatformController controller;
    NullPlatformContext platform;
    NullWindow window;
    WindowAdmissionTestApp app;
    WindowFixture()
        : window(&this->platform, windowProps(), &this->controller),
          app(this->window)
    {
      this->app.flush();
    }
    ViewRoot &root()
    {
      return *static_cast<ViewRoot *>(SceneAccess::rootBoundary(*this->window.scene()));
    }
  };
} // namespace
void testTextViewFocusRebind()
{
  WindowFixture f;
  TextDocumentEditorNode *node = editor(&f.root());
  LOKA_VERIFY(f.controller.applyNativeFocus(*node->getContext()));
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(f.root().focus.state()->get().is(1));
  TextDocumentEditor next(
      TextDocumentEditorProps(f.root().first).attachment(f.root().result).focusedAs(f.root().otherFocus, 2));
  LOKA_VERIFY(next.applyPropsToNode(node));
  LOKA_VERIFY(!f.root().focus.state()->get().is(1));
  LOKA_VERIFY(f.root().otherFocus.state()->get().is(2));
  f.root().shown.set(false);
  f.app.flush();
  LOKA_VERIFY(!f.root().otherFocus.state()->get().is(2));
  NodeContext *native = 0;
  LOKA_VERIFY(f.controller.readNativeFocus(native) && !native);
}
void testTextViewInstallAndCommitRefresh()
{
  WindowFixture f;
  LOKA_VERIFY(f.root().result.get().value() == TextViewAttachment::VIEW_DETACHED);
  LOKA_VERIFY(f.app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(f.root().result.get().value() == TextViewAttachment::VIEW_ATTACHED);
}
void testTextViewOrdinaryIdentityReplacement()
{
  Fixture f;
  ViewRoot &root = f.root();
  Node *parent = box(&root);
  TextDocumentEditorNode *first = editor(parent);
  TextDocumentAccess::body(root.first, "survives");
  Box desired;
  desired << TextDocumentEditor(root.second).attachment(root.otherResult);
  *editorProps(root.definition()) = TextDocumentEditorProps(root.second).attachment(root.otherResult);
  LOKA_VERIFY(root.reconcile(parent, desired, f.scene, f.controller));
  TextDocumentEditorNode *second = editor(parent);
  LOKA_VERIFY(second != first && first->lifecycleFact() == NODE_FACT_RETIRED && !first->getContext());
  f.controller.onChange(&root, NODE_DIRTY_INITIAL, true);
  f.complete();
  LOKA_VERIFY(second->getContext() && TextDocumentAccess::store(root.second)->view.place == second->getContext());
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "survives");
  Box resultChange;
  resultChange << TextDocumentEditor(root.second).attachment(root.result);
  *editorProps(root.definition()) = TextDocumentEditorProps(root.second).attachment(root.result);
  LOKA_VERIFY(root.reconcile(parent, resultChange, f.scene, f.controller));
  TextDocumentEditorNode *third = editor(parent);
  LOKA_VERIFY(third != second && second->lifecycleFact() == NODE_FACT_RETIRED);
  f.controller.onChange(&root, NODE_DIRTY_INITIAL, true);
  f.complete();
  LOKA_VERIFY(TextDocumentAccess::store(root.second)->view.place == third->getContext());
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
}
namespace
{
  class KeyedViewRoot : public BoundaryNodeFor<KeyedViewRoot>
  {
  public:
    TextDocument documentValue;
    NodeState<int> key;
    NodeState<TextViewAttachment> result;
    explicit KeyedViewRoot(const BoundaryPropsFor<KeyedViewRoot> &p)
        : BoundaryNodeFor<KeyedViewRoot>(p)
    {
      this->document(this->documentValue);
      this->state(this->key, 0);
      this->state(this->result, TextViewAttachment());
    }
    void arm(NodeComposition &c)
    {
      c.declare(TextDocumentEditor(this->documentValue).attachment(this->result));
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Keyed(*this->key.state(),
                      this,
                      &KeyedViewRoot::arm,
                      reservation::SeatNodes<reservation::Nodes<TextDocumentEditorNode, 1, reservation::End> >()));
    }
  };
  class RefusedContext : public NullTextDocumentViewContext
  {
  public:
    RefusedContext(IPlatformController &controller, SceneTextDocuments &documents, TextDocumentEditorNode &node)
        : NullTextDocumentViewContext(controller, documents, node)
    {
    }
    virtual bool acceptsInstallation() const
    {
      return false;
    }
  };
  struct FocusDetachProbe
  {
    WindowFixture &fixture;
    unsigned hits;
    explicit FocusDetachProbe(WindowFixture &f)
        : fixture(f),
          hits(0)
    {
    }
    static void run(void *data)
    {
      FocusDetachProbe &self = *static_cast<FocusDetachProbe *>(data);
      ++self.hits;
      self.fixture.window.scene()->invalidate();
      self.fixture.window.scene()->flushInvalidation();
    }
  };
} // namespace
void testTextViewKeyedReplacement()
{
  NullScenePlatformController controller;
  Scene scene((Boundary<KeyedViewRoot>()));
  LOKA_VERIFY(scene.mount(&controller));
  SceneAccess::updateAttached(scene, true);
  KeyedViewRoot &root = *static_cast<KeyedViewRoot *>(SceneAccess::rootBoundary(scene));
  TextDocumentAccess::body(root.documentValue, "resident");
  const unsigned before = calls(controller, NullScenePlatformController::TextViewCall::ATTACH);
  root.key.set(1);
  scene.flushInvalidation();
  TextDocumentAccess::complete(scene.textDocuments(), controller);
  LOKA_VERIFY(calls(controller, NullScenePlatformController::TextViewCall::ATTACH) == before + 1);
  LOKA_VERIFY(calls(controller, NullScenePlatformController::TextViewCall::DETACH) == 1);
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.documentValue) == "resident");
  SceneAccess::unmount(scene);
}
void testTextViewAdoptionAndTeardown()
{
  Fixture f;
  unsigned writes = 0;
  TextDocumentEditorNode refused(TextDocumentEditorProps(f.root().second).attachment(f.root().otherResult));
  f.root().otherResult.state()->bind(&countWrite, &writes, false);
  LOKA_VERIFY(!refused.adoptContext(new RefusedContext(f.controller, f.scene.textDocuments(), refused)));
  f.complete();
  LOKA_VERIFY(writes == 0 && !TextDocumentAccess::store(f.root().second)->view.place);
  f.root().otherResult.state()->unbind(&countWrite, &writes);
  const unsigned before = calls(f.controller, NullScenePlatformController::TextViewCall::DETACH);
  SceneAccess::unmount(f.scene);
  LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::DETACH) == before + 1);
  const unsigned total = static_cast<unsigned>(f.controller.textViewCalls.size());
  f.controller.drainNativeRetirements();
  LOKA_VERIFY(f.controller.textViewCalls.size() == total);
}
void testTextViewFocusObserverDetach()
{
  WindowFixture f;
  TextDocumentEditorNode *node = editor(&f.root());
  LOKA_VERIFY(f.controller.applyNativeFocus(*node->getContext()));
  f.app.reconcileAtCompletion();
  FocusDetachProbe probe(f);
  f.root().focus.state()->bind(&FocusDetachProbe::run, &probe, false);
  f.root().shown.set(false);
  f.app.flush();
  LOKA_VERIFY(probe.hits == 1 && !TextDocumentAccess::store(f.root().first)->view.place);
  f.root().focus.state()->unbind(&FocusDetachProbe::run, &probe);
}
void testTextViewReentryIdentityAndRefusal()
{
  for (unsigned refusal = 0; refusal != 2; ++refusal)
  {
    Fixture f;
    ViewRoot &root = f.root();
    NullTextStore *firstStore = TextDocumentAccess::store(root.first);
    TextDocumentAccess::body(root.first, "first preserved");
    f.show(false);
    Node *outgoing = root.childrenHead();
    TextDocumentEditorProps *desired = editorProps(root.definition());
    LOKA_VERIFY(desired);
    *desired = TextDocumentEditorProps(root.second).attachment(root.otherResult);
    if (refusal)
      refusedViewCreations = 1000;
    f.show(true);
    refusedViewCreations = 0;
    LOKA_VERIFY(TextDocumentAccess::store(root.first) == firstStore && !firstStore->view.place);
    LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "first preserved");
    if (refusal)
    {
      LOKA_VERIFY(root.childrenHead() == outgoing && !editor(&root));
      LOKA_VERIFY(!TextDocumentAccess::store(root.second)->view.place);
    }
    else
    {
      f.complete();
      LOKA_VERIFY(editor(&root)->props.document_ == &root.second);
      LOKA_VERIFY(root.otherResult.get().value() == TextViewAttachment::VIEW_ATTACHED);
    }
  }
}
void testTextViewDetachPreservesNativeFacts()
{
  Fixture f;
  ViewRoot &root = f.root();
  TextDocumentAccess::body(root.first, "body");
  TextDocumentAccess::selection(root.first, 3, 1);
  TextDocumentAccess::uncommitted(root.first, "marked");
  NullTextStore *store = TextDocumentAccess::store(root.first);
  LOKA_VERIFY(store->pending());
  const unsigned long revision = root.first.fact()->get().textRevision();
  f.show(false);
  f.show(true);
  LOKA_VERIFY(store == TextDocumentAccess::store(root.first) && store->pending());
  LOKA_VERIFY(root.first.fact()->get().textRevision() == revision);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "body");
  TextStoreReading reading;
  LOKA_VERIFY(f.controller.readTextStore(*store, reading));
  LOKA_VERIFY(reading.changed() && reading.marked() && reading.caret() == 3 && reading.anchor() == 1);
}
namespace
{
  /** The observer is installed before INITIAL, through the normal binding window. */
  class InitialObserverRoot : public BoundaryNodeFor<InitialObserverRoot>
  {
  public:
    TextDocument first, second;
    NodeState<TextViewAttachment> firstResult, secondResult;
    NodeState<bool> shown;
    unsigned observations;
    PostResult post;
    explicit InitialObserverRoot(const BoundaryPropsFor<InitialObserverRoot> &props)
        : BoundaryNodeFor<InitialObserverRoot>(props),
          observations(0),
          post(POST_ACCEPTED)
    {
      this->document(this->first);
      this->document(this->second);
      this->state(this->firstResult, TextViewAttachment());
      this->state(this->secondResult, TextViewAttachment());
      this->state(this->shown, true);
    }
    virtual void declareBindings(BindingToken &token)
    {
      token.watch(*this->secondResult.state(), this, &InitialObserverRoot::observed);
    }
    void observed()
    {
      ++this->observations;
      if (this->secondResult.get().value() != TextViewAttachment::VIEW_ATTACHED)
        return;
      LOKA_VERIFY(TextDocumentAccess::store(this->first)->view.place);
      this->post = this->first.replace(TextReplacement::Empty());
      this->shown.set(false);
      LOKA_VERIFY(!this->scene()->flushInvalidation());
      LOKA_VERIFY(TextDocumentAccess::store(this->first)->view.place);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(
          Fragment() << (Show(*this->shown.state()) << TextDocumentEditor(this->first).attachment(this->firstResult))
                     << TextDocumentEditor(this->second).attachment(this->secondResult));
    }
  };
} // namespace
void testTextViewInitialObserverAndOtherClaim()
{
  NullScenePlatformController controller;
  Scene scene((Boundary<InitialObserverRoot>()));
  LOKA_VERIFY(scene.mount(&controller));
  SceneAccess::updateAttached(scene, true);
  InitialObserverRoot &root = *static_cast<InitialObserverRoot *>(SceneAccess::rootBoundary(scene));
  LOKA_VERIFY(root.observations == 0 && root.firstResult.get().value() == TextViewAttachment::VIEW_DETACHED);
  LOKA_VERIFY(calls(controller, NullScenePlatformController::TextViewCall::ATTACH) == 2);
  TextDocumentAccess::complete(scene.textDocuments(), controller);
  LOKA_VERIFY(root.observations == 1 && root.post == POST_QUEUE_FULL);
  // The other claim survived the observer and still publishes this completion's attachment.
  LOKA_VERIFY(root.firstResult.get().value() == TextViewAttachment::VIEW_ATTACHED);
  scene.flushInvalidation();
  TextDocumentAccess::complete(scene.textDocuments(), controller);
  LOKA_VERIFY(root.firstResult.get().value() == TextViewAttachment::VIEW_DETACHED);
  LOKA_VERIFY(root.observations == 1);
  SceneAccess::unmount(scene);
}
