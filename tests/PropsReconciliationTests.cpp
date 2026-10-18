#include "testing/scene/SceneTestFlow.hpp"
#include "PropsReconciliationTests.hpp"
#include "support/TestVerify.hpp"
#include "support/PropsReconciliation.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/ZStack.hpp"
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
} // namespace

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
  declaration = Button(&b);
  applyProps(scene, declaration);
  {
    Node *head = root(scene)->childrenHead();
    LOKA_VERIFY(head == button && button->getContext() == context);
  }
  assert(button->props.text_ == &b);
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

void testZStackUnitGroundIdentity()
{
  using namespace loka::app;
  const ZStack ordinary;
  const ZStack blurred = ZStack().expBlurUnit();
  const ZStack copied(blurred);
  LOKA_VERIFY(ordinary.props.unitGround == ZSTACK_UNIT_GROUND_WINDOW);
  LOKA_VERIFY(blurred.props.unitGround == ZSTACK_UNIT_GROUND_EXP_BLUR);
  LOKA_VERIFY(ordinary.props < blurred.props);
  LOKA_VERIFY(!(blurred.props < ordinary.props));
  LOKA_VERIFY(!(copied.props < blurred.props) && !(blurred.props < copied.props));
}
