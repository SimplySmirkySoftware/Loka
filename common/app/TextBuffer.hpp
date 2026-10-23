#ifndef LOKA_APP_TEXT_BUFFER_HPP
#define LOKA_APP_TEXT_BUFFER_HPP
#include <cassert>
#include <new>
#include "app/PlatformContext.hpp"
#include "core/ObservableList.hpp"
#include "core/String.hpp"
namespace loka
{
  namespace app
  {
    /** Fixed handle storage. Construction finishes before allocation-free replay. */
    class PreparedTextReplacement
    {
    public:
      PreparedTextReplacement()
          : rows_(0),
            capacity_(0),
            count_(0),
            phase_(BUILDING)
      {
      }
      ~PreparedTextReplacement()
      {
        this->release();
      }
      bool reserve(unsigned short capacity)
      {
        if (this->rows_)
          return false;
        if (!capacity || capacity > static_cast<std::size_t>(-1) / sizeof(core::String))
          return false;
        this->rows_ = static_cast<core::String *>(
            core::LokaAllocRaw(sizeof(core::String) * capacity, core::LokaAllocationSite("TextBuffer", "handles")));
        if (!this->rows_)
          return false;
        this->capacity_ = capacity;
        for (std::size_t i = 0; i < capacity; ++i)
          new (this->rows_ + i) core::String();
        return true;
      }
      bool append(const core::String &row)
      {
        if (this->phase_ != BUILDING || this->count_ == this->capacity_)
          return false;
        this->rows_[this->count_++] = row;
        return true;
      }
      void seal()
      {
        this->phase_ = SEALED;
      }
      const core::String *data() const
      {
        assert(this->phase_ == SEALED);
        return this->rows_;
      }
      unsigned short size() const
      {
        return this->count_;
      }

    private:
      friend class EditWorkspace;
      friend class TextBuffer;
      PreparedTextReplacement(const PreparedTextReplacement &);
      PreparedTextReplacement &operator=(const PreparedTextReplacement &);
      void clear()
      {
        for (std::size_t i = 0; i < this->count_; ++i)
          this->rows_[i] = core::String();
        this->count_ = 0;
        this->phase_ = BUILDING;
      }
      void release()
      {
        for (std::size_t i = 0; i < this->capacity_; ++i)
          this->rows_[i].~String();
        core::LokaFreeRaw(this->rows_, core::LokaAllocationSite("TextBuffer", "handles"));
        this->rows_ = 0;
        this->capacity_ = 0;
        this->count_ = 0;
        this->phase_ = BUILDING;
      }
      core::String *rows_;
      unsigned short capacity_, count_;
      enum Phase
      {
        BUILDING,
        SEALED
      } phase_;
    };
    class EditScope;
    /** Attach-owned reusable storage; one scoped borrower holds it through publication. */
    class EditWorkspace
    {
    public:
      EditWorkspace()
          : replacement_(),
            borrower_(0)
      {
      }
      ~EditWorkspace()
      {
        assert(!this->borrower_);
      }

    private:
      friend class TextBuffer;
      friend class EditScope;
      EditWorkspace(const EditWorkspace &);
      EditWorkspace &operator=(const EditWorkspace &);
      bool borrow(EditScope &scope);
      void finish()
      {
        this->replacement_.clear();
        this->borrower_ = 0;
      }
      PreparedTextReplacement replacement_;
      EditScope *borrower_;
    };
    /** Exclusive edit borrow. Destruction clears handles after synchronous publication. */
    class EditScope
    {
    public:
      EditScope()
          : workspace_(0)
      {
      }
      ~EditScope()
      {
        if (this->workspace_)
          this->workspace_->finish();
      }
      PreparedTextReplacement &replacement()
      {
        assert(this->workspace_);
        return this->workspace_->replacement_;
      }

