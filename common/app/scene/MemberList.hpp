#ifndef LOKA_APP_SCENE_MEMBER_LIST_HPP
#define LOKA_APP_SCENE_MEMBER_LIST_HPP
#include <cassert>
namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <class T> class MemberList;
      /** One non-owning membership, disconnected synchronously by either endpoint. */
      template <class T> class MemberHook
      {
      public:
        explicit MemberHook(T &value)
            : value_(value),
              owner_(0),
              previous_(0),
              next_(0)
        {
        }
        ~MemberHook()
        {
          this->leave();
        }
        void leave();
        MemberList<T> *owner() const
        {
          return this->owner_;
        }

      private:
        MemberHook(const MemberHook &);
        MemberHook &operator=(const MemberHook &);
        T &value_;
        MemberList<T> *owner_;
        MemberHook *previous_, *next_;
        friend class MemberList<T>;
      };
      /** Allocation-free membership; visit permits the current member to leave. */
      template <class T> class MemberList
      {
      public:
        MemberList()
            : head_(0)
        {
        }
        ~MemberList()
        {
          this->cutAll();
        }
        void join(MemberHook<T> &hook)
        {
          if (hook.owner_ == this)
            return;
          if (hook.owner_)
          {
            assert(false && "member already belongs to a list");
            return;
          }
          hook.owner_ = this;
          hook.next_ = this->head_;
          if (this->head_)
            this->head_->previous_ = &hook;
          this->head_ = &hook;
        }
        void cutAll()
        {
          while (this->head_)
            this->head_->leave();
        }
        template <class Visitor> void visit(Visitor &visitor)
        {
          for (MemberHook<T> *hook = this->head_; hook;)
          {
            MemberHook<T> *next = hook->next_;
            visitor.visit(hook->value_);
            hook = next;
          }
        }

      private:
        MemberList(const MemberList &);
        MemberList &operator=(const MemberList &);
        MemberHook<T> *head_;
        friend class MemberHook<T>;
      };
      template <class T> void MemberHook<T>::leave()
      {
        if (!this->owner_)
          return;
        if (this->previous_)
          this->previous_->next_ = this->next_;
        else
          this->owner_->head_ = this->next_;
        if (this->next_)
          this->next_->previous_ = this->previous_;
        this->owner_ = 0;
        this->previous_ = this->next_ = 0;
      }
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
