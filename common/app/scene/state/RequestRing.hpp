#ifndef LOKA_APP_SCENE_STATE_REQUEST_RING_HPP
#define LOKA_APP_SCENE_STATE_REQUEST_RING_HPP
#include <cassert>
namespace loka
{
  namespace app
  {
    namespace scene
    {
      /** Compile-time capacity wall shared by fixed ring holders. */
      template <unsigned N> struct RequestRingCapacity
      {
        typedef char Wall[(N >= 1 && N <= 255) ? 1 : -1];
      };
      /** Fixed FIFO mechanics over borrowed storage; policy lives in the holder.
          Vacated slots receive T::None() so payload resources are not retained.
          The holder supplies capacity slots initialized to None before use.
          Construction never touches storage: a most-derived holder constructs
          its storage after the base that owns this ring. */
      template <typename T> class RequestRing
      {
      public:
        RequestRing(T *storage, unsigned char capacity)
            : ring_(storage),
              cap_(capacity),
              head_(0),
              count_(0)
        {
          assert(capacity >= 1);
        }
        bool pushBack(const T &value)
        {
          if (this->count_ == this->cap_)
            return false;
          this->ring_[(this->head_ + this->count_) % this->cap_] = value;
          ++this->count_;
          return true;
        }
        bool advance(T &next)
        {
          if (!this->count_)
            return false;
          next = this->ring_[this->head_];
          this->ring_[this->head_] = T::None();
          this->head_ = (this->head_ + 1) % this->cap_;
          --this->count_;
          return true;
        }
        void clear()
        {
          while (this->count_)
          {
            this->ring_[this->head_] = T::None();
            this->head_ = (this->head_ + 1) % this->cap_;
            --this->count_;
          }
          this->head_ = 0;
        }
        unsigned count() const
        {
          return this->count_;
        }

      private:
        RequestRing(const RequestRing &);
        RequestRing &operator=(const RequestRing &);
        T *ring_;
        unsigned char cap_, head_, count_;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
