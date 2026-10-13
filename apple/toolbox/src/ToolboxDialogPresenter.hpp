#ifndef LOKA_TOOLBOX_DIALOG_PRESENTER_HPP
#define LOKA_TOOLBOX_DIALOG_PRESENTER_HPP
#include "app/scene/projection/NativeNodeContext.hpp"

class ToolboxApp;

/** FIFO owner; payload identity stays in the concrete present implementation. */
class ToolboxDialogPresenter : public loka::app::scene::NativeNodeContext
{
public:
  explicit ToolboxDialogPresenter(loka::app::scene::Node *node) { this->setOwner(node); }
  bool attached() const
  {
    return this->owner() && this->owner()->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED;
  }
  virtual void present(ToolboxApp &app) = 0;
};

#endif