    private:
      friend class EditWorkspace;
      EditScope(const EditScope &);
      EditScope &operator=(const EditScope &);
      EditWorkspace *workspace_;
    };
    inline bool EditWorkspace::borrow(EditScope &scope)
    {
      if (this->borrower_ || scope.workspace_)
        return false;
      this->borrower_ = &scope;
      scope.workspace_ = this;
      return true;
    }
    class TextBuffer;
    /** Borrowed mutation/observation facade; attachment authority remains with TextBuffer. */
    class TextLines
    {
    public:
      typedef core::ObservableList<core::String>::Entry Entry;
      const core::State<core::ListRevision> &revision() const
      {
        return this->list_.revision();
      }
      unsigned short size() const
      {
        return this->list_.size();
      }
      const Entry &at(unsigned short index) const
      {
        return this->list_.at(index);
      }
      int find(core::ItemId id) const
      {
        return this->list_.find(id);
      }
      core::ListEditResult apply(core::ListOpCursor<core::String> &cursor)
      {
        return this->list_.apply(cursor);
      }
      core::ListEditResult update(core::ItemId id, const core::String &row)
      {
        return this->list_.update(id, row);
      }
      core::ListEditResult queryMutationTracker(core::StateTracker *&out) const
      {
        return this->list_.queryMutationTracker(out);
      }
      const TextCapacity &capacity() const
      {
        return this->capacity_;
      }

    private:
      friend class TextBuffer;
      TextLines(core::ObservableList<core::String> &list, const TextCapacity &capacity)
          : list_(list),
            capacity_(capacity)
      {
      }
      TextLines(const TextLines &);
      TextLines &operator=(const TextLines &);
      core::ObservableList<core::String> &list_;
      const TextCapacity &capacity_;
    };
    /** App-owned document. Capacity is immutable after successful admission until owner reclaim. */
    class TextBuffer
    {
    public:
      enum AttachResult
      {
        ATTACH_OK,
        ATTACH_RAIL_DECLINED,
        ATTACH_OVER_RAIL_MAXIMUM,
        ATTACH_EMPTY_CAPACITY,
        ATTACH_ALLOCATION_REFUSED,
        ATTACH_ALREADY_ATTACHED,
        ATTACH_NULL_TRACKER,
        ATTACH_GENERATION_EXHAUSTED
      };
      TextBuffer()
          : list_(),
            capacity_(TextCapacity::of(0, 0)),
            lines_(this->list_, this->capacity_),
            workspace_()
      {
      }
      ~TextBuffer()
      {
        core::StateTracker *tracker = 0;
        const core::ListEditResult ready = this->list_.queryMutationTracker(tracker);
        assert(ready != core::EDIT_REENTRANT && "TextBuffer reclaim requires quiescence");
        (void)ready;
      }
      AttachResult attach(core::PushStateTracker *tracker, const PlatformContext &rail, TextCapacity want)
      {
        if (this->capacity_.lines)
          return ATTACH_ALREADY_ATTACHED;
        TextCapacity maximum;
        if (!rail.queryTextEditorCapacity(maximum))
          return ATTACH_RAIL_DECLINED;
        if (!want.fitsIn(maximum))
          return ATTACH_OVER_RAIL_MAXIMUM;
        if (!want.lines || !want.bytes)
          return ATTACH_EMPTY_CAPACITY;
        if (!this->workspace_.replacement_.reserve(want.lines))
          return ATTACH_ALLOCATION_REFUSED;
        const core::ListAttachResult result = this->list_.attach(tracker, want.lines);
        if (result == core::ATTACH_OK)
        {
          this->capacity_ = want;
          return ATTACH_OK;
        }
        this->workspace_.replacement_.release();
        switch (result)
        {
        case core::ATTACH_OK:
          break;
        case core::ATTACH_ALREADY_ATTACHED:
          return ATTACH_ALREADY_ATTACHED;
        case core::ATTACH_NULL_TRACKER:
          return ATTACH_NULL_TRACKER;
        case core::ATTACH_GENERATION_EXHAUSTED:
          return ATTACH_GENERATION_EXHAUSTED;
        case core::ATTACH_ALLOCATION_REFUSED:
          return ATTACH_ALLOCATION_REFUSED;
        }
        return ATTACH_ALLOCATION_REFUSED;
      }
      TextLines &lines()
      {
        return this->lines_;
      }
      const TextLines &lines() const
      {
        return this->lines_;
      }
      const TextCapacity &capacity() const
      {
        return this->capacity_;
      }
      bool borrowWorkspace(EditScope &scope)
      {
        return this->capacity_.lines && this->workspace_.borrow(scope);
      }

    private:
      TextBuffer(const TextBuffer &);
      TextBuffer &operator=(const TextBuffer &);
      core::ObservableList<core::String> list_;
      TextCapacity capacity_;
      TextLines lines_;
      EditWorkspace workspace_;
    };
  } // namespace app
} // namespace loka
#endif
