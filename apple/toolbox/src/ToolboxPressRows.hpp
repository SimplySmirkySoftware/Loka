#ifndef LOKA_TOOLBOX_PRESS_ROWS_HPP
#define LOKA_TOOLBOX_PRESS_ROWS_HPP

#include "ToolboxPressLedger.hpp"
#include "core/State.hpp"
#include "core/String.hpp"
namespace loka
{
  namespace app
  {
    namespace scene
    {
      class NodeContext;
    }
  } // namespace app
} // namespace loka

/** Candidates collected during one retirement, without deduplication.
    The controller unbinds only after every store and TextHit have retired. */
struct ToolboxReleasedStates
{
  std::vector<loka::core::State<loka::core::String> *> texts;
  std::vector<loka::core::State<bool> *> enableds;
};
class ToolboxPressStoreChain;

/** Non-owning link, enrolled in declaration order. Only the ledger owns stores. */
class ToolboxPressStore
{
public:
  virtual void retire(const loka::app::scene::NodeContext *, ToolboxPressLedger &, ToolboxReleasedStates &) = 0;
  virtual void clear() = 0;

protected:
  explicit ToolboxPressStore(ToolboxPressStoreChain &chain);
  ~ToolboxPressStore() {}

private:
  friend class ToolboxPressStoreChain;
  ToolboxPressStore *next_;
  ToolboxPressStore(const ToolboxPressStore &);
  ToolboxPressStore &operator=(const ToolboxPressStore &);
};

/** Append-only, non-owning enrollment chain. Traverse only while the containing
    ledger is fully constructed, never during construction or destruction.
    Destruction deliberately neither traverses nor unlinks its expired links. */
class ToolboxPressStoreChain
{
public:
  ToolboxPressStoreChain()
      : first_(0),
        last_(0)
  {
  }
  void retire(const loka::app::scene::NodeContext *context, ToolboxPressLedger &ledger, ToolboxReleasedStates &released)
  {
    for (ToolboxPressStore *store = this->first_; store; store = store->next_)
      store->retire(context, ledger, released);
  }
  void clear()
  {
    for (ToolboxPressStore *store = this->first_; store; store = store->next_)
      store->clear();
  }

private:
  friend class ToolboxPressStore;
  void append(ToolboxPressStore &store)
  {
    if (this->last_)
      this->last_->next_ = &store;
    else
      this->first_ = &store;
    this->last_ = &store;
  }
  ToolboxPressStore *first_;
  ToolboxPressStore *last_;
  ToolboxPressStoreChain(const ToolboxPressStoreChain &);
  ToolboxPressStoreChain &operator=(const ToolboxPressStoreChain &);
};
inline ToolboxPressStore::ToolboxPressStore(ToolboxPressStoreChain &chain)
    : next_(0)
{
  chain.append(*this);
}

/** Typed passive payloads. Hit must provide the exact static release policy;
    clear and destruction release storage only, without collecting candidates. */
template <class Hit, PressRow::Kind KIND> class ToolboxPressRows : public ToolboxPressStore
{
public:
  typedef typename std::vector<Hit>::iterator iterator;
  typedef typename std::vector<Hit>::const_iterator const_iterator;
  explicit ToolboxPressRows(ToolboxPressStoreChain &chain)
      : ToolboxPressStore(chain)
  {
    void (*check)(const Hit &, ToolboxReleasedStates &) = &Hit::release;
    (void)check;
  }
  std::size_t store(const Hit &hit)
  {
    this->rows_.push_back(hit);
    return this->rows_.size() - 1;
  }
  void reach(std::size_t index, ToolboxPressLedger &ledger)
  {
    ledger.append(KIND, index);
  }
  void record(const Hit &hit, ToolboxPressLedger &ledger)
  {
    this->reach(this->store(hit), ledger);
  }
  std::size_t size() const
  {
    return this->rows_.size();
  }
  bool empty() const
  {
    return this->rows_.empty();
  }
  Hit &operator[](std::size_t index)
  {
    return this->rows_[index];
  }
  const Hit &operator[](std::size_t index) const
  {
    return this->rows_[index];
  }
  iterator begin()
  {
    return this->rows_.begin();
  }
  iterator end()
  {
    return this->rows_.end();
  }
  const_iterator begin() const
  {
    return this->rows_.begin();
  }
  const_iterator end() const
  {
    return this->rows_.end();
  }
  virtual void retire(const loka::app::scene::NodeContext *, ToolboxPressLedger &, ToolboxReleasedStates &);
  virtual void clear()
  {
    this->rows_.clear();
  }

private:
  std::vector<Hit> rows_;
};
#endif
