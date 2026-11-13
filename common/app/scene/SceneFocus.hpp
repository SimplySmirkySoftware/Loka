#ifndef LOKA_APP_SCENE_SCENE_FOCUS_HPP
#define LOKA_APP_SCENE_SCENE_FOCUS_HPP

#include <cassert>

namespace loka
{
  namespace app
  {
    namespace detail
    {
      class FocusPublisher;
    }
    namespace scene
    {
      class FocusRow;
      class SceneFocus;
      struct SceneFocusTestAccess;

      /** Twin of TextStoreLink in app/text/TextStore.hpp. A callback-free, non-owning pair. Either endpoint may die
       * first. */
      class FocusLink
      {
      public:
        explicit FocusLink(FocusRow *row = 0)
            : peer_(0),
              row_(row)
        {
        }
        ~FocusLink()
        {
          this->cut();
        }
        void connect(FocusLink &other)
        {
          this->cut();
          other.cut();
          if (&other == this)
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
        /** The row at the opposite endpoint, or none after either end cuts. */
        FocusRow *peerRow() const
        {
          return this->peer_ ? this->peer_->row_ : 0;
        }

      private:
        FocusLink(const FocusLink &);
        FocusLink &operator=(const FocusLink &);
        FocusLink *peer_;
        FocusRow *const row_;
        friend struct SceneFocusTestAccess;
      };

      template <typename RowT> struct FocusRowTypeTokenStorage
      {
        static const char value_;
      };
      template <typename RowT> const char FocusRowTypeTokenStorage<RowT>::value_ = 0;
      template <typename RowT> const void *FocusRowTypeToken()
      {
        return &FocusRowTypeTokenStorage<RowT>::value_;
      }

      /** Kernel membership and two independent edges; contains no app state. */
      class FocusRow
      {
      public:
        FocusRow()
            : publication_(this),
              source_(this),
              owner_(0),
              previous_(0),
              next_(0)
        {
        }
        virtual ~FocusRow();
        /** Membership identity, independent of source and publication edges. */
        bool belongsTo(const SceneFocus &scene) const { return this->owner_ == &scene; }
        /** Optional extension identity; bare kernel rows have no app type. */
        virtual const void *focusRowTypeKey() const { return 0; }

      protected:
        /** Living detach extension. Reclamation never calls this hook. */
        virtual void leaveAttached()
        {
          this->publication_.cut();
        }
        FocusLink publication_;
        FocusLink source_;

      private:
        FocusRow(const FocusRow &);
        FocusRow &operator=(const FocusRow &);
        void unlink();
        SceneFocus *owner_;
        FocusRow *previous_;
        FocusRow *next_;
        friend class SceneFocus;
        friend class Node;
        friend class ::loka::app::detail::FocusPublisher;
        friend struct SceneFocusTestAccess;
      };

      // Membership twin: SceneTextDocuments; focus additionally owns a publication edge.
      /** Scene-owned focus membership. Retirement doors are teardown-only and read only kernel hooks and edges. */
      class SceneFocus
      {
      public:
        SceneFocus()
            : head_(0),
              published_(),
              phase_(IDLE)
#ifndef NDEBUG
              ,
              registryPrevious_(0),
              registryNext_(registryHead())
#endif
        {
#ifndef NDEBUG
          if (this->registryNext_)
            this->registryNext_->registryPrevious_ = this;
          registryHead() = this;
#endif
        }
        ~SceneFocus()
        {
#ifndef NDEBUG
          const bool publishedSurvived = this->published_.peerRow() != 0;
#endif
          this->disconnectAll();
#ifndef NDEBUG
          if (this->registryPrevious_)
            this->registryPrevious_->registryNext_ = this->registryNext_;
          else
            registryHead() = this->registryNext_;
          if (this->registryNext_)
            this->registryNext_->registryPrevious_ = this->registryPrevious_;
          assert(!publishedSurvived && "Scene teardown must disconnect published focus");
#endif
        }

      private:
        void disconnectAll()
        {
          while (this->head_)
          {
            FocusRow *row = this->head_;
            row->unlink();
            row->publication_.cut();
            row->source_.cut();
          }
          this->published_.cut();
        }

      public:
        /** Covers take, native write, readback and publication as one completion. */
        bool isPublishing() const
        {
          return this->phase_ == PUBLICATION;
        }

      private:
        SceneFocus(const SceneFocus &);
        SceneFocus &operator=(const SceneFocus &);
        enum Phase
        {
          IDLE,
          PUBLICATION
        };
        /** Nested publication restores the enclosing phase on every exit. */
        class Publication
        {
        public:
          explicit Publication(SceneFocus &focus)
              : focus_(focus),
                previous_(focus.phase_)
          {
            this->focus_.phase_ = PUBLICATION;
          }
          ~Publication()
          {
            this->focus_.phase_ = this->previous_;
          }

        private:
          Publication(const Publication &);
          Publication &operator=(const Publication &);
          SceneFocus &focus_;
          const Phase previous_;
        };
        void join(FocusRow &row)
        {
          if (row.owner_ == this)
            return;
          if (row.owner_)
          {
            assert(false && "focus row already belongs to another Scene");
            return;
          }
          row.owner_ = this;
          row.next_ = this->head_;
          if (this->head_)
            this->head_->previous_ = &row;
          this->head_ = &row;
        }
        FocusRow *head_;
        FocusLink published_;
        Phase phase_;
#ifndef NDEBUG
        static SceneFocus *&registryHead()
        {
          static SceneFocus *head = 0;
          return head;
        }
        SceneFocus *registryPrevious_;
        SceneFocus *registryNext_;
#endif
        friend class Scene;
        friend class FocusRow;
        friend class BoundaryNode;
        friend class ::loka::app::detail::FocusPublisher;
        friend struct SceneFocusTestAccess;
      };

      inline void FocusRow::unlink()
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
        this->previous_ = 0;
        this->next_ = 0;
      }

      inline FocusRow::~FocusRow()
      {
        this->unlink();
        // Endpoint members cut their peers during destruction.
      }

    } // namespace scene
  } // namespace app
} // namespace loka
#endif
