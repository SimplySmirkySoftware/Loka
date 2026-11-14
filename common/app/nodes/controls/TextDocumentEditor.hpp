#ifndef LOKA_APP_TEXT_DOCUMENT_EDITOR_HPP
#define LOKA_APP_TEXT_DOCUMENT_EDITOR_HPP
#include "app/scene/Node.hpp"
#include "app/FocusParticipant.hpp"
#include "app/text/TextDocument.hpp"
#include "app/text/TextViewValues.hpp"
namespace loka
{
  namespace app
  {
    struct TextDocumentEditorTypeTag
    {
    };
    class TextDocumentEditorNode;
    /** Borrows a root-owned document and optional ancestor-owned attachment result. */
    struct TextDocumentEditorProps : scene::NodePropsBase<TextDocumentEditorProps>
    {
      typedef TextDocumentEditorTypeTag TypeTag;
      typedef TextDocumentEditorNode NodeType;
      explicit TextDocumentEditorProps(TextDocument &document)
          : document_(&document)
      {
      }
      TextDocument *document_;
      scene::NodeState<TextViewAttachment> attachment_;
      FocusBinding focus_;
      TextViewPresentation presentation_;
      TextDocumentEditorProps &attachment(scene::NodeState<TextViewAttachment> &value)
      {
        this->attachment_ = value;
        return *this;
      }
      TextDocumentEditorProps &presentation(const TextViewPresentation &value)
      {
        this->presentation_ = value;
        return *this;
      }
      template <class K> TextDocumentEditorProps &focusedAs(Focus<K> &fact, K key)
      {
        this->focus_ = FocusBinding(this->reportSeat(fact), this->requestSeat(fact), key);
        return *this;
      }
      bool operator<(const scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != this->propsTypeId())
          return false;
        const TextDocumentEditorProps &other = static_cast<const TextDocumentEditorProps &>(rhs);
        if (this->document_ != other.document_)
          return this->document_ < other.document_;
        if (this->attachment_.state() != other.attachment_.state())
          return this->attachment_.state() < other.attachment_.state();
        if (!this->focus_.same(other.focus_))
          return this->focus_ < other.focus_;
        return this->presentation_ < other.presentation_;
      }
    };
    /** One view of a document; replacing document or result identity requires a new node. */
    class TextDocumentEditorNode : public scene::Node, public scene::IProjectedLayoutNode
    {
    public:
      typedef TextDocumentEditorTypeTag TypeTag;
      TextDocumentEditorProps props;
      FocusParticipant focusParticipant;
      explicit TextDocumentEditorNode(const TextDocumentEditorProps &value)
          : props(value),
            focusParticipant(*this, this->props.focus_)
      {
      }
      virtual scene::FocusRow *asFocusParticipant()
      {
        return &this->focusParticipant;
      }
      virtual const void *nodeTypeKey() const
      {
        return scene::NodeTypeToken<TextDocumentEditorNode>();
      }
      virtual scene::IProjectedLayoutNode *asProjectedLayoutNode()
      {
        return this;
      }
      virtual short layoutProjected(scene::IPlatformController *controller, scene::LayoutState &state)
      {
        if (!controller || !scene::PrepareProjectedLayout(controller, this, state))
          return state.y;
        return scene::Node::layout(controller, state);
      }
      /** Publisher-only call path; one snapshot per completion, no read of the borrowed box. */
      void publishAttachment(TextViewAttachment value)
      {
        if (this->props.attachment_.isValid())
          this->props.attachment_.set(value);
      }

    private:
      bool applyProps(const TextDocumentEditorProps &next)
      {
        const FocusBinding old = this->props.focus_;
        this->props = next;
        this->focusParticipant.rebind(old);
        return true;
      }
      friend struct scene::NodePropsApplier<TextDocumentEditorNode, TextDocumentEditorProps>;
    };
    namespace scene
    {
      template <> struct NodePropsCompatibility<TextDocumentEditorNode, TextDocumentEditorProps>
      {
        static bool accepts(const TextDocumentEditorNode *node, const TextDocumentEditorProps &next)
        {
          return node->props.document_ == next.document_ && node->props.attachment_.state() == next.attachment_.state();
        }
      };
      template <> struct NodePropsApplier<TextDocumentEditorNode, TextDocumentEditorProps>
      {
        static bool apply(TextDocumentEditorNode *node, const TextDocumentEditorProps &next)
        {
          return node->applyProps(next);
        }
      };
    } // namespace scene
    /** Declarative document view; presentation contains only completed neutral values. */
    struct TextDocumentEditor : scene::NodeDefinition<TextDocumentEditorProps, TextDocumentEditorNode>,
                                scene::TestIdDslMixin<TextDocumentEditor>
    {
      explicit TextDocumentEditor(TextDocument &document)
          : scene::NodeDefinition<TextDocumentEditorProps, TextDocumentEditorNode>(TextDocumentEditorProps(document))
      {
      }
      explicit TextDocumentEditor(const TextDocumentEditorProps &props)
          : scene::NodeDefinition<TextDocumentEditorProps, TextDocumentEditorNode>(props)
      {
      }
      TextDocumentEditor &attachment(scene::NodeState<TextViewAttachment> &value)
      {
        this->props.attachment(value);
        return *this;
      }
      TextDocumentEditor &presentation(const TextViewPresentation &value)
      {
        this->props.presentation(value);
        return *this;
      }
      template <class K> TextDocumentEditor &focusedAs(Focus<K> &fact, K key)
      {
        this->props.focusedAs(fact, key);
        return *this;
      }
    };
  } // namespace app
} // namespace loka
#endif
