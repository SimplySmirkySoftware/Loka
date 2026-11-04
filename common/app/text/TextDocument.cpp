#include "app/text/TextDocumentRegistration.hpp"
#include "app/scene/Scene.hpp"
#include "app/text/TextDocumentPublisher.hpp"
#include "app/text/TextChangeNote.hpp"
namespace loka
{
  namespace app
  {
    bool TextDocument::canQuery() const
    {
      if (!this->fact() || this->fact()->get().state() != TEXT_DOCUMENT_READY || !this->registration_)
        return false;
      scene::TextStoreRow *row = this->registration_->store();
      return row && !row->controller().peekTextStore(*row);
    }
    bool TextDocument::queryLength(unsigned long &out) const
    {
      if (!this->canQuery())
        return false;
      scene::TextStoreRow *row = this->registration_->store();
      return row->controller().queryTextLength(*row, out);
    }
    bool TextDocument::queryOrder(const TextPosition &a, const TextPosition &b, int &out) const
    {
      if (!this->canQuery())
        return false;
      unsigned long left, right;
      const unsigned long revision = this->fact()->get().textRevision();
      if (!offset(a, revision, left) || !offset(b, revision, right))
        return false;
      out = left < right ? -1 : left > right ? 1 : 0;
      return true;
    }
    namespace scene
    {
      void ComposableNode::document(TextDocument &out)
      {
        if (out.registration_)
        {
          assert(false && "document declared twice");
          return;
        }
        this->state(out.fact_, TextDocumentFact());
        this->state(out.request_, TextPosition::None());
        TextDocumentRegistration *row = new TextDocumentRegistration(out);
        out.registration_ = row;
        this->enrollParticipant(*row);
      }
      void TextDocumentRegistration::attach(Node &node, Scene &scene)
      {
        if (&node != scene.rootNode_)
        {
#ifdef LOKA_LIFECYCLE_AUDIT
          assert(false && "document must be declared by the Scene root");
#endif
          this->phase_.refuse();
        }
        scene.textDocuments().join(*this);
      }
      void TextDocumentRegistration::unlink()
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
      void TextDocumentRegistration::withdraw()
      {
        TextStoreRow *row = this->store();
        if (row)
          row->retire();
        this->store_.cut();
        this->unlink();
      }
      SceneTextDocuments::~SceneTextDocuments()
      {
        this->disconnectAll();
      }
      void SceneTextDocuments::disconnectAll()
      {
        while (this->head_)
          this->head_->withdraw();
      }
      void SceneTextDocuments::join(TextDocumentRegistration &row)
      {
        if (row.owner_ == this)
          return;
        if (row.owner_)
        {
          assert(false && "document already belongs to a Scene");
          return;
        }
        row.owner_ = this;
        row.next_ = this->head_;
        if (this->head_)
          this->head_->previous_ = &row;
        this->head_ = &row;
      }
      void SceneTextDocuments::visit(Visitor &visitor)
      {
        for (TextDocumentRegistration *row = this->head_; row;)
        {
          TextDocumentRegistration *next = row->next_;
          visitor.visit(*row);
          row = next;
        }
      }
      void SceneTextDocuments::createStores(IPlatformController &controller)
      {
        for (TextDocumentRegistration *row = this->head_; row; row = row->next_)
        {
          if (row->phase_.value() != TextStorePhase::STORE_AWAITED)
            continue;
          if (controller.createTextStore(*row) && row->store())
            row->phase_.link();
          else
          {
            if (row->store())
              row->store()->retire();
            row->phase_.refuse();
          }
        }
      }
    } // namespace scene
    namespace detail
    {
      class TextDocumentPublisher::Visitor : public scene::SceneTextDocuments::Visitor
      {
      public:
        Visitor(scene::IPlatformController &controller, Requests requests)
            : controller_(controller),
              requests_(requests)
        {
        }
        virtual void visit(scene::TextDocumentRegistration &row)
        {
          visitRow(row, this->controller_, this->requests_);
        }

