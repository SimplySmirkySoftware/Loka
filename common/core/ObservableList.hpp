#ifndef LOKA_CORE_OBSERVABLE_LIST_HPP
#define LOKA_CORE_OBSERVABLE_LIST_HPP

#include "core/State.hpp"
#include "core/LokaAlloc.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <climits>

namespace loka
{
  namespace core
  {

    /** Stable list identity. Generation 65535 is reserved for mirror-local IDs. */
    struct ItemId
    {
      unsigned short generation;
      unsigned short seq;
      ItemId(unsigned short g = 0, unsigned short s = 0)
          : generation(g),
            seq(s)
      {
      }
      static ItemId none()
      {
        return ItemId();
      }
      bool isNone() const
      {
        return this->generation == 0 && this->seq == 0;
      }
      bool operator==(const ItemId &other) const
      {
        return this->generation == other.generation && this->seq == other.seq;
      }
      bool operator!=(const ItemId &other) const
      {
        return !(*this == other);
      }
    };
    typedef char ItemIdMustBeFourBytes[sizeof(ItemId) == 4 ? 1 : -1];

    enum ListChangeKind
    {
      LIST_NONE,
      LIST_INSERT,
      LIST_REMOVE,
      LIST_UPDATE,
      LIST_MOVE,
      LIST_BATCH
    };
    struct ListChange
    {
      ListChangeKind kind;
      unsigned short first;
      unsigned short count;
      ListChange(ListChangeKind k = LIST_NONE, unsigned short f = 0, unsigned short n = 0)
          : kind(k),
            first(f),
            count(n)
      {
      }
      bool operator==(const ListChange &other) const
      {
        return !(*this != other);
      }
      bool operator!=(const ListChange &other) const
      {
        return this->kind != other.kind || this->first != other.first || this->count != other.count;
      }
    };
    /** Completed publication; change summarizes only the last successful door. */
    struct ListRevision
    {
      unsigned long structure;
      unsigned long content;
      ListChange change;
      ListRevision()
          : structure(0),
            content(0),
            change()
      {
      }
      bool operator==(const ListRevision &other) const
      {
        return !(*this != other);
      }
      bool operator!=(const ListRevision &other) const
      {
        return this->structure != other.structure || this->content != other.content || this->change != other.change;
      }
    };
    typedef char ListRevisionMustStaySmall[sizeof(ListRevision) <= 32 ? 1 : -1];

    enum ListOpKind
    {
      INSERT,
      REMOVE,
      UPDATE,
      MOVE
    };
    /** INSERT receives a fresh model ID; id is a cursor's optional local label.
        toIndex is the final index for MOVE, or the insertion position for INSERT. */
    template <class T> struct ListOp
    {
      ListOpKind kind;
      ItemId id;
      unsigned short toIndex;
      T after;
      ListOp(ListOpKind k = INSERT, ItemId i = ItemId(), unsigned short to = 0, const T &a = T())
          : kind(k),
            id(i),
            toIndex(to),
            after(a)
      {
      }
    };
    /** Borrowed, stable replay source. rewind restores the same sequence; next and
        rewind must neither edit the list nor allocate. INSERT labels are not
        resolved by the model: adapters translate later targets to issued IDs. */
    template <class T> class ListOpCursor
    {
    public:
      virtual ~ListOpCursor() {}
      virtual void rewind() = 0;
      virtual bool next(ListOp<T> &out) = 0;
    };
    template <class T> class ArrayListOpCursor : public ListOpCursor<T>
    {
    public:
      ArrayListOpCursor(const ListOp<T> *ops, std::size_t count)
          : ops_(ops),
            count_(count),
            index_(0)
      {
        assert(ops || count == 0);
      }
      void rewind()
      {
        this->index_ = 0;
      }
      bool next(ListOp<T> &out)
      {
        if (this->index_ == this->count_)
          return false;
        out = this->ops_[this->index_++];
        return true;
      }

    private:
      const ListOp<T> *ops_;
      std::size_t count_;
      std::size_t index_;
    };

    enum ListAttachResult
    {
      ATTACH_OK,
      ATTACH_ALLOCATION_REFUSED,
      ATTACH_ALREADY_ATTACHED,
      ATTACH_NULL_TRACKER,
      ATTACH_GENERATION_EXHAUSTED
    };
    enum ListEditResult
    {
      EDIT_OK,
      EDIT_CAPACITY_EXCEEDED,
      EDIT_ID_NOT_FOUND,
      EDIT_INDEX_OUT_OF_RANGE,
      EDIT_SEQ_EXHAUSTED,
      EDIT_NOT_ATTACHED,
      EDIT_REENTRANT,
      EDIT_GENERATION_EXHAUSTED
    };
    enum ListPhase
    {
      LIST_IDLE,
      LIST_PUBLISHING
    };

