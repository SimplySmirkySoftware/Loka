#ifndef LOKA_TOOLBOX_EDIT_CONTROL_LEDGER_HPP
#define LOKA_TOOLBOX_EDIT_CONTROL_LEDGER_HPP

#include "ToolboxFocusedEditIndex.hpp"
#include <cstddef>
#include <vector>

/** Owns Toolbox edit-control bindings together with their focused position.
    BindingT must expose ownerContext and text fields. The context is a borrowed
    identity token; the controller remains the sole owner of both the binding
    and its native handle. */
template <typename BindingT, typename ContextT> class ToolboxEditControlLedger
{
public:
  std::size_t size() const
  {
    return bindings_.size();
  }

  bool empty() const
  {
    return bindings_.empty();
  }

  BindingT &operator[](std::size_t index)
  {
    return bindings_[index];
  }

  const BindingT &operator[](std::size_t index) const
  {
    return bindings_[index];
  }

  void add(const BindingT &binding)
  {
    bindings_.push_back(binding);
  }

  BindingT &back()
  {
    return bindings_.back();
  }

  bool find(ContextT *ownerContext, std::size_t &index) const
  {
    for (std::size_t i = 0; i < bindings_.size(); ++i)
    {
      if (bindings_[i].ownerContext == ownerContext)
      {
        index = i;
        return true;
      }
    }
    return false;
  }

  bool contains(ContextT *ownerContext) const
  {
    std::size_t index = 0;
    return this->find(ownerContext, index);
  }

  template <typename TextT, typename ReceiverT>
  std::size_t forEachTextBinding(TextT *text,
                                 ReceiverT *receiver,
                                 void (ReceiverT::*operation)(BindingT &))
  {
    std::size_t applied = 0;
    for (std::size_t i = 0; i < bindings_.size(); ++i)
    {
      if (bindings_[i].text == text)
      {
        (receiver->*operation)(bindings_[i]);
        ++applied;
      }
    }
    return applied;
  }

  BindingT *focused()
  {
    return focused_.resolve(bindings_);
  }

  bool queryFocusedIndex(std::size_t &out) const
  {
    return focused_.query(bindings_.size(), out);
  }

  void focus(std::size_t index)
  {
    focused_.focus(index);
  }

  void clearFocus()
  {
    focused_.clear();
  }

  void erase(std::size_t index)
  {
    focused_.erase(index);
    bindings_.erase(bindings_.begin() + index);
  }

  void clear()
  {
    focused_.clear();
    bindings_.clear();
  }

private:
  std::vector<BindingT> bindings_;
  ToolboxFocusedEditIndex focused_;
};

#endif // LOKA_TOOLBOX_EDIT_CONTROL_LEDGER_HPP
