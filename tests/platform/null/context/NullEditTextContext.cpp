#include "platform/null/context/NullEditTextContext.hpp"

#include "app/layout/FallbackControlMetrics.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"

namespace
{
  class NullEditTextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<NullEditTextNodeHandler,
                                                     loka::app::EditTextNode,
                                                     NullEditTextContext>
  {
  public:
    static loka::app::EditTextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asEditTextNode() : 0;
    }

    static NullEditTextContext *create(loka::app::EditTextNode *editText,
                                       loka::app::scene::IPlatformController *controller,
                                       const loka::app::scene::LayoutState &state)
    {
      (void)state;
      NullScenePlatformController *nullPlatform =
          static_cast<NullScenePlatformController *>(controller);
      return new NullEditTextContext(editText, nullPlatform);
    }
  };

  NullEditTextNodeHandler gNullEditTextNodeHandler;
} // namespace

NullEditTextContext::NullEditTextContext(loka::app::EditTextNode *node,
                                         NullScenePlatformController *controller)
    : loka::app::scene::NativeNodeContext(),
      node_(node),
      controller_(controller),
      handle_(0)
{
  if (this->controller_ && this->node_)
  {
    this->handle_ = this->controller_->createLedgerRow(
        NullScenePlatformController::CONTROL_RECIPE_EDIT_TEXT, this, this->node_->nativeLifetimeHint());
  }
}

NullEditTextContext::~NullEditTextContext()
{
  assert(!this->handle_ && "terminal fact delivery must detach the native before context reclaim");
  this->handle_ = 0;
  this->controller_ = 0;
  this->node_ = 0;
}

void NullEditTextContext::readLifecycleFactOnAttach()
{
  if (this->controller_ && this->node_)
  {
    this->controller_->setVisible(this->handle_,
                                  this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED);
    this->controller_->observeHint(this->handle_, this->lifetimeHint());
  }
}

void NullEditTextContext::onFactChanged(loka::app::scene::NodeLifecycleFact previous,
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

short NullEditTextContext::layout(loka::app::scene::IPlatformController *,
                                  loka::app::scene::LayoutState &state)
{
  // The control consumes its own height; the enclosing container owns gaps.
  state.height = static_cast<short>(loka::app::layout::FallbackControlMetrics::kEditTextHeight);
  return static_cast<short>(state.y + loka::app::layout::FallbackControlMetrics::kEditTextHeight);
}

void RegisterNullEditTextNodeHandler(NullScenePlatformController &controller)
{
  controller.registerNodeHandler(&gNullEditTextNodeHandler);
}

bool IsNullEditTextNodeHandler(const loka::app::scene::IPlatformNodeHandler *handler)
{
  return handler == &gNullEditTextNodeHandler;
}
