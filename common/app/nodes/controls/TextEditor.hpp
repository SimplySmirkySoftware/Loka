#ifndef LOKA_APP_TEXT_EDITOR_HPP
#define LOKA_APP_TEXT_EDITOR_HPP
#include "app/scene/Node.hpp"
#include "app/FocusParticipant.hpp"
#include "app/scene/projection/SeamKey.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/scene/state/Reported.hpp"
#include "app/nodes/controls/TextEditorRequests.hpp"
#include "app/style/LineHighlighter.hpp"
#include "app/nodes/controls/TextEditorDocument.hpp"
namespace loka
{
  namespace app
  {
    namespace testing
    {
      class TextEditorAccess;
    }
    struct TextEditorTypeTag
    {
    };
    class TextEditorNode;
    /** App-owned lines, fact, request and optional highlighter outlive the node.
        List, fact and request use the same owner tracker. One request slot has
        one consumer; requests resolve ItemIds against its current binding. Lines contain ASCII with
        no embedded CR/LF/NUL; separators count toward the whole-document byte cap. */
    struct TextEditorProps : scene::NodePropsBase<TextEditorProps>
    {
      typedef TextEditorTypeTag TypeTag;
      typedef TextEditorNode NodeType;
      FocusBinding focus_;
      /** Borrows an ancestor-owned fact; LazyView keeps fact/key structurally stable. */
      template <typename K> TextEditorProps &focusedAs(Focus<K> &fact, K key)
      {
        this->focus_ = FocusBinding(this->reportSeat(fact), this->requestSeat(fact), key);
        return *this;
      }
      enum
      {
        kMaxLines = 256,
        kMaxBytes = 8192
      };
      core::ObservableList<core::String> *lines_;
      scene::RequestBinding<LineCursor> moveCaretTo_;
      scene::RequestBinding<EditorCommand> command_;

    private:
      friend class TextEditorDocument;
      scene::WriteSeat<LineCursor> cursor_;

    public:
      const LineHighlighter *highlighter_;
      TextEditorProps()
          : lines_(0),
            moveCaretTo_(),
            command_(),
            cursor_(),
            highlighter_(0)
      {
      }
      TextEditorProps(core::ObservableList<core::String> &lines, scene::Reported<LineCursor> &cursor)
          : lines_(&lines),
            moveCaretTo_(),
            command_(),
            cursor_(this->reportSeat(cursor)),
            highlighter_(0)
      {
      }
      /** None is an empty request slot; it cannot request an absent caret. */
      TextEditorProps &moveCaretTo(scene::Request<LineCursor> &value)
      {
        this->moveCaretTo_ = scene::RequestBinding<LineCursor>(this->requestSeat(value));
        return *this;
      }
      TextEditorProps &moveCaretTo(scene::RequestWithReply<LineCursor> &value)
      {
        this->moveCaretTo_ = scene::RequestBinding<LineCursor>(
            this->requestSeat(static_cast<scene::Request<LineCursor> &>(value)), this->replySeat(value));
        return *this;
      }
      TextEditorProps &moveCaretTo(scene::RequestQueueBase<LineCursor> &value)
      {
        this->moveCaretTo_ = scene::RequestBinding<LineCursor>(this->requestSeat(value), this->replySeat(value), value);
        return *this;
      }
      /** Commands execute after the caret slot drains; a continuing caret feed
          can delay them indefinitely. Replies acknowledge verbs, cursor is the fact. */
      TextEditorProps &command(scene::RequestQueueBase<EditorCommand> &value)
      {
        this->command_ = scene::RequestBinding<EditorCommand>(this->requestSeat(value), this->replySeat(value), value);
        return *this;
      }
      /** Committed logical caret; this does not promise a native selection. */
      core::State<LineCursor> *cursorState() const
      {
        return this->cursor_.state();
      }
      bool cursorUsesTracker(const core::StateTracker *tracker) const
      {
        return this->cursor_.isValid() && this->cursor_.usesTracker(tracker);
      }
      TextEditorProps &highlighter(const LineHighlighter &value)
      {
        this->highlighter_ = &value;
        return *this;
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != this->propsTypeId())
          return false;
        const TextEditorProps &other = static_cast<const TextEditorProps &>(rhs);
        if (!this->focus_.same(other.focus_))
          return this->focus_ < other.focus_;
        if (this->lines_ != other.lines_)
          return this->lines_ < other.lines_;
        if (this->cursor_.state() != other.cursor_.state())
          return this->cursor_.state() < other.cursor_.state();
        if (!this->moveCaretTo_.same(other.moveCaretTo_))
          return this->moveCaretTo_ < other.moveCaretTo_;
        if (!this->command_.same(other.command_))
          return this->command_ < other.command_;
        return this->highlighter_ < other.highlighter_;
      }
    };
    class TextEditorNode : public scene::Node, public scene::IProjectedLayoutNode
    {
    public:
      typedef TextEditorTypeTag TypeTag;
      virtual scene::FocusRow *asFocusParticipant()
      {
        return &this->focusParticipant;
      }
      TextEditorProps props;
      FocusParticipant focusParticipant;

    private:
      friend class testing::TextEditorAccess;
      friend struct scene::NodePropsApplier<TextEditorNode, TextEditorProps>;
      TextEditorDocument document;

