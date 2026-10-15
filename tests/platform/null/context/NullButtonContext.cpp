#include "platform/null/context/NullButtonContext.hpp"

#include "app/layout/FallbackControlMetrics.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"

namespace
{
  class NullButtonNodeHandler
      : public loka::app::scene::RetainedNodeHandler<NullButtonNodeHandler,
                                                     loka::app::ButtonNode,
                                                     NullButtonContext>
  {
  public:
    static loka::app::ButtonNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asButtonNode() : 0;
    }

    static NullButtonContext *create(loka::app::ButtonNode *button,
                                     loka::app::scene::IPlatformController *controller,
                                     const loka::app::scene::LayoutState &state)
    {
      (void)state;
      NullScenePlatformController *nullPlatform =
          static_cast<NullScenePlatformController *>(controller);
      return new NullButtonContext(button, nullPlatform);
    }
  };

  NullButtonNodeHandler gNullButtonNodeHandler;
} // namespace

NullButtonContext::NullButtonContext(loka::app::ButtonNode *node,
                                     NullScenePlatformController *controller)
    : loka::app::scene::NativeNodeContext(),
      node_(node),
      controller_(controller),
      handle_(0)
{
  if (this->controller_ && this->node_)
  {
    this->handle_ = this->controller_->createLedgerRow(
        NullScenePlatformController::CONTROL_RECIPE_BUTTON, this, this->node_->nativeLifetimeHint());
  }
}

NullButtonContext::~NullButtonContext()
{
  assert(!this->handle_ && "terminal fact delivery must detach the native before context reclaim");
  this->handle_ = 0;
  this->controller_ = 0;
  this->node_ = 0;
}

void NullButtonContext::readLifecycleFactOnAttach()
{
  if (this->controller_ && this->node_)
  {
    this->controller_->setVisible(this->handle_,
                                  this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED);
    this->controller_->observeHint(this->handle_, this->lifetimeHint());
  }
}

void NullButtonContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
                                      loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (this->controller_)
  {
    this->controller_->setVisible(this->handle_, next == loka::app::scene::NODE_FACT_ATTACHED);
    this->controller_->observeHint(this->handle_, this->lifetimeHint());
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      this->controller_->completeContextTeardown(this->handle_);
      this->handle_ = 0;
      this->controller_ = 0;
      this->node_ = 0;
    }
  }
}

short NullButtonContext::layout(loka::app::scene::IPlatformController *,
                                loka::app::scene::LayoutState &state)
{
  // The control consumes its own height; the enclosing container owns gaps.
  state.height = static_cast<short>(loka::app::layout::FallbackControlMetrics::kButtonHeight);
  return static_cast<short>(state.y + loka::app::layout::FallbackControlMetrics::kButtonHeight);
}

void RegisterNullButtonNodeHandler(NullScenePlatformController &controller)
{
  controller.registerNodeHandler(&gNullButtonNodeHandler);
}
