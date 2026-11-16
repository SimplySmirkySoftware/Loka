#include "app/text/TextDocumentViewContext.hpp"
#include "app/text/TextDocumentViewAccess.hpp"
#include "app/nodes/controls/TextDocumentEditor.hpp"
#include "app/text/TextDocumentRegistration.hpp"
#include "app/scene/Scene.hpp"
#include "app/text/TextDocumentPublisher.hpp"
#include "app/text/TextChangeNote.hpp"
#include "app/text/TextReader.hpp"
#include <climits>
#include "app/scene/state/RequestSettlement.hpp"
namespace loka
{
  namespace app
  {
    scene::PostResult TextDocument::replace(const TextReplacement &replacement)
    {
      scene::TextDocumentRegistration *row = this->registration_.remote();
      if (!row || !this->replacement_.isValid() || replacement.stage() == TextReplacement::NONE)
        return scene::POST_INVALID;
      if (row->isPublishing() || this->replacement_.get().stage() != TextReplacement::NONE)
        return scene::POST_QUEUE_FULL;
      this->replacement_.set(replacement);
      return scene::POST_ACCEPTED;
    }
    scene::PostResult TextDocument::splice(const TextSplice &value)
    {
      scene::TextDocumentRegistration *row = this->registration_.remote();
      if (!row || !this->splice_.isValid() || value.isNone())
        return scene::POST_INVALID;
      if (row->isPublishing() || !this->splice_.state()->get().isNone() || this->splice_.pending() != 0)
        return scene::POST_QUEUE_FULL;
      return this->splice_.post(value);
    }
    bool TextDocument::canQuery() const
    {
      if (!this->registration_.remote() || !this->fact() || this->fact()->get().state() != TEXT_DOCUMENT_READY)
        return false;
      scene::TextStoreRow *row = this->registration_.remote()->store();
      return row && !row->controller().peekTextStore(*row);
    }
    namespace
    {
      const unsigned long kQueryScratchBytes = 256;
      TextReadRefusal readRefusal(scene::TextChunkResult result)
      {
        switch (result)
        {
        case scene::TEXT_CHUNK_REFUSED_MALFORMED:
          return TEXT_READ_MALFORMED;
        case scene::TEXT_CHUNK_REFUSED_RESOURCE:
          return TEXT_READ_RESOURCE;
        case scene::TEXT_CHUNK_NO_ROOM:
        case scene::TEXT_CHUNK_REFUSED_RANGE:
          return TEXT_READ_RANGE;
        case scene::TEXT_CHUNK_REFUSED_UNSUPPORTED:
        case scene::TEXT_CHUNK_PROGRESS:
        case scene::TEXT_CHUNK_END:
          return TEXT_READ_NOT_READY;
        }
        return TEXT_READ_NOT_READY;
      }
      bool countText(
          scene::TextStoreRow &row, unsigned long end, unsigned long &bytes, unsigned long &units, unsigned long &lines)
      {
        char scratch[kQueryScratchBytes];
        unsigned long cursor = 0, n = 0;
        bytes = units = 0;
        lines = 1;
        for (;;)
        {
          const scene::TextChunkResult result =
              row.controller().readTextChunk(row, cursor, end, scratch, sizeof scratch, n);
          if (result == scene::TEXT_CHUNK_END)
            return true;
          if (result != scene::TEXT_CHUNK_PROGRESS)
            return false;
          bytes += n;
          for (unsigned long i = 0; i < n; ++i)
          {
            const unsigned char byte = static_cast<unsigned char>(scratch[i]);
            if ((byte & 0xc0) != 0x80)
              units += byte >= 0xf0 ? 2 : 1;
            if (byte == '\n')
              ++lines;
          }
        }
      }
      bool recoverNative(scene::TextStoreRow &row,
                         unsigned long &cursor,
                         unsigned long bytes,
                         char (&scratch)[kQueryScratchBytes])
      {
        while (bytes)
        {
          const unsigned long capacity = bytes < sizeof scratch ? bytes : sizeof scratch;
          unsigned long n = 0;
          if (row.controller().readTextChunk(row, cursor, scene::kTextNativeEnd, scratch, capacity, n)
              != scene::TEXT_CHUNK_PROGRESS)
            return false;
          bytes -= n;
        }
        return true;
      }
    } // namespace
    bool TextDocument::queryLength(unsigned long &out) const
    {
      if (!this->canQuery())
        return false;
      unsigned long bytes, units, lines;
      if (!countText(*this->registration_.remote()->store(), scene::kTextNativeEnd, bytes, units, lines))
        return false;
      out = bytes;
      return true;
    }
    bool TextDocument::queryLineCount(unsigned long &out) const
    {
      if (!this->canQuery())
        return false;
      unsigned long bytes, units, lines;
      if (!countText(*this->registration_.remote()->store(), scene::kTextNativeEnd, bytes, units, lines))
        return false;
      out = lines;
      return true;
    }
    bool TextDocument::queryNativeOffset(const TextPosition &position, unsigned long &out) const
    {
      return this->canQuery() && offset(position, this->fact()->get().textRevision(), out)
             && out != scene::kTextNativeEnd;
    }
    bool TextDocument::queryUtf8Offset(const TextPosition &position, unsigned long &out) const
    {
      unsigned long end, bytes, units, lines;
      if (!this->queryNativeOffset(position, end)
          || !countText(*this->registration_.remote()->store(), end, bytes, units, lines))
        return false;
      out = bytes;
      return true;
    }
    bool TextDocument::queryUtf16Offset(const TextPosition &position, unsigned long &out) const
    {
      unsigned long end, bytes, units, lines;
      if (!this->queryNativeOffset(position, end)
          || !countText(*this->registration_.remote()->store(), end, bytes, units, lines))
        return false;
      out = units;
      return true;
    }
    bool TextDocument::queryPositionAtUtf8(unsigned long bytes, TextPosition &out) const
    {
      if (!this->canQuery())
        return false;
      unsigned long cursor = 0;
      char scratch[kQueryScratchBytes];
      if (!recoverNative(*this->registration_.remote()->store(), cursor, bytes, scratch))
        return false;
      out = position(this->fact()->get().textRevision(), cursor);
      return true;
    }
    bool TextDocument::queryLineRange(unsigned long line, TextPosition &start, TextPosition &end) const
    {
      if (!this->canQuery())
        return false;
      scene::TextStoreRow &row = *this->registration_.remote()->store();
      char scratch[kQueryScratchBytes];
      unsigned long cursor = 0, current = 0, beginning = 0, n = 0;
      for (;;)
      {
        const unsigned long before = cursor;
        const scene::TextChunkResult result =
            row.controller().readTextChunk(row, cursor, scene::kTextNativeEnd, scratch, sizeof scratch, n);
        if (result == scene::TEXT_CHUNK_END)
        {
          if (current != line)
            return false;
          start = position(this->fact()->get().textRevision(), beginning);
          end = position(this->fact()->get().textRevision(), cursor);
          return true;
        }
        if (result != scene::TEXT_CHUNK_PROGRESS)
          return false;
        unsigned long beginningPrefix = n, separatorPrefix = n;
        for (unsigned long i = 0; i < n; ++i)
        {
          if (scratch[i] != '\n')
            continue;
          if (current == line)
          {
            separatorPrefix = i;
            break;
          }
          ++current;
          if (current == line)
            beginningPrefix = i;
        }
        // Recovery overwrites scratch only after the delimiter scan is complete.
        if (beginningPrefix < n)
        {
          beginning = before;
          if (!recoverNative(row, beginning, beginningPrefix, scratch) || !recoverNative(row, beginning, 1, scratch))
            return false;
        }
        if (separatorPrefix < n)
        {
          unsigned long separator = before;
          if (!recoverNative(row, separator, separatorPrefix, scratch))
            return false;
          start = position(this->fact()->get().textRevision(), beginning);
          end = position(this->fact()->get().textRevision(), separator);
          return true;
        }
      }
    }
    void TextReader::attach(const TextDocument &document)
    {
      if (!document.canQuery())
      {
        this->phase_.refuse(TEXT_READ_NOT_READY);
        return;
      }
      this->revision_ = document.fact()->get().textRevision();
      document.registration_.remote()->join(this->membership_);
    }
    TextReader::TextReader(const TextDocument &document)
        : membership_(*this),
          nativeOffset_(0),
          nativeEnd_(scene::kTextNativeEnd),
          revision_(0)
    {
      this->attach(document);
    }
    TextReader::TextReader(const TextDocument &document, const TextPosition &from, const TextPosition &to)
        : membership_(*this),
          nativeOffset_(0),
          nativeEnd_(0),
          revision_(0)
    {
      this->attach(document);
      if (this->phase_.stage() == TextReadPhase::REFUSED)
        return;
      if (!TextDocument::offset(from, this->revision_, this->nativeOffset_)
          || !TextDocument::offset(to, this->revision_, this->nativeEnd_))
      {
        this->phase_.refuse(TEXT_READ_NOT_READY);
        return;
      }
      if (this->nativeOffset_ > this->nativeEnd_)
      {
        this->phase_.refuse(TEXT_READ_RANGE);
        return;
      }
      scene::TextStoreRow &row = *document.registration_.remote()->store();
      char scratch[4];
      unsigned long n = 0, cursor = this->nativeOffset_;
      scene::TextChunkResult result = row.controller().readTextChunk(row, cursor, cursor, scratch, sizeof scratch, n);
      if (result != scene::TEXT_CHUNK_END)
      {
        this->phase_.refuse(readRefusal(result));
        return;
      }
      cursor = this->nativeEnd_;
      result = row.controller().readTextChunk(row, cursor, cursor, scratch, sizeof scratch, n);
      if (result != scene::TEXT_CHUNK_END)
        this->phase_.refuse(readRefusal(result));
    }
    TextReadResult TextReader::read(char *out, unsigned long capacity, unsigned long &n)
    {
      n = 0;
      if (this->phase_.stage() == TextReadPhase::REFUSED)
        return TEXT_READ_REFUSED;
      scene::TextDocumentRegistration *registration =
          static_cast<scene::TextDocumentRegistration *>(this->membership_.owner());
      TextDocument *document = registration ? registration->document_.remote() : 0;
      if (!document || !document->canQuery() || document->fact()->get().textRevision() != this->revision_)
        this->phase_.refuse(TEXT_READ_NOT_READY);
      else if (capacity < 4 || !out)
        this->phase_.refuse(TEXT_READ_RANGE);
      if (this->phase_.stage() == TextReadPhase::REFUSED)
        return TEXT_READ_REFUSED;
      if (this->phase_.stage() == TextReadPhase::ENDED)
        return TEXT_READ_END;
      scene::TextStoreRow &row = *registration->store();
      const scene::TextChunkResult result =
          row.controller().readTextChunk(row, this->nativeOffset_, this->nativeEnd_, out, capacity, n);
      if (result == scene::TEXT_CHUNK_PROGRESS)
        return TEXT_READ_MORE;
      if (result == scene::TEXT_CHUNK_END)
      {
        this->phase_.end();
        return TEXT_READ_END;
      }
      this->phase_.refuse(readRefusal(result));
      return TEXT_READ_REFUSED;
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
        if (out.registration_.remote())
        {
          assert(false && "document declared twice");
          return;
        }
        this->state(out.fact_, TextDocumentFact());
        this->state(out.request_, TextPosition::None());
        this->state(out.replacement_, TextReplacement());
        this->state(out.splice_, TextSplice());
        TextDocumentRegistration *row = new TextDocumentRegistration(out);
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
      void TextDocumentRegistration::withdraw()
      {
        TextStoreRow *row = this->store();
        if (row)
          row->retire();
        this->store_.cut();
        this->membership_.leave();
        this->cutAll();
        this->document_.cut();
      }
      SceneTextDocuments::~SceneTextDocuments()
      {
        this->disconnectAll();
      }
      /** Withdraw each declaration while the membership traversal keeps its next hook. */
      class Withdrawer
      {
      public:
        void visit(TextDocumentRegistration &row)
        {
          row.withdraw();
        }
      };
      void SceneTextDocuments::disconnectAll()
      {
        Withdrawer visitor;
        MemberList<TextDocumentRegistration>::visit(visitor);
      }
      void SceneTextDocuments::join(TextDocumentRegistration &row)
      {
        MemberList<TextDocumentRegistration>::join(row.membership_);
      }
      void SceneTextDocuments::visit(Visitor &visitor)
      {
        MemberList<TextDocumentRegistration>::visit(visitor);
      }
      /** Initial projection policy applied once to each live declaration. */
      class SceneTextDocuments::StoreCreator
      {
      public:
        explicit StoreCreator(IPlatformController &controller)
            : controller_(controller)
        {
        }
        void visit(TextDocumentRegistration &row)
        {
          if (!row.document_.remote() || row.phase_.value() != TextStorePhase::STORE_AWAITED)
            return;
          TextStoreSource source(TextReplacement::Empty());
          TextStoreRow *fresh = 0;
          TextStoreReading reading;
          BorrowScope borrow(this->controller_);
          if (this->controller_.createTextStore(source, 0, fresh, reading) == TEXT_STORE_BORN)
          {
            assert(fresh);
            row.connectStore(*fresh);
            row.phase_.link();
          }
          else
            row.phase_.refuse();
        }

      private:
        IPlatformController &controller_;
      };
      void SceneTextDocuments::createStores(IPlatformController &controller)
      {
        StoreCreator visitor(controller);
        MemberList<TextDocumentRegistration>::visit(visitor);
      }
    } // namespace scene
    namespace detail
    {
      /** Completed body-write result; only successful rail writes carry a reading. */
      class TextBodyWrite
      {
      public:
        enum Landed
        {
          LANDED_NONE,
          LANDED_REPLACEMENT,
          LANDED_SPLICE,
          LANDED_UNRECONCILED
        };
        static TextBodyWrite NothingTaken()
        {
          return TextBodyWrite(
              LANDED_NONE, TextReplacement(), scene::Reply<TextReplacement>(), scene::TextStoreReading());
        }
        static TextBodyWrite Refused(const TextReplacement &replacement, TextReplaceResult reason)
        {
          return TextBodyWrite(LANDED_NONE,
                               replacement,
                               scene::Reply<TextReplacement>::Refused(replacement, reason),
                               scene::TextStoreReading());
        }
        static TextBodyWrite ReplacementLanded(const TextReplacement &replacement,
                                               const scene::Reply<TextReplacement> &reply,
                                               const scene::TextStoreReading &reading)
        {
          return TextBodyWrite(LANDED_REPLACEMENT, replacement, reply, reading);
        }
        TextBodyWrite withSplice(const TextSplice &splice,
                                 const scene::Reply<TextSplice> &reply,
                                 Landed landed,
                                 const scene::TextStoreReading &reading) const
        {
          return TextBodyWrite(landed, this->replacement_, this->reply_, reading, splice, reply);
        }
        const TextSplice &splice() const
        {
          return this->splice_;
        }
        const scene::Reply<TextSplice> &spliceReply() const
        {
          return this->spliceReply_;
        }
        bool hasReading() const
        {
          return this->landed_ == LANDED_REPLACEMENT || this->landed_ == LANDED_SPLICE;
        }
        Landed landed() const
        {
          return this->landed_;
        }
        const TextReplacement &replacement() const
        {
          return this->replacement_;
        }
        const scene::Reply<TextReplacement> &reply() const
        {
          return this->reply_;
        }
        const scene::TextStoreReading &reading() const
        {
          assert(this->hasReading());
          return this->reading_;
        }

