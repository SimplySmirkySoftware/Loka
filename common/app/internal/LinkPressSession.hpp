#ifndef LOKA_APP_INTERNAL_LINK_PRESS_SESSION_HPP
#define LOKA_APP_INTERNAL_LINK_PRESS_SESSION_HPP

#include "app/style/AttributedString.hpp"
#include <cassert>

namespace loka
{
  namespace testing
  {
    class LinkPressSessionAccess;
  }
  namespace app
  {
    namespace internal
    {
      /** Completed maximal run identity within one unchanged table value. */
      class PressedRun
      {
      public:
        PressedRun()
            : first_(static_cast<std::size_t>(-1)),
              last_(static_cast<std::size_t>(-1))
        {
        }
        explicit PressedRun(const loka::app::AttributedString::LinkRun &run)
            : first_(run.first),
              last_(run.last)
        {
        }
        std::size_t first() const
        {
          return this->first_;
        }
        std::size_t last() const
        {
          return this->last_;
        }
        bool operator==(const PressedRun &other) const
        {
          return this->first_ == other.first_ && this->last_ == other.last_;
        }

      private:
        std::size_t first_, last_;
      };

      /** Placement replaces the pending payload. It survives close until taken, invalidated or lost. */
      template <class Payload> class PressCandidate
      {
      public:
        PressCandidate()
            : present_(false)
        {
        }
        void place(const Payload &payload)
        {
          this->payload_ = payload;
          this->present_ = true;
        }
        void drop()
        {
          this->present_ = false;
        }
        bool take(Payload &out)
        {
          if (!this->present_)
            return false;
          out = this->payload_;
          this->drop();
          return true;
        }
        bool holds(const Payload &payload) const
        {
          return this->present_ && this->payload_ == payload;
        }

      private:
        Payload payload_;
        bool present_;
      };

      template <class Payload> class LinkPressProcedure;

      /** Owns one pending candidate and admits at most one active procedure.
          Only invalidate() marks the active procedure. */
      template <class Payload> class LinkPressSession
      {
      public:
        LinkPressSession()
            : active_(0)
        {
        }
        void invalidate()
        {
          this->candidate_.drop();
          if (this->active_)
            this->active_->invalidated_ = true;
        }
        void lose()
        {
          this->candidate_.drop();
        }

      private:
        friend class LinkPressProcedure<Payload>;
        friend class loka::testing::LinkPressSessionAccess;
        bool enter(LinkPressProcedure<Payload> &procedure)
        {
          if (this->active_)
            return false;
          this->active_ = &procedure;
          return true;
        }
        void leave(LinkPressProcedure<Payload> &procedure)
        {
          assert(this->active_ == &procedure);
          (void)procedure;
          this->active_ = 0;
        }
        PressCandidate<Payload> candidate_;
        LinkPressProcedure<Payload> *active_;
        LinkPressSession(const LinkPressSession &);
        LinkPressSession &operator=(const LinkPressSession &);
      };

      /** Stack registration. Refusal is inert. An admitted procedure starts unmarked
          and can be marked only by invalidation before close. Close before publishing. */
      template <class Payload> class LinkPressProcedure
      {
      public:
        explicit LinkPressProcedure(LinkPressSession<Payload> &session)
            : session_(session.enter(*this) ? &session : 0),
              invalidated_(false)
        {
        }
        ~LinkPressProcedure()
        {
          this->close();
        }
        void close()
        {
          if (this->session_)
          {
            this->session_->leave(*this);
            this->session_ = 0;
          }
        }
        bool admitted() const
        {
          return this->session_ != 0;
        }
        bool invalidated() const
        {
          return this->invalidated_;
        }
        bool takeCandidate(Payload &out)
        {
          assert(this->session_);
          return this->session_->candidate_.take(out);
        }
        void placeCandidate(const Payload &payload)
        {
          assert(this->session_);
          this->session_->candidate_.place(payload);
        }
        bool candidateHolds(const Payload &payload) const
        {
          assert(this->session_);
          return this->session_->candidate_.holds(payload);
        }

      private:
        friend class LinkPressSession<Payload>;
        LinkPressSession<Payload> *session_;
        bool invalidated_;
        LinkPressProcedure(const LinkPressProcedure &);
        LinkPressProcedure &operator=(const LinkPressProcedure &);
      };
    } // namespace internal
  } // namespace app
} // namespace loka
#endif