      private:
        scene::IPlatformController &controller_;
        const Requests requests_;
      };
      void TextDocumentPublisher::visitRow(scene::TextDocumentRegistration &row,
                                           scene::IPlatformController &controller,
                                           Requests requests)
      {
        if (requests == KEEP_REQUESTS)
        {
          scene::TextStoreRow *store = row.store();
          if (row.phase_.value() == scene::TextStorePhase::STORE_LINKED && store)
            controller.commitMarkedText(*store);
        }
        publish(row, controller, requests);
      }
      void TextDocumentPublisher::complete(scene::SceneTextDocuments &documents, scene::IPlatformController &controller)
      {
        scene::SceneTextDocuments::Publication publication(documents);
        // Fact observers may queue structural work, including removal of a refused
        // non-root declaration. Keep the traversal and its borrowed states alive
        // until all readings have been published; scope exit never pumps.
        scene::BorrowScope borrow(controller);
        Visitor visitor(controller, TAKE_REQUESTS);
        documents.visit(visitor);
      }
      void TextDocumentPublisher::refresh(scene::SceneTextDocuments &documents, scene::IPlatformController &controller)
      {
        scene::SceneTextDocuments::Publication publication(documents);
        scene::BorrowScope borrow(controller);
        Visitor visitor(controller, KEEP_REQUESTS);
        documents.visit(visitor);
      }
      void TextDocumentPublisher::publish(scene::TextDocumentRegistration &row,
                                          scene::IPlatformController &controller,
                                          Requests requests)
      {
        TextDocument &document = row.document_;
        if (!document.fact_.isValid() || !document.request_.isValid())
          return;
        const TextDocumentFact previous = document.fact_.get();
        const scene::WriteSeat<TextDocumentFact> fact = document.fact_.writeSeat();
        scene::TextStoreReading first, second;
        scene::TextStoreRow *store = row.store();
        const bool read = store && controller.readTextStore(*store, first);
        const unsigned long revision = previous.textRevision() + (read && first.changed() ? 1 : 0);
        // No gate or early return after consuming the first reading.
        if (requests == TAKE_REQUESTS && !(read && first.marked())
            && row.phase_.value() != scene::TextStorePhase::STORE_AWAITED)
        {
          const TextPosition request = document.request_.get();
          if (!request.isNone())
          {
            document.request_.set(TextPosition::None());
            unsigned long target;
            store = row.store();
            if (row.phase_.value() == scene::TextStorePhase::STORE_LINKED && store
                && TextDocument::offset(request, revision, target))
              controller.applyTextRequest(*store, target);
          }
        }
        store = row.store();
        const bool reread = store && controller.readTextStore(*store, second);
        scene::TextChangeNote changes;
        if (read)
          changes.note(first);
        if (reread)
          changes.note(second);
        const scene::TextStoreReading combined = changes.take();
        const unsigned long finalRevision = previous.textRevision() + (combined.changed() ? 1 : 0);
        TextDocumentState state = TEXT_DOCUMENT_AWAITING_STORE;
        switch (row.phase_.value())
        {
        case scene::TextStorePhase::STORE_AWAITED:
          break;
        case scene::TextStorePhase::STORE_REFUSED:
          state = TEXT_DOCUMENT_UNAVAILABLE;
          break;
        case scene::TextStorePhase::STORE_LINKED:
          state = store ? (combined.marked() ? TEXT_DOCUMENT_MARKED : TEXT_DOCUMENT_READY) : TEXT_DOCUMENT_UNAVAILABLE;
          break;
        }
        const scene::TextStoreReading &last = reread ? second : first;
        const TextPosition caret = (state == TEXT_DOCUMENT_READY || state == TEXT_DOCUMENT_MARKED) && (read || reread)
                                       ? TextDocument::position(finalRevision, last.caret())
                                       : TextPosition::None();
        const TextPosition anchor = (state == TEXT_DOCUMENT_READY || state == TEXT_DOCUMENT_MARKED) && (read || reread)
                                        ? TextDocument::position(finalRevision, last.anchor())
                                        : TextPosition::None();
        TextChangedRange range;
        const TextDocumentFact next(state, finalRevision, caret, anchor, combined.queryRange(range) ? &range : 0);
        if (next != previous)
          fact.set(next);
      }
    } // namespace detail
  } // namespace app
} // namespace loka
