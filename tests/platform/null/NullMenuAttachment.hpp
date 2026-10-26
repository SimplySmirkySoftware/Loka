#ifndef LOKA_TESTS_NULL_MENU_ATTACHMENT_HPP
#define LOKA_TESTS_NULL_MENU_ATTACHMENT_HPP

#include <map>
#include <vector>
#include <utility>
#include "app/Menu.hpp"
#include "app/EditMenuDispatch.hpp"
#include "core/util/OwnedDef.hpp"

/** Reference projection owner: command endpoints and value subscriptions are
    valid only until disconnect, which precedes Scene root detach. Like the
    native rails, ids are local to the opened surface, in declaration order. */
class NullMenuAttachment
{
  struct Subscription
  {
    explicit Subscription(const loka::app::MenuItemDefinition &item)
        : item_(item), values_(item.isEnabledInitial(), item.isCheckedInitial()),
          title_(item.titleState ? item.titleState->get() : item.title)
    {
      if (this->item_.titleState)
        this->item_.titleState->deferBind(&Update, this);
      if (this->item_.enabledBindingState())
        this->item_.enabledBindingState()->deferBind(&Update, this);
      if (this->item_.checkedBindingState())
        this->item_.checkedBindingState()->deferBind(&Update, this);
    }
    ~Subscription()
    {
      if (this->item_.titleState)
        this->item_.titleState->deferUnbind(&Update, this);
      if (this->item_.enabledBindingState())
        this->item_.enabledBindingState()->deferUnbind(&Update, this);
      if (this->item_.checkedBindingState())
        this->item_.checkedBindingState()->deferUnbind(&Update, this);
    }
    static void Update(void *data)
    {
      Subscription *self = static_cast<Subscription *>(data);
      self->title_ = self->item_.titleState ? self->item_.titleState->get() : self->item_.title;
      self->values_ = std::make_pair(self->item_.isEnabledInitial(), self->item_.isCheckedInitial());
    }
    const loka::app::MenuItemDefinition &item_;
    std::pair<bool, bool> values_;
    loka::core::String title_;
  private:
    Subscription(const Subscription &);
    Subscription &operator=(const Subscription &);
  };

public:
  NullMenuAttachment() {}
  ~NullMenuAttachment() { this->disconnect(); }

  bool open(const loka::app::MenuBarDefinition &bar)
  {
    // Failure-atomic: a clone refusal leaves the connected projection as it is.
    loka::core::OwnedDef<loka::app::MenuBarDefinition> candidate(bar.clone());
    if (!candidate.isSet())
      return false;
    this->disconnect();
    this->applied_.reset(candidate.take());
    for (const loka::app::MenuDefinition *menu = this->applied_->menusHead(); menu;
         menu = menu->nextInComposition)
      this->append(menu->itemsHead());
    return true;
  }

  void dispatch(unsigned id, Window *window = 0)
  {
    std::map<unsigned, const loka::app::MenuItemDefinition *>::const_iterator found = this->commands_.find(id);
    if (found == this->commands_.end())
      return;
    const loka::app::MenuActionType action = found->second->action;
    loka::core::EmitterState *binding = found->second->onClickState;
    loka::app::EditVerb verb;
    if (loka::app::menuEditVerb(action, verb))
    {
      loka::app::dispatchEditMenuCommand(window, verb, binding);
      return;
    }
    if (binding)
      binding->emit();
  }

  void disconnect()
  {
    this->commands_.clear();
    for (size_t i = 0; i < this->subscriptions_.size(); ++i)
      delete this->subscriptions_[i];
    this->subscriptions_.clear();
    this->applied_.reset();
  }

  bool connected() const { return !this->commands_.empty() || !this->subscriptions_.empty(); }
  size_t subscriptionCount() const { return this->subscriptions_.size(); }
  const loka::app::MenuBarDefinition *applied() const { return this->applied_.get(); }
  std::pair<bool, bool> values(unsigned id) const
  {
    return id && id <= this->subscriptions_.size()
        ? this->subscriptions_[id - 1]->values_ : std::make_pair(false, false);
  }

  loka::core::String title(unsigned id) const
  {
    return id && id <= this->subscriptions_.size()
        ? this->subscriptions_[id - 1]->title_ : loka::core::String();
  }

private:
  void append(const loka::app::MenuItemDefinition *item)
  {
    for (; item; item = item->nextInComposition)
    {
      this->subscriptions_.push_back(new Subscription(*item));
      const unsigned id = static_cast<unsigned>(this->subscriptions_.size());
      if (item->onClickState || item->action != loka::app::MENU_ACTION_NONE)
        this->commands_[id] = item;
      this->append(item->childrenHead());
    }
  }
  loka::core::OwnedDef<loka::app::MenuBarDefinition> applied_;
  std::map<unsigned, const loka::app::MenuItemDefinition *> commands_;
  std::vector<Subscription *> subscriptions_;
  NullMenuAttachment(const NullMenuAttachment &);
  NullMenuAttachment &operator=(const NullMenuAttachment &);
};

#endif