      private:
        TextBodyWrite(Landed landed,
                      const TextReplacement &replacement,
                      const scene::Reply<TextReplacement> &reply,
                      const scene::TextStoreReading &reading,
                      const TextSplice &splice = TextSplice(),
                      const scene::Reply<TextSplice> &spliceReply = scene::Reply<TextSplice>())
            : landed_(landed),
              replacement_(replacement),
              reply_(reply),
              reading_(reading),
              splice_(splice),
              spliceReply_(spliceReply)
        {
        }
        const Landed landed_;
        const TextReplacement replacement_;
        const scene::Reply<TextReplacement> reply_;
        const scene::TextStoreReading reading_;
        const TextSplice splice_;
        const scene::Reply<TextSplice> spliceReply_;
      };
      /** Publish-local reply authority; no seat survives the document link. */
      class TextDocumentReplyAccess
      {
        static scene::WriteSeat<scene::Reply<TextReplacement> > seat(scene::RequestWithReply<TextReplacement> &request)
        {
          return scene::ComposableNode::textDocumentReplySeat(request);
        }
        static scene::WriteSeat<TextSplice> spliceSeat(scene::RequestQueueBase<TextSplice> &request)
        {
          return scene::ComposableNode::textDocumentSpliceSeat(request);
        }
        static scene::WriteSeat<scene::Reply<TextSplice> > spliceReplySeat(scene::RequestQueueBase<TextSplice> &request)
        {
          return scene::ComposableNode::textDocumentSpliceReplySeat(request);
        }
        friend class TextDocumentPublisher;
      };
      namespace
      {
        unsigned long advanceRevision(unsigned long previous, bool changed)
        {
          assert(!changed || previous != ULONG_MAX);
          return previous + (changed ? 1 : 0);
        }
        TextReplaceResult replacementRefusal(scene::TextStoreBirth birth)
        {
          switch (birth)
          {
          case scene::TEXT_STORE_BORN:
            return TEXT_REPLACE_OK;
          case scene::TEXT_STORE_REFUSED_ALLOCATION:
            return TEXT_REPLACE_ALLOCATION;
          case scene::TEXT_STORE_REFUSED_NOT_REPRESENTABLE:
            return TEXT_REPLACE_NOT_REPRESENTABLE;
          case scene::TEXT_STORE_REFUSED_CAPACITY:
            return TEXT_REPLACE_CAPACITY;
          case scene::TEXT_STORE_REFUSED_UNSUPPORTED:
            return TEXT_REPLACE_UNAVAILABLE;
          }
          return TEXT_REPLACE_UNAVAILABLE;
        }
      } // namespace
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
        if (!row.document_.remote())
          return;
        if (requests == KEEP_REQUESTS)
        {
          scene::TextStoreRow *store = row.store();
          if (row.phase_.value() == scene::TextStorePhase::STORE_LINKED && store)
            controller.commitMarkedText(*store);
        }
        publish(row, controller, requests);
      }
      /** Adapter delegates privileged work to a member of the befriended publisher (GCC 4.0.1). */
      class TextDocumentPublisher::ViewVisitor
      {
      public:
        void visit(scene::TextViewClaim &claim)
        {
          TextDocumentPublisher::publishView(claim);
        }
      };
      void TextDocumentPublisher::publishView(scene::TextViewClaim &claim)
      {
        scene::TextDocumentViewContext &context = static_cast<scene::TextDocumentViewContext &>(claim);
        scene::Node *node = context.owner();
        if (node
            && (node->lifecycleFact() == scene::NODE_FACT_ATTACHED
                || node->lifecycleFact() == scene::NODE_FACT_DETACHED_RETAINED))
          static_cast<TextDocumentEditorNode *>(node)->publishAttachment(claim.attachment());
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
        ViewVisitor views;
        documents.scene::MemberList<scene::TextViewClaim>::visit(views);
      }
      void TextDocumentPublisher::refresh(scene::SceneTextDocuments &documents, scene::IPlatformController &controller)
      {
        scene::SceneTextDocuments::Publication publication(documents);
        scene::BorrowScope borrow(controller);
        Visitor visitor(controller, KEEP_REQUESTS);
        documents.visit(visitor);
        ViewVisitor views;
        documents.scene::MemberList<scene::TextViewClaim>::visit(views);
      }
      TextBodyWrite TextDocumentPublisher::writeReplacement(scene::TextDocumentRegistration &row,
                                                            scene::IPlatformController &controller,
                                                            Requests requests,
                                                            scene::TextStoreRow *store,
                                                            bool read,
                                                            const scene::TextStoreReading &first)
      {
        if (requests != TAKE_REQUESTS || row.phase_.value() == scene::TextStorePhase::STORE_AWAITED)
          return TextBodyWrite::NothingTaken();
        TextDocument &document = *row.document_.remote();
        const TextReplacement pending = document.replacement_.get();
        if (pending.stage() == TextReplacement::NONE)
          return TextBodyWrite::NothingTaken();
        document.replacement_.set(TextReplacement());
        TextReplaceResult reason = TEXT_REPLACE_UNAVAILABLE;
        if (row.phase_.value() == scene::TextStorePhase::STORE_LINKED && read)
        {
          if (first.marked())
            reason = TEXT_REPLACE_COMPOSING;
          else
          {
            TextStoreSource source(pending);
            scene::TextStoreRow *fresh = 0;
            scene::TextStoreReading reading;
            const scene::TextStoreBirth birth = controller.createTextStore(source, store, fresh, reading);
            reason = replacementRefusal(birth);
            if (birth == scene::TEXT_STORE_BORN)
            {
              assert(fresh);
              row.connectStore(*fresh);
              store->retire();
              TextReplacement applied(pending);
              applied.placement_ = source.appliedPlacement();
              return TextBodyWrite::ReplacementLanded(pending, scene::formReply(pending, applied, reason), reading);
            }
          }
        }
        return TextBodyWrite::Refused(pending, reason);
      }
      TextBodyWrite TextDocumentPublisher::writeBody(scene::TextDocumentRegistration &row,
                                                     scene::IPlatformController &controller,
                                                     Requests requests,
                                                     scene::TextStoreRow *store,
                                                     bool read,
                                                     const scene::TextStoreReading &first)
      {
        if (requests != TAKE_REQUESTS || row.phase_.value() == scene::TextStorePhase::STORE_AWAITED)
          return TextBodyWrite::NothingTaken();
        const TextBodyWrite replacement = writeReplacement(row, controller, requests, store, read, first);
        return writeSplice(row, controller, read, first, replacement);
      }
      TextBodyWrite TextDocumentPublisher::writeSplice(scene::TextDocumentRegistration &row,
                                                       scene::IPlatformController &controller,
                                                       bool read,
                                                       const scene::TextStoreReading &first,
                                                       const TextBodyWrite &replacement)
      {
        TextDocument &document = *row.document_.remote();
        const scene::WriteSeat<TextSplice> seat = TextDocumentReplyAccess::spliceSeat(document.splice_);
        if (!seat.isValid())
          return replacement;
        const TextSplice pending = seat.state()->get();
        if (pending.isNone())
          return replacement;
        seat.set(TextSplice());
        TextSpliceResult reason = TEXT_SPLICE_UNAVAILABLE;
        TextBodyWrite::Landed landed = replacement.landed();
        if (landed == TextBodyWrite::LANDED_REPLACEMENT)
          reason = TEXT_SPLICE_STALE;
        else if (row.phase_.value() == scene::TextStorePhase::STORE_LINKED && read)
        {
          if (first.marked())
            reason = TEXT_SPLICE_COMPOSING;
          else
          {
            const unsigned long revision = advanceRevision(document.fact_.get().textRevision(), first.changed());
            unsigned long from, to;
            if (!TextDocument::offset(pending.from(), revision, from)
                || !TextDocument::offset(pending.to(), revision, to))
              reason = TEXT_SPLICE_STALE;
            else
            {
              TextStoreSource source(pending.inserted());
              scene::TextStoreReading reading;
              scene::TextStoreRow &store = *row.store();
              reason = controller.applyTextSplice(store, from, to, source, reading);
              if (reason == TEXT_SPLICE_OK)
              {
                TextSplice applied(pending);
                applied.inserted_.placement_ = source.appliedPlacement();
                TextChangedRange range;
                assert(reading.queryRange(range) && range.start() == from);
                return replacement.withSplice(
                    pending, scene::formReply(pending, applied, reason), TextBodyWrite::LANDED_SPLICE, reading);
              }
              if (reason == TEXT_SPLICE_ALLOCATION && controller.peekTextStore(store))
                landed = TextBodyWrite::LANDED_UNRECONCILED;
            }
          }
        }
        return replacement.withSplice(pending,
                                      scene::Reply<TextSplice>::Refused(pending, reason),
                                      landed,
                                      replacement.hasReading() ? replacement.reading() : scene::TextStoreReading());
      }
      void TextDocumentPublisher::replyBodyWrite(scene::TextDocumentRegistration &row, const TextBodyWrite &write)
      {
        if (write.replacement().stage() != TextReplacement::NONE && row.document_.remote())
          TextDocumentReplyAccess::seat(row.document_.remote()->replacement_).set(write.reply(), true);
        if (!write.splice().isNone() && row.document_.remote())
          TextDocumentReplyAccess::spliceReplySeat(row.document_.remote()->splice_).set(write.spliceReply(), true);
      }
      void TextDocumentPublisher::publish(scene::TextDocumentRegistration &row,
                                          scene::IPlatformController &controller,
                                          Requests requests)
      {
        TextDocument *linked = row.document_.remote();
        if (!linked)
          return;
        TextDocument &document = *linked;
        if (!document.fact_.isValid() || !document.request_.isValid() || !document.replacement_.isValid()
            || !document.splice_.isValid())
          return;
        const TextDocumentFact previous = document.fact_.get();
        const scene::WriteSeat<TextDocumentFact> fact = document.fact_.writeSeat();
        scene::TextStoreReading first;
        scene::TextStoreRow *store = row.store();
        const bool read = store && controller.readTextStore(*store, first);
        const TextBodyWrite write = writeBody(row, controller, requests, store, read, first);
        const TextBodyWrite::Landed landed = write.landed();
        const unsigned long revision = advanceRevision(
            previous.textRevision(), (read && first.changed()) || (landed != TextBodyWrite::LANDED_NONE));
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
        scene::TextStoreReading rereading;
        const bool reread = write.hasReading() || (store && controller.readTextStore(*store, rereading));
        const scene::TextStoreReading &second = write.hasReading() ? write.reading() : rereading;
        scene::TextChangeNote changes;
        if (read)
          changes.note(first);
        if (reread)
          changes.note(second);
        const scene::TextStoreReading combined = changes.take();
        const unsigned long finalRevision = advanceRevision(
            previous.textRevision(),
            (read && first.changed()) || (landed != TextBodyWrite::LANDED_NONE) || (reread && second.changed()));
        TextDocumentState state = TEXT_DOCUMENT_AWAITING_STORE;
        switch (row.phase_.value())
        {
        case scene::TextStorePhase::STORE_AWAITED:
          break;
        case scene::TextStorePhase::STORE_REFUSED:
          state = TEXT_DOCUMENT_UNAVAILABLE;
          break;
        case scene::TextStorePhase::STORE_LINKED:
          state = write.hasReading() ? TEXT_DOCUMENT_READY
                  : store            ? (combined.marked() ? TEXT_DOCUMENT_MARKED : TEXT_DOCUMENT_READY)
                                     : TEXT_DOCUMENT_UNAVAILABLE;
          break;
        }
        const scene::TextStoreReading &last = reread ? second : first;
        const bool selectionKnown = (read || reread) && !(landed == TextBodyWrite::LANDED_UNRECONCILED && !reread);
        const TextPosition caret = (state == TEXT_DOCUMENT_READY || state == TEXT_DOCUMENT_MARKED) && selectionKnown
                                       ? TextDocument::position(finalRevision, last.caret())
                                       : TextPosition::None();
        const TextPosition anchor = (state == TEXT_DOCUMENT_READY || state == TEXT_DOCUMENT_MARKED) && selectionKnown
                                        ? TextDocument::position(finalRevision, last.anchor())
                                        : TextPosition::None();
        TextChangedRange range;
        // Successful body writes carry a range even though they report no pending edit.
        const bool reported = !(landed == TextBodyWrite::LANDED_UNRECONCILED && !reread)
                              && (write.hasReading() ? second.queryRange(range) : combined.queryRange(range));
        assert(landed != TextBodyWrite::LANDED_REPLACEMENT || (reported && range.start() == 0));
        const TextPosition changedStart =
            reported ? TextDocument::position(finalRevision, range.start()) : TextPosition::None();
        const TextPosition insertedEnd =
            reported ? TextDocument::position(finalRevision, range.start() + range.insertedLength())
                     : TextPosition::None();
        file::File source;
        if (landed == TextBodyWrite::LANDED_REPLACEMENT)
          write.replacement().querySource(source);
        const TextDocumentOrigin origin =
            landed == TextBodyWrite::LANDED_REPLACEMENT ? TextDocumentOrigin(source, finalRevision) : previous.origin();
        const TextDocumentFact next(state, finalRevision, caret, anchor, changedStart, insertedEnd, origin);
        if (next != previous)
          fact.set(next);
        replyBodyWrite(row, write);
      }
    } // namespace detail
  } // namespace app
} // namespace loka

