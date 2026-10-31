#ifndef LOKA_APP_NODES_CONTROLS_TOOLBAR_HPP
#define LOKA_APP_NODES_CONTROLS_TOOLBAR_HPP

#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/DeclaredWidth.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include <cassert>

namespace loka
{
  namespace app
  {
    struct ToolbarDefinition;
    struct ToolbarItem;

    /** A flexible or fixed empty seat in Toolbar's closed vocabulary; not a node. */
    class ToolbarSpace
    {
      friend struct ToolbarItem;
      friend struct ToolbarDefinition;
      explicit ToolbarSpace(detail::DeclaredWidth width)
          : width_(width)
      {
      }
      detail::DeclaredWidth width_;
    };

    /** A centred, one-line ellipsis label in Toolbar's closed vocabulary; not a node.
        Input must contain no CR/LF. This contract is not detected (#1668).
        Toolbox examines at most 255 native bytes, like Text. */
    class ToolbarLabel
    {
    public:
      ToolbarLabel operator+(const TextStyle &style) const
      {
        ToolbarLabel result(*this);
        result.text_.props.textStyle_ = result.text_.props.textStyle_ + style;
        return result;
      }
      ToolbarLabel &testId(const char *value)
      {
        this->text_.testId(value);
        return *this;
      }
      ToolbarLabel &testId()
      {
        this->text_.testId();
        return *this;
      }
      ToolbarLabel &noop()
      {
        this->text_.noop();
        return *this;
      }

    private:
      friend struct ToolbarItem;
      friend struct ToolbarDefinition;
      explicit ToolbarLabel(const TextDefinition &text)
          : text_(text)
      {
        this->text_.props.blockStyle_ =
            BlockStyle().wrap(TEXT_WRAP_NONE).truncation(TEXT_TRUNCATION_ELLIPSIS).align(TEXT_ALIGN_CENTER);
      }
      TextDefinition text_;
    };

    /** A constant-title button in Toolbar's closed vocabulary; not a node.
        The emitter and enabled/shown states are borrowed, with Button/Show lifetimes.
        A null emitter does nothing. A hidden shown button takes no seat or gap.
        Switching constant buttons with shown replaces the native control; keyboard
        focus is not carried to the replacement. */
    struct ToolbarItem
    {
      explicit ToolbarItem(const char *title, loka::core::EmitterState *emitter = 0)
          : button_(title, emitter),
            shown_(0)
      {
      }
      ToolbarItem &enabled(loka::core::State<bool> *state)
      {
        this->button_.enabled(state);
        return *this;
      }
      template <typename ConditionT>
      ToolbarItem &shown(ConditionT &condition,
                         typename detail::ShowResult<detail::ShowCondition<ConditionT>::value>::Type * = 0)
      {
        const loka::core::State<bool> *state = &condition;
        this->shown_ = const_cast<loka::core::State<bool> *>(state);
        return *this;
      }
      ToolbarItem &testId(const char *value)
      {
        this->button_.testId(value);
        return *this;
      }
      ToolbarItem &testId()
      {
        this->button_.testId();
        return *this;
      }
      ToolbarItem &noop()
      {
        this->button_.noop();
        return *this;
      }
      static ToolbarSpace flexibleSpace()
      {
        return ToolbarSpace(detail::DeclaredWidth());
      }
      static ToolbarSpace fixedSpace(short width)
      {
        assert(width > 0);
        return ToolbarSpace(detail::DeclaredWidth(width));
      }
      static ToolbarLabel label(const char *text)
      {
        return ToolbarLabel(Text(text));
      }
      static ToolbarLabel label(loka::core::State<loka::core::String> *text)
      {
        return ToolbarLabel(Text(text));
      }

    private:
      friend struct ToolbarDefinition;
      ButtonDefinition button_;
      loka::core::State<bool> *shown_;
    };

    /** A closed Row of buttons, spaces and centred labels. Labels share the remaining
        width, with a one-unit floor. Only ToolbarItem, ToolbarSpace and ToolbarLabel
        may be appended. Toolbar draws nothing: ground, lines and padding belong to
        the application's Box and window. */
    struct ToolbarDefinition : public scene::NestableNodeDefinition<StackProps, StackNode, ToolbarDefinition>,
                               public scene::TestIdDslMixin<ToolbarDefinition>
    {
      typedef scene::NestableNodeDefinition<StackProps, StackNode, ToolbarDefinition> BaseType;
      ToolbarDefinition()
          : BaseType(StackProps(STACK_AXIS_ROW))
      {
        this->props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL_LABELS_SHARE;
        this->props.alignVertical(VERTICAL_ALIGNMENT_CENTER);
        this->props.gap(8);
      }
      ToolbarDefinition(const ToolbarDefinition &other)
          : BaseType(other)
      {
      }
      // These overloads hide the base's generic node append door.
      ToolbarDefinition &operator<<(const ToolbarItem &item)
      {
        if (item.shown_)
          BaseType::operator<<(Show(*item.shown_) << item.button_);
        else
          BaseType::operator<<(item.button_);
        return *this;
      }
      ToolbarDefinition &operator<<(const ToolbarSpace &space)
      {
        short width = 0;
        if (space.width_.query(width))
          BaseType::operator<<(Box().width(width));
        else
          BaseType::operator<<(Spacer());
        return *this;
      }
      ToolbarDefinition &operator<<(const ToolbarLabel &label)
      {
        BaseType::operator<<(label.text_);
        return *this;
      }
    };
    typedef ToolbarDefinition Toolbar;
  } // namespace app
} // namespace loka
#endif
