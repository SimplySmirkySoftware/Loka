#ifndef LOKA_APP_SCENE_TWIN_LINK_HPP
#define LOKA_APP_SCENE_TWIN_LINK_HPP
namespace loka
{
  namespace app
  {
    namespace scene
    {
      /** Non-owning, callback-free pair. Destroying either endpoint severs both ends. */
      template <class Local, class Remote> class TwinLink
      {
      public:
        explicit TwinLink(Local *local = 0)
            : local_(local),
              peer_(0)
        {
        }
        ~TwinLink()
        {
          this->cut();
        }
        void connect(TwinLink<Remote, Local> &other)
        {
          this->cut();
          other.cut();
          if (static_cast<const void *>(&other) == static_cast<const void *>(this))
            return;
          this->peer_ = &other;
          other.peer_ = this;
        }
        void cut()
        {
          if (!this->peer_)
            return;
          this->peer_->peer_ = 0;
          this->peer_ = 0;
        }
        Remote *remote() const
        {
          return this->peer_ ? this->peer_->local_ : 0;
        }

      private:
        TwinLink(const TwinLink &);
        TwinLink &operator=(const TwinLink &);
        Local *const local_;
        TwinLink<Remote, Local> *peer_;
        template <class A, class B> friend class TwinLink;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
