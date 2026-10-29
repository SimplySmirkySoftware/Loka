#ifndef LOKA_TOOLBOX_EDIT_CONTROL_LEDGER_HPP
#define LOKA_TOOLBOX_EDIT_CONTROL_LEDGER_HPP

#include "ToolboxFocusedEditIndex.hpp"
#include <cstddef>
#include "core/LokaAlloc.hpp"
#include <cassert>
#include <limits>

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

  /** Prepare storage before creating a native handle. Refusal preserves rows and focus. */
  bool prepareAppend()
  {
    this->cancel();
    if (this->bindings_.size() < this->bindings_.capacity())
      return true;
    const std::size_t limit = (std::numeric_limits<std::size_t>::max)() / sizeof(BindingT);
    const std::size_t capacity = this->bindings_.capacity();
    if (capacity == limit)
      return false;
    const std::size_t next = capacity == 0 ? 4 : (capacity > limit / 2 ? limit : capacity * 2);
    return this->candidate_.reserve(next);
  }

  /** Commit a completed row without allocation; growth copies only this owner's rows. */
  void commitAppend(const BindingT &binding)
  {
    if (this->candidate_.capacity())
    {
      for (std::size_t i = 0; i < this->bindings_.size(); ++i)
        this->candidate_.append(this->bindings_[i]);
      this->candidate_.append(binding);
      this->bindings_.take(this->candidate_);
    }
    else
      this->bindings_.append(binding);
  }

  /** Abandon uncommitted growth, including native-install refusal. */
  void cancel()
  {
    this->candidate_.release();
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
    std::size_t index = 0;
    return focused_.query(bindings_.size(), index) ? &bindings_[index] : 0;
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
    bindings_.erase(index);
  }

  void clear()
  {
    focused_.clear();
    this->cancel();
    bindings_.clear();
  }

private:
  /** Owns raw capacity and its constructed prefix. No native resources are released here. */
  class Storage
  {
  public:
    Storage()
        : rows_(0),
          size_(0),
          capacity_(0)
    {
    }
    ~Storage()
    {
      this->release();
    }
    std::size_t size() const
    {
      return this->size_;
    }
    std::size_t capacity() const
    {
      return this->capacity_;
    }
    bool empty() const
    {
      return this->size_ == 0;
    }
    BindingT &operator[](std::size_t i)
    {
      return this->rows_[i];
    }
    const BindingT &operator[](std::size_t i) const
    {
      return this->rows_[i];
    }
    BindingT &back()
    {
      return this->rows_[this->size_ - 1];
    }
    bool reserve(std::size_t capacity)
    {
      assert(!this->rows_);
      this->rows_ = static_cast<BindingT *>(loka::core::LokaAllocRaw(capacity * sizeof(BindingT), site()));
      if (!this->rows_)
        return false;
      this->capacity_ = capacity;
      return true;
    }
    void append(const BindingT &row)
    {
      assert(this->size_ < this->capacity_);
      new (this->rows_ + this->size_) BindingT(row);
      ++this->size_;
    }
    void erase(std::size_t index)
    {
      assert(index < this->size_);
      for (std::size_t i = index + 1; i < this->size_; ++i)
        this->rows_[i - 1] = this->rows_[i];
      this->rows_[--this->size_].~BindingT();
    }
    void clear()
    {
      while (this->size_)
        this->rows_[--this->size_].~BindingT();
    }
    void release()
    {
      this->clear();
      loka::core::LokaFreeRaw(this->rows_, site());
      this->rows_ = 0;
      this->capacity_ = 0;
    }
    /** Transfer a prepared allocation and its constructed rows; leave source empty. */
    void take(Storage &source)
    {
      this->release();
      this->rows_ = source.rows_;
      this->size_ = source.size_;
      this->capacity_ = source.capacity_;
      source.rows_ = 0;
      source.size_ = source.capacity_ = 0;
    }

  private:
    static loka::core::LokaAllocationSite site()
    {
      return loka::core::LokaAllocationSite("ToolboxEditControlLedger", "Rows");
    }
    Storage(const Storage &);
    Storage &operator=(const Storage &);
    BindingT *rows_;
    std::size_t size_;
    std::size_t capacity_;
  };
  Storage bindings_;
  Storage candidate_;
  ToolboxFocusedEditIndex focused_;
};

#endif // LOKA_TOOLBOX_EDIT_CONTROL_LEDGER_HPP
