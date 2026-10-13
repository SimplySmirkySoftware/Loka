#ifndef LOKA_TEST_DIALOG_CONTEXT_HPP
#define LOKA_TEST_DIALOG_CONTEXT_HPP
#include "app/core/DialogResultTransport.hpp"
#include "platform/null/NullWindow.hpp"
#include <vector>

namespace dialog_testing
{
  using namespace loka::app;
  using namespace loka::app::scene;
  /** Native-equivalent projection producer shared by file and confirm pins. */
  template <class NodeT, class RegistrationT, class ReturnPortT>
  class DialogContext : public NodeContext
  {
  public:
    DialogContext(NodeT *node, NullWindow &window, std::vector<int> &events)
        : node_(node), registration_(0), window_(&window), events_(&events) {}
    virtual ~DialogContext()
    {
      this->cancel();
      this->events_->push_back(3);
    }
    void readLifecycleFactOnAttach()
    {
      if (this->node_->lifecycleFact() == NODE_FACT_ATTACHED)
        this->presentation_.markDetached();
    }
    virtual void onFactChanged(NodeLifecycleFact, NodeLifecycleFact next)
    {
      this->events_->push_back(4);
      if (next == NODE_FACT_ATTACHED)
        this->presentation_.markDetached();
      else
      {
        this->presentation_.markDetached();
        this->cancel();
      }
    }
    virtual void onPropsApplied()
    {
      if (this->registration_ && !this->registration_->matches(this->node_->props))
        this->cancel();
    }
    void attach()
    {
      // Deliberate Win32 producer twin: attach records intent; projection
      // attempts enrollment once, with phase and binding guarding repeats.
      if (this->registration_ || this->node_->lifecycleFact() != NODE_FACT_ATTACHED
          || !this->presentation_.beginPresent())
        return;
      this->registration_ = this->window_->dialogResults().reserve(this->node_->props);
      if (!this->registration_)
      {
        this->presentation_.markDetached();
        return;
      }
      this->presentation_.markPresented();
      this->events_->push_back(5);
    }
    void cancel()
    {
      RegistrationT *registration = this->registration_;
      this->registration_ = 0;
      delete registration;
    }
    template <class ResultT> Window *produce(const ResultT &result)
    {
      ReturnPortT port(this->registration_);
      return port.seal(result);
    }
    OpenFileDialogPresentationPhase presentation_;
    NodeT *node_;
    RegistrationT *registration_;
  private:
    NullWindow *window_;
    std::vector<int> *events_;
  };
}
#endif
