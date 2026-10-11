#include "testing/scene/SceneTestFlow.hpp"
#include "PropsReconciliationTests.hpp"
#include "support/TestVerify.hpp"
#include "support/PropsReconciliation.hpp"
#include "app/nodes/controls/Button.hpp"
#include "platform/null/NullScenePlatformController.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace PropsReconciliationSupport;

  class CountingContext : public NodeContext
  {
  public:
    CountingContext()
        : calls_(0)
    {
    }
    virtual void onPropsApplied()
    {
      ++this->calls_;
    }
    unsigned calls() const
    {
      return this->calls_;
    }

  private:
    unsigned calls_;
  };

  class ButtonHandler : public IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return NodeTypeToken<ButtonNode>();
    }
    virtual NodeContext *ensureContext(Node *node, IPlatformController *, const LayoutState &)
    {
      if (!node->getContext())
        node->setContext(new CountingContext());
      return node->getContext();
    }
  };

  /** Exposes the existing local retained-tree path without recomposing a Boundary. */
  class MetadataRetainedBoundary : public BoundaryNodeFor<MetadataRetainedBoundary>
  {
  public:
    explicit MetadataRetainedBoundary(const BoundaryPropsFor<MetadataRetainedBoundary> &props)
        : BoundaryNodeFor<MetadataRetainedBoundary>(props)
    {
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Button());
    }
    bool reconcile(ComponentContext &context, Button &previous, Button &current)
    {
      return this->applyRetainedDefinitionTree(
          context, this->childrenHead(), &previous, &current);
    }
  };
} // namespace

void testMetadataOnlyRetainedReconciliationRefreshesDefinitionMetadata()
{
  Button previous;
  Button current;
  current.tag(442);
  current.lifetimeHint(NATIVE_HINT_DESIRE_STAY);
  LOKA_VERIFY(previous.hasEquivalentProps(current));
  LOKA_VERIFY(current.hasEquivalentProps(previous));

  ButtonHandler handler;
  NullScenePlatformController platform;
  LOKA_VERIFY(platform.registerNodeHandler(&handler));
  Scene scene((Boundary<MetadataRetainedBoundary>()));
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  MetadataRetainedBoundary *boundary = static_cast<MetadataRetainedBoundary *>(root(scene));
  ButtonNode *button = boundary->childrenHead()->asButtonNode();
  LOKA_VERIFY(button != 0);
  CountingContext *context = static_cast<CountingContext *>(button->getContext());
  LOKA_VERIFY(context != 0);
  LOKA_VERIFY(context->calls() == 0);
  LOKA_VERIFY(button->nodeTag() == previous.nodeTag());
  LOKA_VERIFY(button->nativeLifetimeHint() == previous.nativeLifetimeHint());
  LOKA_VERIFY(button->nodeTag() != current.nodeTag());
  LOKA_VERIFY(button->nativeLifetimeHint() != current.nativeLifetimeHint());

  ComponentContext componentContext;
  componentContext.setBoundary(boundary);
  componentContext.setStateOwner(boundary);
  componentContext.setScene(&scene);
  componentContext.setPlatformController(&platform);
  // Use the reconciliation decision, not direct apply/repoint: equivalent Props
  // must skip onPropsApplied while refreshing definition-only metadata (#442).
  LOKA_VERIFY(boundary->reconcile(componentContext, previous, current));
  LOKA_VERIFY(boundary->childrenHead() == button);
  LOKA_VERIFY(button->getContext() == context);
  LOKA_VERIFY(context->calls() == 0);
  LOKA_VERIFY(button->nativeLifetimeHint() == current.nativeLifetimeHint());
  LOKA_VERIFY(button->nodeTag() == current.nodeTag());
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}

void testRetainedPropsApplicationNotifiesContextExactlyOnce()
{
  MutableState<String> a(String::Literal("A")), b(String::Literal("B"));
  Button declaration(&a);
  ButtonHandler handler;
  NullScenePlatformController platform;
  LOKA_VERIFY(platform.registerNodeHandler(&handler));
  Scene scene((Boundary<Tree<Button> >(Props<Button>(&declaration))));
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  settle(scene);
  ButtonNode *button = root(scene)->childrenHead()->asButtonNode();
  LOKA_VERIFY(button != 0);
  CountingContext *context = static_cast<CountingContext *>(button->getContext());
  LOKA_VERIFY(context != 0);
  LOKA_VERIFY(context->calls() == 0);
  LOKA_VERIFY(declaration.repointRetainedNodeDefinition(button));
  {
    Node *head = root(scene)->childrenHead();
    LOKA_VERIFY(head == button && button->getContext() == context);
  }
  LOKA_VERIFY(context->calls() == 0);
  declaration = Button(&b);
  applyProps(scene, declaration);
  {
    Node *head = root(scene)->childrenHead();
    LOKA_VERIFY(head == button && button->getContext() == context);
  }
  assert(button->props.text_ == &b);
  LOKA_VERIFY(context->calls() == 1);
  LOKA_VERIFY(declaration.repointRetainedNodeDefinition(button));
  LOKA_VERIFY(context->calls() == 1);
  declaration = Button(&a);
  applyProps(scene, declaration);
  {
    Node *head = root(scene)->childrenHead();
    LOKA_VERIFY(head == button && button->getContext() == context);
  }
  LOKA_VERIFY(context->calls() == 2);
  loka::dsl::testing::SceneTestAccess::unmount(scene);
}
