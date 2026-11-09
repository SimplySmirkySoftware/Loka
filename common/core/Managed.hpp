#ifndef LOKA_CORE_MANAGED_HPP
#define LOKA_CORE_MANAGED_HPP

#include <cstddef>
#include <cstdlib>
#include <functional>
#include "core/LokaAlloc.hpp"

namespace loka
{
  namespace core
  {
    /** Allocation identity shared by every Managed control block. */
    /**
     * The one allocation site of every Managed control block. Built at each
     * use rather than held in a namespace-scope object: a namespace-scope
     * object has dynamic initialization in C++98, so a Managed constructed
     * during another translation unit's static initialization could allocate
     * under zeroed tags and free under the real ones.
     */
    inline LokaAllocationSite ManagedControlBlockSite()
    {
      return LokaAllocationSite("Managed", "ControlBlock");
    }

    // Managed<T>: Simple intrusive reference-counted handle for platform resources.
    // - Wraps a pointer to T plus an optional releaser callback.
    // - Copying increments the refcount, destruction decrements it.
    // - When the count reaches zero, the releaser is invoked and the control block is destroyed.
    template <typename T> class Managed
    {
    public:
      typedef void (*ReleaserFn)(T *value, void *userData);

      Managed()
          : block_(0)
      {
      }
      Managed(const Managed &other)
          : block_(other.block_)
      {
        retain();
      }
      Managed &operator=(const Managed &other)
      {
        if (this != &other)
        {
          release();
          block_ = other.block_;
          retain();
        }
        return *this;
      }
      ~Managed()
      {
        release();
      }

      /** Adopts non-null value or aborts on allocation refusal; null stays empty. */
      static Managed<T> Wrap(T *value, ReleaserFn releaser = &Managed::DefaultDelete, void *userData = 0)
      {
        const Managed<T> managed = TryWrap(value, releaser, userData);
        if (value && !managed.isValid())
          std::abort();
        return managed;
      }

      /**
       * Adopts value only on success. On refusal the caller still owns value;
       * no releaser is invoked. All blocks use ManagedControlBlockSite();
       * the allocation backend must remain installed until release.
       */
      static Managed<T> TryWrap(T *value, ReleaserFn releaser, void *userData)
      {
        if (!value)
          return Managed<T>();
        void *storage = LokaAllocRaw(sizeof(ControlBlock), ManagedControlBlockSite());
        if (!storage)
          return Managed<T>();
        return Managed<T>(new (storage) ControlBlock(value, releaser, userData));
      }

      T *get() const
      {
        return block_ ? block_->value : 0;
      }
      T &operator*() const
      {
        return *block_->value;
      }
      T *operator->() const
      {
        return block_ ? block_->value : 0;
      }
      bool isValid() const
      {
        return block_ && block_->value;
      }
      int useCount() const
      {
        return block_ ? block_->refCount : 0;
      }

      void reset()
      {
        release();
        block_ = 0;
      }

      bool operator==(const Managed &other) const
      {
        return block_ == other.block_;
      }
      bool operator!=(const Managed &other) const
      {
        return block_ != other.block_;
      }

      /** Strict control-block identity order consistent with ==; no handle sorts first. */
      bool identityBefore(const Managed &other) const
      {
        if (!this->block_ || !other.block_)
          return !this->block_ && other.block_;
        return std::less<const ControlBlock *>()(this->block_, other.block_);
      }

    private:
      struct ControlBlock
      {
        ControlBlock(T *ptr, ReleaserFn rel, void *ud)
            : value(ptr),
              releaser(rel),
              userData(ud),
              refCount(1)
        {
        }
        T *value;
        ReleaserFn releaser;
        void *userData;
        int refCount;
      };

      explicit Managed(ControlBlock *block)
          : block_(block)
      {
      }

      void retain()
      {
        if (block_)
          ++block_->refCount;
      }
      void release()
      {
        if (!block_)
          return;
        --block_->refCount;
        if (block_->refCount == 0)
        {
          if (block_->releaser)
            block_->releaser(block_->value, block_->userData);
          block_->~ControlBlock();
          LokaFreeRaw(block_, ManagedControlBlockSite());
        }
        block_ = 0;
      }

      static void DefaultDelete(T *value, void *)
      {
        delete value;
      }

      ControlBlock *block_;
    };
  } // namespace core
} // namespace loka

#endif // LOKA_CORE_MANAGED_HPP
