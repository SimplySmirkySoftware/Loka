#ifndef LOKA_SIMPLE_TEXT_MAIN_NODE_HPP
#define LOKA_SIMPLE_TEXT_MAIN_NODE_HPP

#include "app/scene/BorrowedKeys.hpp"
#include "app/scene/KeySnapshot.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "platform/file/FileHandle.hpp"
#include "app/core/DocumentFacts.hpp"
#include "app/core/DocumentRoster.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/PolicyScope.hpp"
#include "app/nodes/Text.hpp"
#include "app/Menu.hpp"
#include "app/CommandSet.hpp"
#include "app/OpenFileDialog.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/TextDocumentFile.hpp"
#include "app/scene/state/FlowSlot.hpp"

#ifdef TEST_BUILD
class SimpleTextTestAccess;
#endif

namespace simpletext
{
  typedef loka::app::FileChooserResult Choice;

  /** The roster owns the destination and its window's chrome facts. */
  struct Document : loka::app::DocumentFacts
  {
    Choice file;
    Document()
        : loka::app::DocumentFacts(loka::core::String::Literal("Untitled"), false),
          file()
    {
    }
    Document(const Choice &fileValue, const loka::core::String &titleValue, bool editedValue)
        : loka::app::DocumentFacts(titleValue, editedValue),
          file(fileValue)
    {
    }
  };
  enum
  {
    kMaxDocuments = 8
  };

  enum FileCommand
  {
    NEW_DOCUMENT,
    OPEN_DOCUMENT,
    CLOSE_DOCUMENT,
    SAVE_DOCUMENT,
    SAVE_AS_DOCUMENT,
    FILE_COMMAND_COUNT
  };

  enum Operation
  {
    NONE,
    OPEN,
    SAVE,
    CONFIRM_CLOSE,
    SAVE_FOR_CLOSE
  };
  class MainTypeTag
  {
  };
  class MainNode;

  struct MainProps : loka::app::scene::NodePropsBase<MainProps>
  {
    typedef MainTypeTag TypeTag;
    typedef MainNode NodeType;
    MainProps &platformContext(PlatformContext *value)
    {
      this->keys_.set(PLATFORM, value);
      return *this;
    }
    PlatformContext *platformContext() const
    {
      return static_cast<PlatformContext *>(const_cast<void *>(this->keys_.get(PLATFORM)));
    }
    MainProps &documents(loka::app::DocumentRoster<Document, kMaxDocuments> *value)
    {
      this->keys_.set(DOCUMENTS, value);
      return *this;
    }
    loka::app::DocumentRoster<Document, kMaxDocuments> *documents() const
    {
      return static_cast<loka::app::DocumentRoster<Document, kMaxDocuments> *>(
          const_cast<void *>(this->keys_.get(DOCUMENTS)));
    }
    MainProps &document(loka::core::ItemId value)
    {
      this->document_ = value;
      return *this;
    }
    loka::core::ItemId document() const
    {
      return this->document_;
    }
    void assertInitialized() const
    {
      assert(this->keys_.complete());
    }
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return false;
      const MainProps &other = static_cast<const MainProps &>(rhs);
      if (!(this->keys_ == other.keys_))
        return this->keys_ < other.keys_;
      if (this->document_.generation != other.document_.generation)
        return this->document_.generation < other.document_.generation;
      return this->document_.seq < other.document_.seq;
    }

