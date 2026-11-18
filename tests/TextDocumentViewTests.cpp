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
#include "support/TextStoreConformance.hpp"
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
  unsigned viewMaterializations = 0;
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
      ++viewMaterializations;
      return TextDocumentEditor::create();
    }
    virtual Node *createInPlace(void *memory) const
    {
      if (refusedViewCreations)
      {
        --refusedViewCreations;
        return 0;
      }
      ++viewMaterializations;
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
  class DirectViewRoot : public BoundaryNodeFor<DirectViewRoot>
  {
  public:
    TextDocument first;
    NodeState<TextViewAttachment> result;
    NodeState<bool> shown;
    explicit DirectViewRoot(const BoundaryPropsFor<DirectViewRoot> &props)
        : BoundaryNodeFor<DirectViewRoot>(props)
    {
      this->document(this->first);
      this->state(this->result, TextViewAttachment());
      this->state(this->shown, true);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Show(*this->shown.state()) << ViewDefinition(this->first).attachment(this->result));
    }
    NodeDefinitionBase *definition()
    {
      return this->composition().root();
    }
  };
  class DefaultSettleController : public NullScenePlatformController
  {
  public:
    virtual void settleTextComposition(TextStoreRow &row)
    {
      this->textViewCalls.push_back(TextViewCall(TextViewCall::SETTLE, row, this->borrowPhase().open()));
      IPlatformController::settleTextComposition(row);
    }
  };
  template <class Root, class Controller = NullScenePlatformController> struct ViewFixture
  {
    Controller controller;
    Scene scene;
    ViewFixture()
        : scene(Boundary<Root>())
    {
      LOKA_VERIFY(this->scene.mount(&this->controller));
      SceneAccess::updateAttached(this->scene, true);
    }
    ~ViewFixture()
    {
      SceneAccess::unmount(this->scene);
    }
    Root &root()
    {
      return *static_cast<Root *>(SceneAccess::rootBoundary(this->scene));
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
  typedef ViewFixture<ViewRoot> Fixture;
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
void testTextViewWrapParkedRootRefuses()
{
  Fixture f;
  ViewRoot &root = f.root();
  TextDocumentEditorNode *node = editor(&root);
  NodeContext *context = node->getContext();
  NullTextStore *store = TextDocumentAccess::store(root.first);
  TextDocumentAccess::body(root.first, "wrap body");
  f.show(false);
  TextDocumentEditor next(
      TextDocumentEditorProps(node->props).presentation(TextViewPresentation(TextStyle(), 0, false, false)));
  LOKA_VERIFY(!root.reconcile(node, next, f.scene, f.controller));
  f.complete();
  LOKA_VERIFY(node->getContext() == context && node->props.presentation_.wrap());
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_DETACHED);
  LOKA_VERIFY(!store->view.place && TextDocumentAccess::store(root.first) == store);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "wrap body");
  f.show(true);
  f.complete();
  LOKA_VERIFY(editor(&root) == node && node->getContext() == context);
  LOKA_VERIFY(store->view.place == context && TextStoreTestAccess::viewAttached(*store));
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "wrap body");
}
void testTextViewWrapChildReplacement()
{
  for (unsigned parked = 0; parked != 2; ++parked)
  {
    Fixture f;
    ViewRoot &root = f.root();
    NullTextStore *store = TextDocumentAccess::store(root.first);
    TextDocumentAccess::body(root.first, "wrap body");
    for (unsigned wrap = 0; wrap != 2; ++wrap)
    {
      Node *parent = box(&root);
      TextDocumentEditorNode *old = editor(parent);
      if (parked)
      {
        f.show(false);
        f.complete();
      }
      const TextDocumentEditorProps props =
          TextDocumentEditorProps(old->props).presentation(TextViewPresentation(TextStyle(), 0, false, wrap != 0));
      Box next;
      next << TextDocumentEditor(props);
      *editorProps(root.definition()) = props;
      LOKA_VERIFY(root.reconcile(parent, next, f.scene, f.controller));
      TextDocumentEditorNode *fresh = editor(parent);
      LOKA_VERIFY(fresh && fresh != old && fresh->props.presentation_.wrap() == (wrap != 0));
      LOKA_VERIFY(old->lifecycleFact() == NODE_FACT_RETIRED && !old->getContext());
      LOKA_VERIFY(!store->view.place && !TextStoreTestAccess::viewAttached(*store));
      if (parked)
      {
        LOKA_VERIFY(!fresh->getContext());
        f.complete();
        LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_DETACHED);
        f.show(true);
      }
      else
        f.controller.onChange(&root, NODE_DIRTY_INITIAL, true);
      f.complete();
      LOKA_VERIFY(editor(&root) == fresh && fresh->getContext());
      LOKA_VERIFY(store == TextDocumentAccess::store(root.first) && store->view.place == fresh->getContext());
      LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
      LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "wrap body");
    }
  }
}
void testTextViewWrapReturningBranch()
{
  ViewFixture<DirectViewRoot> f;
  DirectViewRoot &root = f.root();
  NullTextStore *store = TextDocumentAccess::store(root.first);
  TextDocumentAccess::body(root.first, "wrap body");
  for (unsigned wrap = 0; wrap != 2; ++wrap)
  {
    const unsigned attaches = calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH);
    const unsigned detaches = calls(f.controller, NullScenePlatformController::TextViewCall::DETACH);
    f.show(false);
    f.complete();
    LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_DETACHED);
    TextDocumentEditorProps *props = editorProps(root.definition());
    props->presentation(TextViewPresentation(TextStyle(), 0, false, wrap != 0));
    const unsigned materializations = viewMaterializations;
    f.show(true);
    LOKA_VERIFY(viewMaterializations == materializations + 1);
    f.complete();
    TextDocumentEditorNode *fresh = editor(&root);
    LOKA_VERIFY(fresh && fresh->props.presentation_.wrap() == (wrap != 0));
    LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::ATTACH) == attaches + 1);
    LOKA_VERIFY(calls(f.controller, NullScenePlatformController::TextViewCall::DETACH) == detaches + 1);
    LOKA_VERIFY(store == TextDocumentAccess::store(root.first) && store->view.place == fresh->getContext());
    LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
    LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "wrap body");
  }
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
  TextViewPresentation value(TextStyle(TextSizeValue::points(18)), 4, true, true);
  TextDocumentEditor next(TextDocumentEditorProps(f.root().first).attachment(f.root().result).presentation(value));
  failTextViewPreparations(1);
  LOKA_VERIFY(next.applyPropsToNode(node));
  const TextViewPresentation fallback = TextDocumentAccess::store(f.root().first)->view.presentation;
  LOKA_VERIFY(fallback.style() == TextStyle() && fallback.readOnly() && fallback.wrap());
  LOKA_VERIFY(next.applyPropsToNode(node));
  LOKA_VERIFY(TextDocumentAccess::store(f.root().first)->view.presentation == value && node->getContext() == context);
  LOKA_VERIFY(editor(&f.root()) == node);
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
    if (f.controller.textViewCalls[i].kind != NullScenePlatformController::TextViewCall::RETIRE)
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
  NodeContext *context = node->getContext();
  LOKA_VERIFY(f.controller.applyNativeFocus(*node->getContext()));
  f.app.reconcileAtCompletion();
  LOKA_VERIFY(f.root().focus.state()->get().is(1));
  TextDocumentEditor next(
      TextDocumentEditorProps(f.root().first).attachment(f.root().result).focusedAs(f.root().otherFocus, 2));
  LOKA_VERIFY(next.applyPropsToNode(node));
  LOKA_VERIFY(editor(&f.root()) == node && node->getContext() == context);
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
      c.declare(TextDocumentEditor(this->documentValue)
                    .attachment(this->result)
                    .presentation(TextViewPresentation(TextStyle(), 0, false, this->key.get() != 1)));
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
  NullTextStore *store = TextDocumentAccess::store(root.documentValue);
  LOKA_VERIFY(editor(&root)->props.presentation_.wrap());
  const unsigned before = calls(controller, NullScenePlatformController::TextViewCall::ATTACH);
  root.key.set(1);
  scene.flushInvalidation();
  LOKA_VERIFY(!editor(&root)->props.presentation_.wrap());
  TextDocumentAccess::complete(scene.textDocuments(), controller);
  LOKA_VERIFY(calls(controller, NullScenePlatformController::TextViewCall::ATTACH) == before + 1);
  LOKA_VERIFY(calls(controller, NullScenePlatformController::TextViewCall::DETACH) == 1);
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.documentValue) == "resident");
  LOKA_VERIFY(TextDocumentAccess::store(root.documentValue) == store
              && store->view.place == editor(&root)->getContext());
  root.key.set(2);
  scene.flushInvalidation();
  TextDocumentAccess::complete(scene.textDocuments(), controller);
  LOKA_VERIFY(editor(&root)->props.presentation_.wrap());
  LOKA_VERIFY(calls(controller, NullScenePlatformController::TextViewCall::ATTACH) == before + 2);
  LOKA_VERIFY(calls(controller, NullScenePlatformController::TextViewCall::DETACH) == 2);
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  LOKA_VERIFY(TextDocumentAccess::store(root.documentValue) == store
              && store->view.place == editor(&root)->getContext());
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
  ViewFixture<ViewRoot, DefaultSettleController> f;
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

