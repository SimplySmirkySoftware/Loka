#ifndef LOKA_APP_SCENE_TEXT_DOCUMENTS_HPP
#define LOKA_APP_SCENE_TEXT_DOCUMENTS_HPP
#include "app/scene/MemberList.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      struct TextDocumentAccess;
    }
    namespace detail
    {
      class TextDocumentPublisher;
    }
    namespace scene
    {
      class TextDocumentRegistration;
      class IPlatformController;
      /** Membership twin of SceneFocus; documents publish independently, not through one focus edge.
       * Retirement doors are teardown-only; traversal stays with trusted framework callers. */
      class SceneTextDocuments : private MemberList<TextDocumentRegistration>
      {
      public:
        /** Receives rows while membership and traversal remain with the Scene. */
        class Visitor
        {
        public:
          virtual ~Visitor() {}
          virtual void visit(TextDocumentRegistration &row) = 0;
        };
        SceneTextDocuments()
            : phase_(IDLE)
        {
        }
        ~SceneTextDocuments();
        void createStores(IPlatformController &controller);
        bool isPublishing() const
        {
          return this->phase_ == PUBLICATION;
        }

      private:
        /** Visits in membership order; a visit may withdraw its current row. */
        void visit(Visitor &visitor);
        void disconnectAll();
        SceneTextDocuments(const SceneTextDocuments &);
        SceneTextDocuments &operator=(const SceneTextDocuments &);
        void join(TextDocumentRegistration &row);
        enum Phase
        {
          IDLE,
          PUBLICATION
        };
        class Publication
        {
        public:
          explicit Publication(SceneTextDocuments &owner)
              : owner_(owner),
                previous_(owner.phase_)
          {
            owner.phase_ = PUBLICATION;
          }
          ~Publication()
          {
            this->owner_.phase_ = this->previous_;
          }

        private:
          Publication(const Publication &);
          Publication &operator=(const Publication &);
          SceneTextDocuments &owner_;
          const Phase previous_;
        };
        class StoreCreator;
        Phase phase_;
        friend struct ::loka::app::testing::TextDocumentAccess;
        friend class Scene;
        friend class TextDocumentRegistration;
        friend class ::loka::app::detail::TextDocumentPublisher;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