    namespace list_detail
    {
      /** Owns constructed entries, including unused capacity. T must be default
          constructible, copyable without allocation/throwing, and have operator!=.
          Default construction and copying must not allocate. Destruction and
        assignment must not call back into either list owner. */
      template <class T> class EntryBuffer
      {
      public:
        struct Entry
        {
          ItemId id;
          T value;
          Entry()
              : id(),
                value()
          {
          }
        };
        // reserve() takes an unsigned short, so the largest request is USHRT_MAX
        // entries; refusing at compile time any Entry whose USHRT_MAX copies
        // would overflow std::size_t keeps sizeof(Entry) * capacity exact
        // without a runtime comparison that GCC 4.2 proves always false.
        typedef char EntryArrayMustFitSize[(sizeof(Entry) <= static_cast<std::size_t>(-1) / USHRT_MAX) ? 1 : -1];
        explicit EntryBuffer(const LokaAllocationSite &site)
            : site_(site),
              data_(0),
              capacity_(0)
        {
        }
        ~EntryBuffer()
        {
          this->clear();
        }
        bool reserve(unsigned short capacity)
        {
          assert(!this->data_);
          if (capacity == 0)
            return true;
          this->data_ = static_cast<Entry *>(LokaAllocRaw(sizeof(Entry) * capacity, this->site_));
          if (!this->data_)
            return false;
          this->capacity_ = capacity;
          for (unsigned int i = 0; i < capacity; ++i)
            new (this->data_ + i) Entry();
          return true;
        }
        void clear()
        {
          for (unsigned int i = 0; i < this->capacity_; ++i)
            this->data_[i].~Entry();
          LokaFreeRaw(this->data_, this->site_);
          this->data_ = 0;
          this->capacity_ = 0;
        }
        unsigned short capacity() const
        {
          return this->capacity_;
        }
        const Entry &at(unsigned short i) const
        {
          assert(i < this->capacity_);
          return this->data_[i];
        }
        int find(ItemId id, unsigned short size) const
        {
          for (unsigned int i = 0; i < size; ++i)
            if (this->data_[i].id == id)
              return static_cast<int>(i);
          return -1;
        }
        void copy(const EntryBuffer &from, unsigned short size, bool values)
        {
          assert(size <= this->capacity_);
          for (unsigned int i = 0; i < size; ++i)
            this->copyEntry(i, from.data_[i], values);
        }
        void swap(EntryBuffer &other)
        {
          assert(this->capacity_ == other.capacity_);
          Entry *old = this->data_;
          this->data_ = other.data_;
          other.data_ = old;
        }
        ListEditResult validate(const ListOp<T> &op, unsigned short size) const
        {
          if (op.kind == INSERT)
          {
            if (size == this->capacity_)
              return EDIT_CAPACITY_EXCEEDED;
            return op.toIndex <= size ? EDIT_OK : EDIT_INDEX_OUT_OF_RANGE;
          }
          if (this->find(op.id, size) < 0)
            return EDIT_ID_NOT_FOUND;
          if (op.kind == MOVE && op.toIndex >= size)
            return EDIT_INDEX_OUT_OF_RANGE;
          return EDIT_OK;
        }
        /** Validated movement, shared by the identity pre-pass and value replay. */
        unsigned short edit(const ListOp<T> &op, ItemId inserted, unsigned short &size, bool values)
        {
          unsigned short index = op.kind == INSERT ? op.toIndex : static_cast<unsigned short>(this->find(op.id, size));
          switch (op.kind)
          {
          case INSERT:
            for (unsigned int i = size; i > index; --i)
              this->copyEntry(i, this->data_[i - 1], values);
            this->data_[index].id = inserted;
            if (values)
              this->data_[index].value = op.after;
            ++size;
            break;
          case REMOVE:
            for (unsigned int i = index; i + 1 < size; ++i)
              this->copyEntry(i, this->data_[i + 1], values);
            --size;
            break;
          case UPDATE:
            if (values)
              this->data_[index].value = op.after;
            break;
          case MOVE:
          {
            Entry saved;
            saved.id = this->data_[index].id;
            if (values)
              saved.value = this->data_[index].value;
            for (unsigned int i = index; i < op.toIndex; ++i)
              this->copyEntry(i, this->data_[i + 1], values);
            for (unsigned int i = index; i > op.toIndex; --i)
              this->copyEntry(i, this->data_[i - 1], values);
            this->copyEntry(op.toIndex, saved, values);
            index = op.toIndex;
          }
          break;
          }
          return index;
        }

      private:
        EntryBuffer(const EntryBuffer &);
        EntryBuffer &operator=(const EntryBuffer &);
        void copyEntry(unsigned int index, const Entry &entry, bool values)
        {
          this->data_[index].id = entry.id;
          if (values)
            this->data_[index].value = entry.value;
        }
        LokaAllocationSite site_;
        Entry *data_;
        unsigned short capacity_;
      };
      inline ListChangeKind changeKind(ListOpKind kind)
      {
        switch (kind)
        {
        case INSERT:
          return LIST_INSERT;
        case REMOVE:
          return LIST_REMOVE;
        case UPDATE:
          return LIST_UPDATE;
        case MOVE:
          return LIST_MOVE;
        }
        assert(false);
        return LIST_NONE;
      }
    } // namespace list_detail