namespace
{
  typedef NullScenePlatformController::TextViewCall Call;
  /** Read only this operation's trace, including native mark state at each entry. */
  class ViewTrace
  {
  public:
    explicit ViewTrace(const NullScenePlatformController &controller)
        : calls_(controller.textViewCalls),
          next_(this->calls_.size())
    {
    }
    void expect(Call::Kind kind, const TextStoreRow &row, bool marked, bool borrowed = true)
    {
      LOKA_VERIFY(this->next_ < this->calls_.size());
      const Call &call = this->calls_[this->next_++];
      LOKA_VERIFY(call.kind == kind && call.row == &row && call.marked == marked && call.borrowed == borrowed);
    }
    void end() const
    {
      LOKA_VERIFY(this->next_ == this->calls_.size());
    }

  private:
    const NullScenePlatformController::TextViewCalls &calls_;
    std::size_t next_;
  };

  void expectFold(TextDocument &document, const char *body = "ime")
  {
    LOKA_VERIFY(!TextDocumentAccess::store(document)->marked());
    LOKA_VERIFY(TextDocumentAccess::committedText(document) == body);
    LOKA_VERIFY(TextDocumentAccess::commitCalls(document) == 0);
  }

  void applyPresentation(TextDocumentEditorNode &node, bool readOnly)
  {
    TextDocumentEditor next(
        TextDocumentEditorProps(node.props).presentation(TextViewPresentation(TextStyle(), 1, readOnly)));
    LOKA_VERIFY(next.applyPropsToNode(&node));
  }