  private:
    enum Key
    {
      PLATFORM,
      DOCUMENTS,
      KEY_COUNT
    };
    loka::app::scene::BorrowedKeys<KEY_COUNT> keys_;
    loka::core::ItemId document_;
  };

  class ConfirmCloseTypeTag
  {
  };
  class ConfirmCloseNode;

  /** Borrows the document identity and forwards the owner's rail result channel. */
  struct ConfirmCloseProps : loka::app::scene::NodePropsBase<ConfirmCloseProps>
  {
    typedef ConfirmCloseTypeTag TypeTag;
    typedef ConfirmCloseNode NodeType;
    MainProps document;
    loka::app::scene::NodeState<loka::app::ConfirmResult> result;
    ConfirmCloseProps(const MainProps &owner, const loka::app::scene::NodeState<loka::app::ConfirmResult> &channel)
        : document(owner),
          result(channel)
    {
    }
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return false;
      const ConfirmCloseProps &other = static_cast<const ConfirmCloseProps &>(rhs);
      if (this->document < other.document)
        return true;
      if (other.document < this->document)
        return false;
      return this->result.state() < other.result.state();
    }
  };

  /** Captures the current title when the confirmation arm mounts, not at app composition. */
  class ConfirmCloseNode : public loka::app::scene::ComponentNodeWithProps<ConfirmCloseProps>
  {
  public:
    explicit ConfirmCloseNode(const ConfirmCloseProps &props)
        : loka::app::scene::ComponentNodeWithProps<ConfirmCloseProps>(props)
    {
    }

  protected:
    virtual void composeChildren(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      using namespace loka::core;
      const Document *entry = this->props.document.documents()->find(this->props.document.document());
      const String title = entry ? entry->title : String::Literal("Untitled");
      c.declare(ConfirmDialog(ConfirmDialogOptions(String::Literal("Save changes to ") + title + "?"))
                    .result(this->props.result)
                    .testId("SimpleText.ConfirmClose"));
    }
  };

  /** One document owner; chooser facts arrive through separate rail-written channels. */
  class MainNode : public loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>
  {
    typedef loka::app::FileChooserResult Choice;
    typedef loka::dsl::FlowChain<Choice, Choice> ChoiceFlow;
    enum DocumentAction
    {
      NEW_DOCUMENT,
      OPEN_DOCUMENT,
      SAVE_DOCUMENT
    };

    /** Replay the owner's old IDs before inserting the sole empty row. */
    class EmptyDocument : public loka::core::ListOpCursor<loka::core::String>
    {
    public:
      explicit EmptyDocument(const loka::app::TextLines &lines)
          : lines_(lines),
            position_(0)
      {
      }
      virtual void rewind()
      {
        this->position_ = 0;
      }
      virtual bool next(loka::core::ListOp<loka::core::String> &out)
      {
        using namespace loka::core;
        if (this->position_ < this->lines_.size())
          out = ListOp<String>(
              REMOVE, this->lines_.at(static_cast<unsigned short>(this->lines_.size() - 1 - this->position_)).id);
        else if (this->position_ == this->lines_.size())
          out = ListOp<String>(INSERT, ItemId::none(), 0, String());
        else
          return false;
        ++this->position_;
        return true;
      }

    private:
      const loka::app::TextLines &lines_;
      std::size_t position_;
    };

    class ErrorVisibleEvalFn : public loka::core::DerivedState<bool>::EvalFn
    {
    public:
      explicit ErrorVisibleEvalFn(const loka::app::scene::NodeState<loka::core::String> &error)
          : error_(error)
      {
      }
      virtual bool operator()()
      {
        return !this->error_.get().empty();
      }

    private:
      const loka::app::scene::NodeState<loka::core::String> &error_;
    };

    struct ChooserCompletion
    {
      typedef Choice In;
      typedef Choice Out;
      loka::dsl::StepRunStatus run(const In &in, Out &out, loka::dsl::FlowError &) const
      {
        out = in;
        return loka::dsl::FLOW_STEP_SUCCEEDED;
      }
    };

  public:
    typedef MainTypeTag TypeTag;
    explicit MainNode(const MainProps &props)
        : loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>(props),
          saved_(&this->buffer_.lines().revision()),
          answered_(&props.documents()->closeAsked(), loka::app::CloseAsk::none()),
          openFlow_(*this),
          saveFlow_(*this)
    {
      this->state(this->operation_, NONE);
      this->state(this->openResult_, Choice());
      this->state(this->saveResult_, Choice());
      this->state(this->confirmResult_, loka::app::ConfirmResult::none());
      this->state(this->error_, loka::core::String());
      this->derived(this->errorVisible_, this->error_, new (std::nothrow) ErrorVisibleEvalFn(this->error_));
      this->state(this->cursor_, loka::app::LineCursor::None());
      this->state(this->caret_, loka::app::LineCursor::None());
    }

    virtual void attachNode(loka::app::scene::NodeComposition &)
    {
      loka::core::StateTracker *owner = 0;
      if (this->buffer_.lines().queryMutationTracker(owner) == loka::core::EDIT_NOT_ATTACHED)
      {
        loka::app::TextCapacity maximum;
        PlatformContext *rail = this->props.platformContext();
        const loka::app::TextBuffer::AttachResult result =
            !rail || !rail->queryTextEditorCapacity(maximum)
                ? loka::app::TextBuffer::ATTACH_RAIL_DECLINED
                : this->buffer_.attach(this->tracker()->asPushTracker(),
                                       *rail,
                                       loka::app::TextCapacity::of(1024, 16384).clampedTo(maximum));
        if (result != loka::app::TextBuffer::ATTACH_OK && result != loka::app::TextBuffer::ATTACH_ALREADY_ATTACHED)
          this->error_.set(loka::core::String::Literal("Cannot allocate document."));
        else if (result == loka::app::TextBuffer::ATTACH_OK)
          this->commitDocument(NEW_DOCUMENT, Choice());
      }
      this->openFlow_
          .set(loka::dsl::Flow() | loka::dsl::Step(1, ChooserCompletion()).onSuccess(&MainNode::opened, this))
          .bindTrigger(this->openResult_.state())
          .withTracker(this->tracker());
      this->saveFlow_.set(loka::dsl::Flow() | loka::dsl::Step(1, ChooserCompletion()).onSuccess(&MainNode::saved, this))
          .bindTrigger(this->saveResult_.state())
          .withTracker(this->tracker());
    }

    virtual void declareBindings(loka::app::scene::BindingToken &t)
    {
      this->props.assertInitialized();
      t.actions(this->commands_, this, &MainNode::onCommand);
      t.watch(*this->confirmResult_.state(), this, &MainNode::completeConfirm);
      // Immediate: a request published before this node mounted (a document
      // opened and asked to close in one turn, a Quit that reached a row whose
      // window was not admitted yet) is answered at attach, not lost.
      t.watch(const_cast<loka::core::State<loka::app::CloseAsk> &>(this->props.documents()->closeAsked()),
              this,
              &MainNode::onCloseAsked,
              true);
      t.watch(const_cast<loka::core::State<loka::core::ListRevision> &>(this->buffer_.lines().revision()),
              this,
              static_cast<void (MainNode::*)()>(&MainNode::publishFacts));
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      using namespace loka::core;
      const State<bool> &errorVisible = *this->errorVisible_.state();
      c.menuBar(MenuBarDefinition()
                << StandardMenuPresets()
                << (Menu("File")
                    << MenuItem("New").onClick(this->commands_.slot<simpletext::NEW_DOCUMENT>())
                    << MenuItem("Open...").shortcut('o').onClick(this->commands_.slot<simpletext::OPEN_DOCUMENT>())
                    << MenuItem("Close").shortcut('w').onClick(this->commands_.slot<simpletext::CLOSE_DOCUMENT>())
                    << MenuItem("Save").shortcut('s').onClick(this->commands_.slot<simpletext::SAVE_DOCUMENT>())
                    << MenuItem("Save As...").onClick(this->commands_.slot<simpletext::SAVE_AS_DOCUMENT>())));
      // One seat on the operation: at most one dialog exists, and none while idle.
      const PolicyScopeDefinition saveDialog = PolicyScopeDefinition().destroyOnDetach()
                                               << SaveFileDialog(String::Literal("untitled.txt"))
                                                      .filterPolicy(FILE_DIALOG_FILTER_ALL_FILES_TEXT)
                                                      .result(this->saveResult_)
                                                      .testId("SimpleText.Save");
      MatchDefinition<Operation> dialog = Match(*this->operation_.state());
      dialog
          .arm(OPEN,
               PolicyScopeDefinition().destroyOnDetach() << OpenFileDialog()
                                                                .filterPolicy(FILE_DIALOG_FILTER_ALL_FILES_TEXT)
                                                                .result(this->openResult_)
                                                                .testId("SimpleText.Open"))
          .arm(SAVE, saveDialog)
          .arm(CONFIRM_CLOSE,
               PolicyScopeDefinition().destroyOnDetach()
                   << scene::Component(ConfirmCloseProps(this->props, this->confirmResult_)))
          .arm(SAVE_FOR_CLOSE, saveDialog)
          .otherwise(Fragment());
      // The band and File menu fire the same emitters. The window has no
      // margin, so the editor reaches the window edges; the band and the
      // error row keep the standard margin inside one padded box.
      c.declare(
          HStack()
          << (VStack().gap(0)
              << (Box().padding(StandardWindowMargin()).gap(StandardWindowMargin())
                  << (RibbonControl().TEST_ID("SimpleText.Ribbon")
                      << (RibbonGroup("File")
                          << RibbonItem("New").onClick(this->commands_.slot<simpletext::NEW_DOCUMENT>())
                          << RibbonItem("Open...").onClick(this->commands_.slot<simpletext::OPEN_DOCUMENT>())
                          << RibbonItem("Save").onClick(this->commands_.slot<simpletext::SAVE_DOCUMENT>())
                          << RibbonItem("Save As...").onClick(this->commands_.slot<simpletext::SAVE_AS_DOCUMENT>())))
                  << (Show(errorVisible) << Text(this->error_.state()).TEST_ID("SimpleText.Error")))
              << TextEditor(this->buffer_, this->cursor_) //
                     .moveCaretTo(this->caret_)
                     .TEST_ID("SimpleText.Editor"))
          << dialog);
    }

  private:
#ifdef TEST_BUILD
    friend class ::SimpleTextTestAccess;
#endif
    void onCommand(FileCommand command)
    {
      switch (command)
      {
      case simpletext::NEW_DOCUMENT:
        this->newDocument();
        break;
      case simpletext::OPEN_DOCUMENT:
        this->openDocument();
        break;
      case simpletext::CLOSE_DOCUMENT:
        this->decideClose(loka::app::CloseAsk::none());
        break;
      case simpletext::SAVE_DOCUMENT:
        this->saveDocument();
        break;
      case simpletext::SAVE_AS_DOCUMENT:
        this->saveAsDocument();
        break;
      case FILE_COMMAND_COUNT:
        break;
      }
    }

    void newDocument()
    {
      if (this->operation_.get() == NONE)
      {
        loka::core::StateTrackerGuard guard(this->tracker());
        if (this->props.documents()->open(Document()).isNone())
          this->error_.set(loka::core::String::Literal("Cannot open another document window."));
      }
    }
    void onCloseAsked()
    {
      const loka::app::CloseAsk ask = this->props.documents()->closeAsked().get();
      if (ask.id != this->props.document() || this->answered_.matches())
        return;
      this->answered_ = loka::app::scene::KeySnapshot<loka::app::CloseAsk>(&this->props.documents()->closeAsked());
      this->decideClose(ask);
    }
    void decideClose(const loka::app::CloseAsk &ask)
    {
      if (this->operation_.get() != NONE)
      {
        if (!ask.id.isNone())
          this->props.documents()->declineClose(ask);
        return;
      }
      const Document *entry = this->props.documents()->find(this->props.document());
      if (entry && entry->edited)
      {
        this->operation_.set(CONFIRM_CLOSE);
        return;
      }
      this->closeDocument();
    }
    void closeDocument()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      // A second Close before admission finds the document already gone.
      const loka::core::ListEditResult result = this->props.documents()->close(this->props.document());
      assert(result == loka::core::EDIT_OK || result == loka::core::EDIT_ID_NOT_FOUND);
      (void)result;
    }
    void declineConfirmedClose()
    {
      this->operation_.set(NONE);
      const loka::app::CloseAsk &ask = this->answered_.value();
      if (!ask.id.isNone())
        this->props.documents()->declineClose(ask);
    }
    void completeConfirm()
    {
      using namespace loka::app;
      const ConfirmResult result = this->confirmResult_.get();
      if (this->operation_.get() != CONFIRM_CLOSE || result.kind == ConfirmResult::CONFIRM_NONE)
        return;
      switch (result.kind)
      {
      case ConfirmResult::CONFIRM_CANCEL:
        this->declineConfirmedClose();
        break;
      case ConfirmResult::CONFIRM_DISCARD:
        this->closeDocument();
        break;
      case ConfirmResult::CONFIRM_ERROR:
        this->error_.set(loka::core::String::Literal("Confirmation dialog failed."));
        this->declineConfirmedClose();
        break;
      case ConfirmResult::CONFIRM_SAVE:
      {
        this->operation_.set(SAVE_FOR_CLOSE);
        const Choice destination = this->currentFile();
        if (destination.kind == Choice::RESULT_FILE)
          this->completeSaveForClose(this->commitDocument(SAVE_DOCUMENT, destination));
        break;
      }
      case ConfirmResult::CONFIRM_NONE:
        break;
      }
    }
    void completeSaveForClose(bool committed)
    {
      if (committed)
        this->closeDocument();
      else
        this->declineConfirmedClose();
    }
    void openDocument()
    {
      if (this->operation_.get() == NONE)
        this->operation_.set(OPEN);
    }
    void saveAsDocument()
    {
      if (this->operation_.get() == NONE)
        this->operation_.set(SAVE);
    }
    void saveDocument()
    {
      if (this->operation_.get() != NONE)
        return;
      const Choice file = this->currentFile();
      if (file.kind == Choice::RESULT_FILE)
        this->commitDocument(SAVE_DOCUMENT, file);
      else
        this->saveAsDocument();
    }

    static void opened(const Choice &result, void *owner)
    {
      static_cast<MainNode *>(owner)->completeChooser(OPEN, result);
    }
    static void saved(const Choice &result, void *owner)
    {
      MainNode *node = static_cast<MainNode *>(owner);
      node->completeChooser(node->operation_.get() == SAVE_FOR_CLOSE ? SAVE_FOR_CLOSE : SAVE, result);
    }
    void completeChooser(Operation expected, const Choice &result)
    {
      if (this->operation_.get() != expected || (result.kind == Choice::RESULT_NONE && expected != SAVE_FOR_CLOSE))
        return;
      this->operation_.set(NONE);
      switch (result.kind)
      {
      case Choice::RESULT_FILE:
        if (expected == SAVE_FOR_CLOSE)
          this->completeSaveForClose(this->commitDocument(SAVE_DOCUMENT, result));
        else
          this->commitDocument(expected == OPEN ? OPEN_DOCUMENT : SAVE_DOCUMENT, result);
        return;
      case Choice::RESULT_FOLDER:
      case Choice::RESULT_ERROR:
        this->error_.set(loka::core::String::Literal("File dialog failed."));
        break;
      case Choice::RESULT_CANCELED:
      case Choice::RESULT_NONE:
        break;
      }
      if (expected == SAVE_FOR_CLOSE)
        this->declineConfirmedClose();
    }

    static loka::core::String failureText(DocumentAction action, loka::app::TextDocumentResult result)
    {
      using namespace loka::app;
      switch (action)
      {
      case NEW_DOCUMENT:
        return loka::core::String::Literal("Cannot start a new document.");
      case OPEN_DOCUMENT:
        if (result == TEXT_DOCUMENT_TOO_LARGE)
          return loka::core::String::Literal("Cannot open the file: it is too long for the editor.");
        if (result == TEXT_DOCUMENT_NON_ASCII)
          return loka::core::String::Literal("Cannot open the file: it is not plain ASCII text.");
        return loka::core::String::Literal("Cannot open the file.");
      case SAVE_DOCUMENT:
        // The prepare door refuses a non-TEXT file before anything is written.
        if (result == TEXT_DOCUMENT_NOT_TEXT)
          return loka::core::String::Literal("Not saved: the file is not a text file.");
        return loka::core::String::Literal("Save failed; the destination may have changed.");
      }
      return loka::core::String();
    }

    /** The sole destination writer. Replacement success also supersedes stale caret IDs. */
    bool commitDocument(DocumentAction action, const Choice &destination)
    {
      using namespace loka::app;
      loka::core::StateTrackerGuard guard(this->tracker());
      bool committed = false;
      TextDocumentResult result = TEXT_DOCUMENT_OK;
      switch (action)
      {
      case NEW_DOCUMENT:
      {
        // Kept for attachNode's first fill; File > New opens a roster entry.
        EmptyDocument replacement(this->buffer_.lines());
        committed = this->buffer_.lines().apply(replacement) == loka::core::EDIT_OK;
        break;
      }
      case OPEN_DOCUMENT:
        result = ReadTextDocument(this->props.platformContext(), destination.item, this->buffer_);
        committed = result == TEXT_DOCUMENT_OK;
        break;
      case SAVE_DOCUMENT:
        result = WriteTextDocument(this->props.platformContext(), destination.item, this->buffer_);
        committed = result == TEXT_DOCUMENT_OK;
        break;
      }
      if (!committed)
      {
        this->error_.set(failureText(action, result));
        return false;
      }
      this->saved_ = loka::app::scene::KeySnapshot<loka::core::ListRevision>(&this->buffer_.lines().revision());
      this->publishFacts(destination);
      this->error_.set(loka::core::String());
      if (action != SAVE_DOCUMENT)
        this->caret_.set(LineCursor(this->buffer_.lines().at(0).id, 0));
      return true;
    }

    Choice currentFile() const
    {
      const Document *entry = this->props.documents()->find(this->props.document());
      return entry ? entry->file : Choice();
    }

    void publishFacts()
    {
      this->publishFacts(this->currentFile());
    }

    void publishFacts(const Choice &file)
    {
      if (this->props.document().isNone())
        return;
      const Document *entry = this->props.documents()->find(this->props.document());
      const Document previous = entry ? *entry : Document();
      const bool edited = !this->saved_.matches();
      if (!(previous.file != file) && previous.edited == edited)
        return;
      loka::core::String title = previous.title;
      if (previous.file != file)
      {
        title = file.kind == Choice::RESULT_FILE ? file.item.name() : loka::core::String();
        if (title.empty())
          title = loka::core::String::Literal("Untitled");
      }
      const loka::core::ListEditResult result =
          this->props.documents()->update(this->props.document(), Document(file, title, edited));
      assert(result == loka::core::EDIT_OK || result == loka::core::EDIT_ID_NOT_FOUND);
      (void)result;
    }

    loka::app::CommandSet<FileCommand, FILE_COMMAND_COUNT> commands_;
    loka::app::TextBuffer buffer_;
    loka::app::scene::Reported<loka::app::LineCursor> cursor_;
    loka::app::scene::Request<loka::app::LineCursor> caret_;
    loka::app::scene::KeySnapshot<loka::core::ListRevision> saved_;
    loka::app::scene::KeySnapshot<loka::app::CloseAsk> answered_;
    loka::app::scene::NodeState<Operation> operation_;
    loka::app::scene::NodeState<Choice> openResult_, saveResult_;
    loka::app::scene::NodeState<loka::app::ConfirmResult> confirmResult_;
    loka::app::scene::NodeState<loka::core::String> error_;
    loka::app::scene::DerivedNodeState<bool> errorVisible_;
    loka::app::scene::FlowSlot<ChoiceFlow> openFlow_, saveFlow_;
  };
} // namespace simpletext
#endif
