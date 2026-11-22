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

      class LinkPressRegistration;

      /** Admits at most one registration at a time. Only markActive() marks that registration. */
      class LinkPressOccupancy
      {
      public:
        LinkPressOccupancy();
        void markActive();

      private:
        friend class LinkPressRegistration;
        friend class loka::testing::LinkPressSessionAccess;
        bool enter(LinkPressRegistration &registration);
        void leave(LinkPressRegistration &registration);
        LinkPressRegistration *active_;
        LinkPressOccupancy(const LinkPressOccupancy &);
        LinkPressOccupancy &operator=(const LinkPressOccupancy &);
      };

      /** Stack registration marked only while active. Close is idempotent and destruction closes it. */
      class LinkPressRegistration
      {
      public:
        explicit LinkPressRegistration(LinkPressOccupancy &occupancy);
        ~LinkPressRegistration();
        void close();
        bool admitted() const
        {
          return this->occupancy_ != 0;
        }
        bool invalidated() const
        {
          return this->invalidated_;
        }

      private:
        friend class LinkPressOccupancy;
        LinkPressOccupancy *occupancy_;
        bool invalidated_;
        LinkPressRegistration(const LinkPressRegistration &);
        LinkPressRegistration &operator=(const LinkPressRegistration &);
      };

      template <class Payload> class LinkPressProcedure;

      /** Owns one pending candidate and admits at most one active procedure.
          Only invalidate() marks the active procedure. */
      template <class Payload> class LinkPressSession
      {
      public:
        LinkPressSession() {}
        void invalidate()
        {
          this->candidate_.drop();
          this->occupancy_.markActive();
        }
        void lose()
        {
          this->candidate_.drop();
        }

      private:
        friend class LinkPressProcedure<Payload>;
        friend class loka::testing::LinkPressSessionAccess;
        PressCandidate<Payload> candidate_;
        LinkPressOccupancy occupancy_;
        LinkPressSession(const LinkPressSession &);
        LinkPressSession &operator=(const LinkPressSession &);
      };

      /** Stack registration. Refusal is inert. An admitted procedure starts unmarked
          and can be marked only by invalidation before close. Close before publishing. */
      template <class Payload> class LinkPressProcedure
      {
      public:
        explicit LinkPressProcedure(LinkPressSession<Payload> &session)
            : registration_(session.occupancy_),
              session_(session)
        {
        }
        void close()
        {
          this->registration_.close();
        }
        bool admitted() const
        {
          return this->registration_.admitted();
        }
        bool invalidated() const
        {
          return this->registration_.invalidated();
        }
        bool takeCandidate(Payload &out)
        {
          assert(this->admitted());
          return this->session_.candidate_.take(out);
        }
        void placeCandidate(const Payload &payload)
        {
          assert(this->admitted());
          this->session_.candidate_.place(payload);
        }
        bool candidateHolds(const Payload &payload) const
        {
          assert(this->admitted());
          return this->session_.candidate_.holds(payload);
        }

      private:
        LinkPressRegistration registration_;
        LinkPressSession<Payload> &session_;
        LinkPressProcedure(const LinkPressProcedure &);
        LinkPressProcedure &operator=(const LinkPressProcedure &);
      };
    } // namespace internal
  } // namespace app
} // namespace loka
#endif