  class EarlyRetiringContext : public NullTextDocumentViewContext
  {
  public:
    EarlyRetiringContext(Fixture &fixture, TextDocumentEditorNode &node)
        : NullTextDocumentViewContext(fixture.controller, fixture.scene.textDocuments(), node),
          fixture_(fixture)
    {
    }
    virtual ~EarlyRetiringContext()
    {
      TextStoreRow *row = this->attachedStore();
      LOKA_VERIFY(row);
      ViewTrace trace(this->fixture_.controller);
      this->retireView();
      trace.expect(Call::SETTLE, *row, true);
      trace.expect(Call::DETACH, *row, false);
      trace.end();
    }

  private:
    Fixture &fixture_;
  };

} // namespace

void testTextViewSettleLifecycleDetach()
{
  Fixture f;
  TextDocument &document = f.root().first;
  NullTextStore &row = *TextDocumentAccess::store(document);
  TextDocumentEditorNode *node = editor(&f.root());
  f.complete();
  LOKA_VERIFY(!row.pending());
  const unsigned long revision = document.fact()->get().textRevision();
  ViewTrace trace(f.controller);
  TextDocumentAccess::uncommitted(document, "ime");
  NotifySubtreeNodeDetached(node);
  LOKA_VERIFY(!f.controller.borrowPhase().open());
  LifecycleFactTestAccess::DeliverFacts(node);
  trace.expect(Call::SETTLE, row, true);
  trace.expect(Call::DETACH, row, false);
  trace.end();
  expectFold(document);
  // The settled edit stays pending for the ordinary completion, which publishes it once.
  LOKA_VERIFY(row.pending());
  f.complete();
  LOKA_VERIFY(document.fact()->get().textRevision() == revision + 1);
  LOKA_VERIFY(!row.pending());
}