      void discardPendingRequest(bool command = true, bool caret = true)
      {
        if (command)
          this->props.command_.clearRing();
        if (caret)
          this->props.moveCaretTo_.clearRing();
        const EditorCommand oldCommand =
            this->props.command_.isValid() ? this->props.command_.state()->get() : EditorCommand::None();
        const LineCursor oldCaret =
            this->props.moveCaretTo_.isValid() ? this->props.moveCaretTo_.state()->get() : LineCursor::None();
        if (command)
          this->props.command_.cancelFrom(oldCommand);
        if (caret)
          this->props.moveCaretTo_.cancelFrom(oldCaret);
      }

      bool applyProps(const TextEditorProps &next)
      {
#ifndef NDEBUG
        // Scene queues subscriber-driven updates for the next run. This is
        // a diagnostic for direct kernel misuse, not an admission protocol.
        static const TextEditorNode *transitioning = 0;
        assert(transitioning != this && "TextEditor Props replacement is exclusive");
        const TextEditorNode *previous = transitioning;
        transitioning = this;
#endif
        this->discardPendingRequest(this->props.lines_ != next.lines_ || !this->props.command_.same(next.command_),
                                    this->props.lines_ != next.lines_
                                        || !this->props.moveCaretTo_.same(next.moveCaretTo_));
        // A repost from cancellation is admitted to next. Do not clear again.
        const FocusBinding oldBinding = this->props.focus_;
        this->props = next;
        this->focusParticipant.rebind(oldBinding);
#ifndef NDEBUG
        transitioning = previous;
#endif
        return true;
      }

    protected:
      virtual void onLifecycleFactChanged(scene::NodeLifecycleFact previous, scene::NodeLifecycleFact next)
      {
        if (previous == scene::NODE_FACT_ATTACHED && next != scene::NODE_FACT_ATTACHED)
          this->discardPendingRequest();
      }

    public:
      explicit TextEditorNode(const TextEditorProps &p)
          : props(p),
            focusParticipant(*this, this->props.focus_),
            document(this->props)
      {
      }
      /** Native projection access; the key grants no lifetime extension. */
      TextEditorDocument &seam(const scene::SeamKey<TextEditorNode> &)
      {
        return this->document;
      }
      virtual scene::NodeKind kind() const
      {
        return scene::NODE_KIND_TEXT_EDITOR;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<TextEditorNode>();
      }
      virtual scene::IProjectedLayoutNode *asProjectedLayoutNode()
      {
        return this;
      }
      virtual short layoutProjected(scene::IPlatformController *controller, scene::LayoutState &state)
      {
        assert(this->props.lines_ && "TextEditor borrows a non-null app-owned list");
        if (!controller || !scene::PrepareProjectedLayout(controller, this, state))
          return state.y;
        return scene::Node::layout(controller, state);
      }
      virtual void declareDirtySources(scene::DirtySourceRegistrar &registrar)
      {
        assert(this->props.lines_);
        if (this->props.lines_)
          registrar.markDirtyOnChange(const_cast<core::State<core::ListRevision> *>(&this->props.lines_->revision()),
                                      scene::NODE_DIRTY_PROPS);
        if (this->props.cursorState())
          registrar.markDirtyOnChange(this->props.cursorState(), scene::NODE_DIRTY_PROPS);
        if (this->props.moveCaretTo_.isValid())
          registrar.markDirtyOnChange(this->props.moveCaretTo_.state(), scene::NODE_DIRTY_PROPS);
        if (this->props.command_.isValid())
          registrar.markDirtyOnChange(this->props.command_.state(), scene::NODE_DIRTY_PROPS);
      }
    };
    namespace scene
    {
      template <> struct NodePropsApplier<TextEditorNode, TextEditorProps>
      {
        static bool apply(TextEditorNode *node, const TextEditorProps &props)
        {
          return node->applyProps(props);
        }
      };
    } // namespace scene
    struct TextEditorDefinition : scene::NodeDefinition<TextEditorProps, TextEditorNode>,
                                  scene::TestIdDslMixin<TextEditorDefinition>
    {
      template <typename K> TextEditorDefinition &focusedAs(Focus<K> &fact, K key)
      {
        this->props.focusedAs(fact, key);
        return *this;
      }
      TextEditorDefinition()
          : scene::NodeDefinition<TextEditorProps, TextEditorNode>()
      {
      }
      explicit TextEditorDefinition(const TextEditorProps &p)
          : scene::NodeDefinition<TextEditorProps, TextEditorNode>(p)
      {
      }
      TextEditorDefinition(core::ObservableList<core::String> &lines, scene::Reported<LineCursor> &cursor)
          : scene::NodeDefinition<TextEditorProps, TextEditorNode>(TextEditorProps(lines, cursor))
      {
      }
      TextEditorDefinition &moveCaretTo(scene::Request<LineCursor> &value)
      {
        this->props.moveCaretTo(value);
        return *this;
      }
      TextEditorDefinition &moveCaretTo(scene::RequestWithReply<LineCursor> &value)
      {
        this->props.moveCaretTo(value);
        return *this;
      }
      TextEditorDefinition &moveCaretTo(scene::RequestQueueBase<LineCursor> &value)
      {
        this->props.moveCaretTo(value);
        return *this;
      }
      TextEditorDefinition &command(scene::RequestQueueBase<EditorCommand> &value)
      {
        this->props.command(value);
        return *this;
      }
      TextEditorDefinition &highlighter(const LineHighlighter &value)
      {
        this->props.highlighter(value);
        return *this;
      }
    };
    typedef TextEditorDefinition TextEditor;
  } // namespace app
} // namespace loka
#endif
