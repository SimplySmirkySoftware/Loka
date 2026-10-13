#ifndef LOKA_APP_CONFIRM_DIALOG_HPP
#define LOKA_APP_CONFIRM_DIALOG_HPP

#include <cassert>
#include "core/State.hpp"
#include "core/String.hpp"
#include "app/scene/Node.hpp"
#include "app/scene/state/NodeState.hpp"

namespace loka
{
  namespace app
  {
    /** Completed message owned by the definition; button policy belongs to rails. */
    class ConfirmDialogOptions
    {
    public:
      explicit ConfirmDialogOptions(const core::String &message = core::String())
          : message_(message) {}
      const core::String &message() const { return this->message_; }
      int compare(const ConfirmDialogOptions &other) const
      {
        return this->message_.compare(other.message_);
      }
    private:
      core::String message_;
    };

    /** A completed choice, or NONE before completion. Only ERROR carries a code. */
    struct ConfirmResult
    {
      enum Kind { CONFIRM_NONE, CONFIRM_SAVE, CONFIRM_DISCARD, CONFIRM_CANCEL, CONFIRM_ERROR };
      ConfirmResult() : kind(CONFIRM_NONE), errorCode(0) {}
      static ConfirmResult none() { return ConfirmResult(); }
      static ConfirmResult Save() { return ConfirmResult(CONFIRM_SAVE, 0); }
      static ConfirmResult Discard() { return ConfirmResult(CONFIRM_DISCARD, 0); }
      static ConfirmResult Cancel() { return ConfirmResult(CONFIRM_CANCEL, 0); }
      static ConfirmResult Error(int code) { return ConfirmResult(CONFIRM_ERROR, code); }
      bool operator==(const ConfirmResult &other) const
      {
        return this->kind == other.kind
            && (this->kind != CONFIRM_ERROR || this->errorCode == other.errorCode);
      }
      bool operator!=(const ConfirmResult &other) const { return !(*this == other); }
      Kind kind;
      int errorCode;
    private:
      ConfirmResult(Kind value, int code) : kind(value), errorCode(code) {}
    };

    class ConfirmDialogTypeTag {};

    class ConfirmDialogNode;

    /** Borrows the app-owned result channel; completion emission is optional. */
    struct ConfirmDialogProps : public loka::app::scene::NodePropsBase<ConfirmDialogProps>
    {
      typedef ConfirmDialogTypeTag TypeTag;
      typedef ConfirmDialogNode NodeType;
      ConfirmDialogOptions options_;
      loka::app::scene::NodeState<ConfirmResult> result_;
      loka::core::EmitterState *onResult_;
      void *windowToAttach_;
      ConfirmDialogProps()
          : result_(),
            onResult_(0),
            windowToAttach_(0)
      {
      }

      ConfirmDialogProps &result(const loka::app::scene::NodeState<ConfirmResult> &state)
      {
        this->result_ = state;
        return *this;
      }

      ConfirmDialogProps &onResult(loka::core::EmitterState *emitter)
      {
        this->onResult_ = emitter;
        return *this;
      }

      ConfirmDialogProps &attachToWindow(void *window)
      {
        this->windowToAttach_ = window;
        return *this;
      }

      bool operator<(const loka::app::scene::PropsBase &rhs) const
      {
        if (rhs.propsTypeId() != this->propsTypeId())
          return false;
        const ConfirmDialogProps &other = static_cast<const ConfirmDialogProps &>(rhs);
        const int optionsOrder = this->options_.compare(other.options_);
        if (optionsOrder != 0)
          return optionsOrder < 0;
        if (this->result_.state() != other.result_.state())
          return this->result_.state() < other.result_.state();
        if (this->onResult_ != other.onResult_)
          return this->onResult_ < other.onResult_;
        return this->windowToAttach_ < other.windowToAttach_;
      }
    };

    class ConfirmDialogNode : public loka::app::scene::Node, public loka::app::scene::IProjectedLayoutNode
    {
    public:
      typedef ConfirmDialogTypeTag TypeTag;
      ConfirmDialogProps props;
      ConfirmDialogNode(const ConfirmDialogProps &p)
          : props(p)
      {
        assert(this->props.result_.isValid() &&
               "ConfirmDialog requires an app-owned result channel; "
               "close the owning Show from completion");
      }
      virtual loka::app::scene::NodeKind kind() const
      {
        return loka::app::scene::NODE_KIND_CONFIRM_DIALOG;
      }
      virtual loka::app::scene::IProjectedLayoutNode *asProjectedLayoutNode()
      {
        return this;
      }
      virtual const void *nodeTypeKey() const
      {
        return loka::app::scene::NodeTypeToken<ConfirmDialogNode>();
      }
      virtual ConfirmDialogNode *asConfirmDialogNode()
      {
        return this;
      }
      virtual short layoutProjected(loka::app::scene::IPlatformController *controller,
                                    loka::app::scene::LayoutState &state)
      {
        if (!controller)
        {
          return state.y;
        }
        if (!loka::app::scene::PrepareProjectedLayout(controller, this, state))
        {
          return state.y;
        }
        return state.y;
      }
      virtual void declareDirtySources(loka::app::scene::DirtySourceRegistrar &registrar)
      {
        (void)registrar;
      }
    };

    struct ConfirmDialogDefinition : public loka::app::scene::NodeDefinition<ConfirmDialogProps, ConfirmDialogNode>,
                                      public loka::app::scene::TestIdDslMixin<ConfirmDialogDefinition>
    {
      ConfirmDialogDefinition()
          : loka::app::scene::NodeDefinition<ConfirmDialogProps, ConfirmDialogNode>()
      {
      }
      ConfirmDialogDefinition(const ConfirmDialogProps &p)
          : loka::app::scene::NodeDefinition<ConfirmDialogProps, ConfirmDialogNode>(p)
      {
      }

      explicit ConfirmDialogDefinition(const ConfirmDialogOptions &options)
          : loka::app::scene::NodeDefinition<ConfirmDialogProps, ConfirmDialogNode>()
      {
        this->props.options_ = options;
      }

      ConfirmDialogDefinition &attachToWindow(void *window)
      {
        this->props.windowToAttach_ = window;
        return *this;
      }

      ConfirmDialogDefinition &result(const loka::app::scene::NodeState<ConfirmResult> &state)
      {
        this->props.result(state);
        return *this;
      }

      ConfirmDialogDefinition &onResult(loka::core::EmitterState *emitter)
      {
        this->props.onResult_ = emitter;
        return *this;
      }

      using loka::app::scene::NodeDefinition<ConfirmDialogProps, ConfirmDialogNode>::create;
    };

    typedef ConfirmDialogDefinition ConfirmDialog;
  } // namespace app
} // namespace loka
#endif