void testTextViewSettleEviction()
{
  Fixture f;
  TextDocument &document = f.root().first;
  NullTextStore &row = *TextDocumentAccess::store(document);
  TextDocumentEditorNode *old = editor(&f.root());
  TextDocumentEditorNode incoming(TextDocumentEditorProps(document).attachment(f.root().otherResult));
  TextDocumentAccess::uncommitted(document, "ime");
  NotifySubtreeNodeDetached(old);
  ViewTrace trace(f.controller);
  install(f.controller, f.scene, incoming);
  trace.expect(Call::SETTLE, row, true);
  trace.expect(Call::DETACH, row, false);
  trace.expect(Call::ATTACH, row, false);
  trace.expect(Call::PRESENT, row, false);
  LifecycleFactTestAccess::DeliverFacts(old);
  trace.end();
  LOKA_VERIFY(row.attachedContext() == incoming.getContext());
  expectFold(document);
}

void testTextViewSettleRetiredDelivery()
{
  Fixture f;
  TextDocument &document = f.root().first;
  NullTextStore &row = *TextDocumentAccess::store(document);
  TextDocumentEditorNode *node = editor(&f.root());
  LOKA_VERIFY(TextDocumentAccess::claimCount(f.scene.textDocuments()) == 1);
  TextDocumentAccess::uncommitted(document, "ime");
  ViewTrace trace(f.controller);
  LifecycleFactTestAccess::MarkSubtreeRetired(node);
  LifecycleFactTestAccess::DeliverFacts(node);
  trace.expect(Call::SETTLE, row, true);
  trace.expect(Call::DETACH, row, false);
  trace.end();
  LOKA_VERIFY(node->getContext() && TextDocumentAccess::claimCount(f.scene.textDocuments()) == 0);
  expectFold(document);
}

void testTextViewSettleContextDestruction()
{
  Fixture f;
  TextDocument &document = f.root().first;
  TextDocumentEditorNode *node = editor(&f.root());
  NullTextStore &row = *TextDocumentAccess::store(document);
  LOKA_VERIFY(node->lifecycleFact() == NODE_FACT_ATTACHED && node->getContext()->deliveredFact() == NODE_FACT_ATTACHED);
  TextDocumentAccess::uncommitted(document, "ime");
  ViewTrace trace(f.controller);
  node->setContext(0);
  trace.expect(Call::SETTLE, row, true);
  trace.expect(Call::DETACH, row, false);
  trace.end();
  expectFold(document);
}

void testTextViewSettleDerivedRetirement()
{
  Fixture f;
  TextDocument &document = f.root().first;
  TextDocumentEditorNode *node = editor(&f.root());
  NullTextStore &row = *TextDocumentAccess::store(document);
  node->setContext(0);
  EarlyRetiringContext *context = new EarlyRetiringContext(f, *node);
  LOKA_VERIFY(node->adoptContext(context));
  context->readLifecycleFactOnAttach();
  LOKA_VERIFY(row.attachedContext() == context && node->lifecycleFact() == NODE_FACT_ATTACHED
              && context->deliveredFact() == NODE_FACT_ATTACHED);
  TextDocumentAccess::uncommitted(document, "ime");
  ViewTrace trace(f.controller);
  node->setContext(0);
  trace.expect(Call::SETTLE, row, true);
  trace.expect(Call::DETACH, row, false);
  trace.end();
  expectFold(document);
}

void testTextViewSettleReadOnlyAttach()
{
  for (unsigned refusal = 0; refusal != 2; ++refusal)
  {
    Fixture f;
    TextDocument &document = f.root().first;
    NullTextStore &row = *TextDocumentAccess::store(document);
    TextDocumentEditorNode *node = editor(&f.root());
    node->setContext(0);
    applyPresentation(*node, true);
    TextDocumentAccess::uncommitted(document, "ime");
    if (refusal)
      failTextViewAttaches(1);
    ViewTrace trace(f.controller);
    LOKA_VERIFY(!f.controller.borrowPhase().open());
    install(f.controller, f.scene, *node);
    trace.expect(Call::SETTLE, row, true);
    trace.expect(Call::ATTACH, row, false);
    if (!refusal)
    {
      trace.expect(Call::SETTLE, row, false);
      trace.expect(Call::PRESENT, row, false);
    }
    trace.end();
    expectFold(document);
  }
}

