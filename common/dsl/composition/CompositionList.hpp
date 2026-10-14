#ifndef LOKA_DSL_COMPOSITION_LIST_HPP
#define LOKA_DSL_COMPOSITION_LIST_HPP

#include <vector>
#include "core/util/OwnedDef.hpp"

namespace loka
{
  namespace dsl
  {
    template <typename DefT> class CompositionList
    {
    public:
      CompositionList()
          : head_(0),
            tail_(0),
            count_(0)
      {
      }
      ~CompositionList()
      {
        clear();
      }

      size_t count() const
      {
        return count_;
      }
      DefT *head() const
      {
        return head_;
      }

      /** Append only a completed clone; report nullable refusal to the caller. */
      bool appendClone(const DefT &def)
      {
        loka::core::OwnedDef<DefT> node(def.clone());
        if (!node.isSet())
          return false;
        appendOwned(node.take());
        return true;
      }

      /** Replace with a completed copy, preserving this list on clone refusal. */
      bool copyFrom(const CompositionList<DefT> &other)
      {
        if (this == &other)
          return true;
        CompositionList<DefT> candidate;
        for (const DefT *cur = other.head(); cur; cur = cur->nextInComposition)
        {
          if (!candidate.appendClone(*cur))
            return false;
        }
        candidate.detachTo(*this);
        return true;
      }

      void appendOwned(DefT *node)
      {
        if (!node)
          return;
        node->nextInComposition = 0;
        if (!head_)
        {
          head_ = node;
          tail_ = node;
        }
        else
        {
          tail_->nextInComposition = node;
          tail_ = node;
        }
        count_ += 1;
      }

      /** Transfer the intact sibling chain without allocating or deleting. */
      DefT *detach()
      {
        DefT *out = this->head_;
        this->head_ = this->tail_ = 0;
        this->count_ = 0;
        return out;
      }

      void detachTo(std::vector<DefT *> &out)
      {
        out.clear();
        out.reserve(count_);
        while (head_)
        {
          DefT *next = head_->nextInComposition;
          head_->nextInComposition = 0;
          out.push_back(head_);
          head_ = next;
        }
        tail_ = 0;
        count_ = 0;
      }

      void detachTo(CompositionList<DefT> &out)
      {
        if (this == &out)
          return;
        out.clear();
        out.head_ = head_;
        out.tail_ = tail_;
        out.count_ = count_;
        head_ = 0;
        tail_ = 0;
        count_ = 0;
      }

      bool remove(DefT *node)
      {
        if (!node)
          return false;
        DefT *prev = 0;
        DefT *cur = head_;
        while (cur)
        {
          if (cur == node)
          {
            if (prev)
            {
              prev->nextInComposition = cur->nextInComposition;
            }
            else
            {
              head_ = cur->nextInComposition;
            }
            if (tail_ == cur)
            {
              tail_ = prev;
            }
            cur->nextInComposition = 0;
            if (count_ > 0)
            {
              count_ -= 1;
            }
            return true;
          }
          prev = cur;
          cur = cur->nextInComposition;
        }
        return false;
      }

      bool replace(DefT *oldNode, DefT *newNode)
      {
        if (!oldNode || !newNode)
        {
          return false;
        }
        DefT *prev = 0;
        DefT *cur = head_;
        while (cur)
        {
          if (cur == oldNode)
          {
            newNode->nextInComposition = cur->nextInComposition;
            if (prev)
            {
              prev->nextInComposition = newNode;
            }
            else
            {
              head_ = newNode;
            }
            if (tail_ == cur)
            {
              tail_ = newNode;
            }
            cur->nextInComposition = 0;
            return true;
          }
          prev = cur;
          cur = cur->nextInComposition;
        }
        newNode->nextInComposition = 0;
        return false;
      }

      void clear()
      {
        while (head_)
        {
          DefT *next = head_->nextInComposition;
          delete head_;
          head_ = next;
        }
        tail_ = 0;
        count_ = 0;
      }

    private:
      DefT *head_;
      DefT *tail_;
      size_t count_;
    };

    template <typename DefT> class CompositionCursor
    {
    public:
      CompositionCursor(DefT *head, size_t count)
          : current_(head),
            remaining_(count)
      {
      }

      DefT *next()
      {
        if (!current_ || remaining_ == 0)
        {
          return 0;
        }
        DefT *out = current_;
        current_ = current_->nextInComposition;
        remaining_ -= 1;
        return out;
      }

    private:
      DefT *current_;
      size_t remaining_;
    };
  } // namespace dsl
} // namespace loka

#endif // LOKA_DSL_COMPOSITION_LIST_HPP
