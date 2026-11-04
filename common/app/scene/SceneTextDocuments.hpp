#ifndef LOKA_APP_SCENE_TEXT_DOCUMENTS_HPP
#define LOKA_APP_SCENE_TEXT_DOCUMENTS_HPP
namespace loka
{
  namespace app
  {
    namespace detail
    {
      class TextDocumentPublisher;
    }
    namespace scene
    {
      class TextDocumentRegistration;
      class IPlatformController;
      /** Membership twin of SceneFocus; documents publish independently, not through one focus edge. */
      class SceneTextDocuments
      {
      public:
        SceneTextDocuments()
            : head_(0),
              phase_(IDLE)
        {
        }
        ~SceneTextDocuments();
        void disconnectAll();
        void createStores(IPlatformController &controller);
        bool isPublishing() const
        {
          return this->phase_ == PUBLICATION;
        }

      private:
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
        TextDocumentRegistration *head_;
        Phase phase_;
        friend class TextDocumentRegistration;
        friend class ::loka::app::detail::TextDocumentPublisher;
      };
    } // namespace scene
  } // namespace app
} // namespace loka
#endif