    namespace testing
    {
      struct ObservableListAccessForTesting;
    }
    template <class T> class MirroredList;
    /** Data-only bounded owner. The tracker must outlive attachment; mirrors must
        be destroyed before this model. Owners must not destroy a list or its
        tracker from an active door/notification. Runtime list algorithms allocate nothing;
        State notification and tracker internals retain their existing costs.
        Each successful edit opens one guard and writes one completed revision.
        Nested guards join the owner's transaction. Single edits walk this list's
        rows for lookup/movement, O(capacity); apply uses identity validation then value
        replay in its reserved scratch buffer (O(ops * capacity)). */
    template <class T> class ObservableList
    {
    public:
      typedef typename list_detail::EntryBuffer<T>::Entry Entry;
      ObservableList()
          : items_(LokaAllocationSite("ObservableList", "items")),
            temp_(LokaAllocationSite("ObservableList", "items")),
            tracker_(0),
            revision_(ListRevision()),
            size_(0),
            generation_(0),
            seq_(0),
            phase_(LIST_IDLE)
      {
      }
      ~ObservableList()
      {
        assert(this->phase_ == LIST_IDLE);
        this->detach();
      }
      ListAttachResult attach(PushStateTracker *tracker, unsigned short capacity)
      {
        if (this->tracker_)
          return ATTACH_ALREADY_ATTACHED;
        if (!tracker)
          return ATTACH_NULL_TRACKER;
        if (this->generation_ == 65534)
          return ATTACH_GENERATION_EXHAUSTED;
        if (!this->items_.reserve(capacity) || !this->temp_.reserve(capacity))
        {
          this->items_.clear();
          this->temp_.clear();
          return ATTACH_ALLOCATION_REFUSED;
        }
        ++this->generation_;
        this->seq_ = 0;
        this->tracker_ = tracker;
        this->tracker_->addState(&this->revision_);
        return ATTACH_OK;
      }
      /** Detach is idempotent, but refuses from a publication callback. Borrowers
          must unbind before destruction. No notification is emitted by detach. */
      ListEditResult detach()
      {
        if (this->phase_ != LIST_IDLE)
          return EDIT_REENTRANT;
        if (this->tracker_)
          this->tracker_->removeState(&this->revision_);
        this->tracker_ = 0;
        this->size_ = 0;
        this->items_.clear();
        this->temp_.clear();
        return EDIT_OK;
      }
      ListEditResult insert(unsigned short index, const T &value, ItemId *outId = 0)
      {
        return this->edit(ListOp<T>(INSERT, ItemId(), index, value), outId);
      }
      ListEditResult remove(ItemId id)
      {
        return this->edit(ListOp<T>(REMOVE, id), 0);
      }
      ListEditResult update(ItemId id, const T &value)
      {
        return this->edit(ListOp<T>(UPDATE, id, 0, value), 0);
      }
      ListEditResult move(ItemId id, unsigned short toIndex)
      {
        return this->edit(ListOp<T>(MOVE, id, toIndex), 0);
      }
      /** Empty apply is a successful no-op with no publication. Both passes must
          see identical cursor output. Failed validation may change scratch IDs,
          but never live entries, sequence allocation, or the revision. */
      ListEditResult apply(ListOpCursor<T> &ops)
      {
        ListEditResult ready = this->ready();
        if (ready != EDIT_OK)
          return ready;
        Publication publication(*this);
        StateTrackerGuard guard(this->tracker_);
        this->temp_.copy(this->items_, this->size_, false);
        unsigned short size = this->size_, seq = this->seq_;
        unsigned int count = 0;
        bool structure = false, content = false;
        ListChange change;
        ListOp<T> op;
        ops.rewind();
        while (ops.next(op))
        {
          ListEditResult result = this->temp_.validate(op, size);
          if (result != EDIT_OK)
            return result;
          if (op.kind == INSERT && seq == 65535)
            return EDIT_SEQ_EXHAUSTED;
          ItemId inserted(this->generation_, op.kind == INSERT ? ++seq : 0);
          unsigned short index = this->temp_.edit(op, inserted, size, false);
          if (op.kind == UPDATE)
            content = true;
          else
            structure = true;
          if (count < 2)
            ++count;
          change = count == 1 ? ListChange(list_detail::changeKind(op.kind), index, 1) : ListChange(LIST_BATCH);
        }
        if (count == 0)
          return EDIT_OK;
        this->temp_.copy(this->items_, this->size_, true);
        size = this->size_;
        seq = this->seq_;
        ops.rewind();
        while (ops.next(op))
          this->temp_.edit(op, ItemId(this->generation_, op.kind == INSERT ? ++seq : 0), size, true);
        this->items_.swap(this->temp_);
        this->size_ = size;
        this->seq_ = seq;
        this->publish(structure, content, change);
        return EDIT_OK;
      }
      /** Clear starts a fresh identity generation, even when already empty. */
      ListEditResult reset()
      {
        ListEditResult ready = this->ready();
        if (ready != EDIT_OK)
          return ready;
        if (this->generation_ == 65534)
          return EDIT_GENERATION_EXHAUSTED;
        Publication publication(*this);
        StateTrackerGuard guard(this->tracker_);
        this->size_ = 0;
        this->seq_ = 0;
        ++this->generation_;
        this->publish(true, false, ListChange(LIST_BATCH));
        return EDIT_OK;
      }
      unsigned short size() const
      {
        return this->size_;
      }
      unsigned short capacity() const
      {
        return this->items_.capacity();
      }
      const Entry &at(unsigned short index) const
      {
        assert(index < this->size_);
        return this->items_.at(index);
      }
      int find(ItemId id) const
      {
        return this->items_.find(id, this->size_);
      }
      /** Reports edit readiness and the borrowed attachment tracker (null when
          detached). Read-only projections may use the owner during publication;
          mutations must require EDIT_OK. Never retain it past attachment. */
      ListEditResult queryMutationTracker(StateTracker *&out) const
      {
        const ListEditResult result = this->ready();
        out = this->tracker_;
        return result;
      }
      const State<ListRevision> &revision() const
      {
        return this->revision_;
      }

    private:
      friend class MirroredList<T>;
      friend struct testing::ObservableListAccessForTesting;
      ObservableList(const ObservableList &);
      ObservableList &operator=(const ObservableList &);
      ListEditResult ready() const
      {
        if (!this->tracker_)
          return EDIT_NOT_ATTACHED;
        return this->phase_ == LIST_IDLE ? EDIT_OK : EDIT_REENTRANT;
      }
      ListEditResult edit(const ListOp<T> &op, ItemId *outId)
      {
        ListEditResult result = this->ready();
        if (result != EDIT_OK)
          return result;
        Publication publication(*this);
        StateTrackerGuard guard(this->tracker_);
        result = this->items_.validate(op, this->size_);
        if (result != EDIT_OK)
          return result;
        if (op.kind == INSERT && this->seq_ == 65535)
          return EDIT_SEQ_EXHAUSTED;
        ItemId inserted(this->generation_, op.kind == INSERT ? ++this->seq_ : 0);
        unsigned short index = this->items_.edit(op, inserted, this->size_, true);
        if (outId)
          *outId = inserted;
        this->publish(op.kind != UPDATE, op.kind == UPDATE, ListChange(list_detail::changeKind(op.kind), index, 1));
        return EDIT_OK;
      }
      /** Declared before the tracker guard, so the phase covers its settlement
          callbacks too. An enclosing transaction settles later under its owner. */
      class Publication
      {
      public:
        explicit Publication(ObservableList &list)
            : list_(list)
        {
          this->list_.phase_ = LIST_PUBLISHING;
        }
        ~Publication()
        {
          this->list_.phase_ = LIST_IDLE;
        }

      private:
        Publication(const Publication &);
        Publication &operator=(const Publication &);
        ObservableList &list_;
      };
      void publish(bool structure, bool content, const ListChange &change)
      {
        ListRevision revision = this->revision_.get();
        if (structure)
          ++revision.structure;
        if (content)
          ++revision.content;
        revision.change = change;
        this->revision_.set(revision);
      }
      list_detail::EntryBuffer<T> items_;
      list_detail::EntryBuffer<T> temp_;
      PushStateTracker *tracker_;
      MutableState<ListRevision> revision_;
      unsigned short size_;
      unsigned short generation_;
      unsigned short seq_;
      ListPhase phase_;
    };

  } // namespace core
} // namespace loka
#endif