namespace loka
{
  namespace app
  {
    namespace scene
    {
      void TextStoreRow::retire()
      {
        if (this->phase_ == RETIRED)
          return;
        if (this->view_.remote())
        {
          BorrowScope borrow(this->controller_);
          this->controller_.detachTextView(*this);
        }
        this->view_.cut();
        this->link_.cut();
        this->owner_.unlink(*this);
        this->phase_ = RETIRED;
        this->next_ = this->owner_.retired_;
        if (this->next_)
          this->next_->previous_ = this;
        this->owner_.retired_ = this;
      }

      void TextStoreRow::transferView(TextStoreRow &old, TextStoreRow &fresh)
      {
        assert(&old.controller_ == &fresh.controller_ && old.phase_ == LIVE && fresh.phase_ == LIVE);
        assert(!fresh.view_.remote());
        TextViewClaim *claim = old.view_.remote();
        if (claim)
          claim->linkTo(fresh);
      }
      void TextViewClaim::linkTo(TextStoreRow &row)
      {
        this->reason_ = TextViewAttachment();
        this->link_.connect(row.view_);
      }
      TextDocumentViewContext::TextDocumentViewContext(IPlatformController &controller,
                                                       SceneTextDocuments &documents,
                                                       TextDocumentEditorNode &node)
          : controller_(controller)
      {
        this->setOwner(&node);
        documents.MemberList<TextViewClaim>::join(this->membership_);
      }
      TextDocumentViewContext::~TextDocumentViewContext()
      {
        this->retireView();
      }
      void TextDocumentViewContext::retireView()
      {
        ::loka::app::detail::TextDocumentViewAccess::detach(*this);
        this->membership_.leave();
      }
      void TextDocumentViewContext::attachView()
      {
        TextDocumentEditorNode *node = static_cast<TextDocumentEditorNode *>(this->owner());
        if (node && node->lifecycleFact() == NODE_FACT_ATTACHED)
          ::loka::app::detail::TextDocumentViewAccess::attach(*this, *node->props.document_, *this);
      }
      void TextDocumentViewContext::readLifecycleFactOnAttach()
      {
        this->attachView();
      }
      void TextDocumentViewContext::onFactChanged(NodeLifecycleFact, NodeLifecycleFact next)
      {
        if (next == NODE_FACT_ATTACHED)
          this->attachView();
        else if (next == NODE_FACT_RETIRED)
          this->retireView();
        else
          ::loka::app::detail::TextDocumentViewAccess::detach(*this);
      }
      void TextDocumentViewContext::onPropsApplied()
      {
        TextDocumentEditorNode *node = static_cast<TextDocumentEditorNode *>(this->owner());
        if (node)
          ::loka::app::detail::TextDocumentViewAccess::present(*this, node->props.presentation_);
      }
    } // namespace scene
    namespace detail
    {
      void TextDocumentViewAccess::attach(scene::TextViewClaim &claim,
                                          TextDocument &document,
                                          scene::TextDocumentViewContext &context)
      {
        using namespace scene;
        TextDocumentRegistration *registration = document.registration_.remote();
        TextStoreRow *store = registration ? registration->store() : 0;
        const SceneTextDocuments *documents = static_cast<const SceneTextDocuments *>(claim.membership_.owner());
        const SceneTextDocuments *owner =
            registration ? static_cast<const SceneTextDocuments *>(registration->membership_.owner()) : 0;
        if ((owner && owner != documents) || (store && &store->controller() != &context.controller_))
        {
          claim.refuse(TextViewAttachment::VIEW_REFUSED_FOREIGN);
          return;
        }
        if (!store)
        {
          claim.refuse(TextViewAttachment::VIEW_REFUSED_UNAVAILABLE);
          return;
        }
        TextViewClaim *occupant = store->view_.remote();
        if (occupant == &claim)
          return;
        if (occupant)
        {
          TextDocumentViewContext &old = static_cast<TextDocumentViewContext &>(*occupant);
          if (old.owner() && old.owner()->lifecycleFact() == NODE_FACT_ATTACHED)
          {
            claim.refuse(TextViewAttachment::VIEW_REFUSED_OCCUPIED);
            return;
          }
          detach(*occupant);
        }
        const TextViewPresentation &presentation =
            static_cast<TextDocumentEditorNode *>(context.owner())->props.presentation_;
        bool attached;
        {
          BorrowScope borrow(context.controller_);
          attached = context.controller_.attachTextView(*store, context, presentation);
        }
        if (!attached)
        {
          claim.refuse(TextViewAttachment::VIEW_REFUSED_UNSUPPORTED);
          return;
        }
        claim.linkTo(*store);
        present(claim, presentation);
      }
      void TextDocumentViewAccess::detach(scene::TextViewClaim &claim)
      {
        scene::TextStoreRow *store = claim.link_.remote();
        if (store)
        {
          scene::BorrowScope borrow(store->controller());
          store->controller().detachTextView(*store);
        }
        claim.unlink();
      }
      void TextDocumentViewAccess::present(scene::TextViewClaim &claim, const TextViewPresentation &presentation)
      {
        scene::TextStoreRow *store = claim.link_.remote();
        if (!store)
          return;
        scene::BorrowScope borrow(store->controller());
        store->controller().applyTextViewPresentation(*store, presentation);
      }
    } // namespace detail
  } // namespace app
} // namespace loka