void testTextViewSettleReadOnlyProps()
{
  Fixture f;
  TextDocument &document = f.root().first;
  NullTextStore &row = *TextDocumentAccess::store(document);
  TextDocumentAccess::uncommitted(document, "ime");
  ViewTrace trace(f.controller);
  LOKA_VERIFY(!f.controller.borrowPhase().open());
  applyPresentation(*editor(&f.root()), true);
  trace.expect(Call::SETTLE, row, true);
  trace.expect(Call::PRESENT, row, false);
  trace.end();
  expectFold(document);
}

void testTextViewSettleEditableAndRefusedClaim()
{
  Fixture f;
  TextDocument &document = f.root().first;
  NullTextStore &row = *TextDocumentAccess::store(document);
  TextDocumentEditorNode *node = editor(&f.root());
  node->setContext(0);
  TextDocumentAccess::uncommitted(document, "ime");
  ViewTrace trace(f.controller);
  install(f.controller, f.scene, *node);
  trace.expect(Call::ATTACH, row, true);
  trace.expect(Call::PRESENT, row, true);
  applyPresentation(*node, false);
  trace.expect(Call::PRESENT, row, true);
  TextDocumentEditorNode refused(TextDocumentEditorProps(document).attachment(f.root().otherResult));
  install(f.controller, f.scene, refused);
  f.complete();
  LOKA_VERIFY(f.root().otherResult.get().value() == TextViewAttachment::VIEW_REFUSED_OCCUPIED);
  NotifySubtreeNodeDetached(&refused);
  LifecycleFactTestAccess::DeliverFacts(&refused);
  trace.end();
  LOKA_VERIFY(row.marked() && TextDocumentAccess::committedText(document).empty());
  LOKA_VERIFY(TextDocumentAccess::commitCalls(document) == 0);
}

void testTextViewSettleDefaultLeavesMarked()
{
  ViewFixture<ViewRoot, DefaultSettleController> f;
  ViewRoot &root = f.root();
  TextDocumentEditorNode *node = editor(&root);
  NullTextStore &row = *TextDocumentAccess::store(root.first);
  TextDocumentAccess::uncommitted(root.first, "ime");
  ViewTrace trace(f.controller);
  NotifySubtreeNodeDetached(node);
  LifecycleFactTestAccess::DeliverFacts(node);
  trace.expect(Call::SETTLE, row, true);
  trace.expect(Call::DETACH, row, true);
  trace.end();
  f.complete();
  LOKA_VERIFY(root.first.fact()->get().state() == TEXT_DOCUMENT_MARKED
              && TextDocumentAccess::commitCalls(root.first) == 0);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first).empty());
  TextReader reader(root.first);
  char out[8];
  unsigned long written;
  LOKA_VERIFY(reader.read(out, sizeof out, written) == TEXT_READ_REFUSED && written == 0
              && reader.refusal() == TEXT_READ_NOT_READY);
}

void testTextViewStoreRetirementDoor()
{
  Fixture f;
  NullTextStore *row = TextDocumentAccess::store(f.root().first);
  LOKA_VERIFY(row && row->view.place);
  const std::size_t before = f.controller.textViewCalls.size();
  TextDocumentAccess::uncommitted(f.root().first, "discard");
  row->retire();
  row->retire();
  LOKA_VERIFY(f.controller.textViewCalls.size() == before + 2);
  const NullScenePlatformController::TextViewCall &detach = f.controller.textViewCalls[before];
  const NullScenePlatformController::TextViewCall &retire = f.controller.textViewCalls[before + 1];
  LOKA_VERIFY(detach.kind == NullScenePlatformController::TextViewCall::DETACH && detach.borrowed);
  LOKA_VERIFY(retire.kind == NullScenePlatformController::TextViewCall::RETIRE && !retire.borrowed);
  LOKA_VERIFY(detach.marked && retire.marked && row->marked());
  LOKA_VERIFY(detach.row == row && retire.row == row && retire.links == 2);
  LOKA_VERIFY(!row->view.place && !TextDocumentAccess::store(f.root().first));
}

namespace
{
  class ClaimReadContext : public NullTextDocumentViewContext
  {
  public:
    ClaimReadContext(IPlatformController &controller, SceneTextDocuments &documents, TextDocumentEditorNode &node)
        : NullTextDocumentViewContext(controller, documents, node)
    {
    }
    TextStoreRow *store() const
    {
      return this->attachedStore();
    }
  };
} // namespace
void testTextViewClaimDerivedReads()
{
  Fixture f;
  ViewRoot &root = f.root();
  TextDocumentEditorNode *node = editor(&root);
  node->setContext(0);
  ClaimReadContext *context = new ClaimReadContext(f.controller, f.scene.textDocuments(), *node);
  LOKA_VERIFY(node->adoptContext(context));
  NullTextStore *old = TextDocumentAccess::store(root.first);
  LOKA_VERIFY(!context->store() && !old->attachedContext() && !TextStoreTestAccess::viewAttached(*old));
  context->readLifecycleFactOnAttach();
  f.complete();
  LOKA_VERIFY(context->store() == old && old->attachedContext() == context && TextStoreTestAccess::viewAttached(*old));
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  // Native place is not attachment authority: temporarily disagree without calling lifecycle doors.
  old->view.place = 0;
  LOKA_VERIFY(context->store() == old && old->attachedContext() == context && TextStoreTestAccess::viewAttached(*old));
  old->view.place = context;
  f.show(false);
  f.complete();
  LOKA_VERIFY(!context->store() && !old->attachedContext() && !TextStoreTestAccess::viewAttached(*old));
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_DETACHED);
  old->view.place = context;
  LOKA_VERIFY(!context->store() && !old->attachedContext() && !TextStoreTestAccess::viewAttached(*old));
  old->view.place = 0;
  f.show(true);
  f.complete();
  LOKA_VERIFY(context->store() == old && old->attachedContext() == context && TextStoreTestAccess::viewAttached(*old));
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  ViewTrace transfer(f.controller);
  LOKA_VERIFY(root.first.replace(replacement("transferred")) == POST_ACCEPTED);
  f.complete();
  NullTextStore *fresh = TextDocumentAccess::store(root.first);
  transfer.expect(Call::TRANSFER, *fresh, false);
  transfer.expect(Call::RETIRE, *old, false);
  transfer.end();
  LOKA_VERIFY(!old->attachedContext() && fresh->attachedContext() == context);
  LOKA_VERIFY(fresh != old && context->store() == fresh);
  LOKA_VERIFY(!TextStoreTestAccess::viewAttached(*old) && TextStoreTestAccess::viewAttached(*fresh));
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_ATTACHED);
  LOKA_VERIFY(TextDocumentAccess::committedText(root.first) == "transferred");
  TextDocumentAccess::uncommitted(root.first, "ime");
  ViewTrace present(f.controller);
  applyPresentation(*node, true);
  present.expect(Call::SETTLE, *fresh, true);
  present.expect(Call::PRESENT, *fresh, false);
  present.end();
  expectFold(root.first, "transferredime");
  fresh->retire();
  f.complete();
  LOKA_VERIFY(!context->store() && !TextStoreTestAccess::viewAttached(*fresh));
  LOKA_VERIFY(root.result.get().value() == TextViewAttachment::VIEW_DETACHED);
  const std::size_t callsBeforeDrain = f.controller.textViewCalls.size();
  f.controller.drainNativeRetirements();
  LOKA_VERIFY(!context->store() && f.controller.textViewCalls.size() == callsBeforeDrain);
}

void testTextViewParkedRowIgnoresNativePlace()
{
  Fixture f;
  TextDocumentEditorNode *node = editor(&f.root());
  NullTextStore &row = *TextDocumentAccess::store(f.root().first);
  f.show(false);
  LOKA_VERIFY(!row.view.place && !row.attachedContext());
  row.view.place = node->getContext();
  LOKA_VERIFY(row.view.place && !row.attachedContext());
  row.view.place = 0;
}
