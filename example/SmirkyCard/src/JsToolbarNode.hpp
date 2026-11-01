#ifndef SMIRKYCARD_JS_TOOLBAR_NODE_HPP
#define SMIRKYCARD_JS_TOOLBAR_NODE_HPP

#include "CardNodes.hpp"
#include "app/CommandSet.hpp"
#include "app/nodes/controls/Toolbar.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include <string>

namespace smirkycard
{
  enum JsToolbarSlot
  {
    JS_TOOLBAR_SLOT_0,
    JS_TOOLBAR_SLOT_1,
    JS_TOOLBAR_SLOT_2,
    JS_TOOLBAR_SLOT_3,
    JS_TOOLBAR_SLOT_4,
    JS_TOOLBAR_SLOT_5,
    JS_TOOLBAR_SLOT_6,
    JS_TOOLBAR_SLOT_7,
    JS_TOOLBAR_SLOT_COUNT
  };

  /** One completed row; all pointers borrow card-owned records or read-only seats. */
  struct JsToolbarRow
  {
    enum Kind
    {
      BUTTON,
      LABEL,
      FLEXIBLE_SPACE,
      FIXED_SPACE
    };
    JsToolbarRow()
        : kind(FLEXIBLE_SPACE),
          text(0),
          enabled(0),
          handler(0),
          width(0)
    {
    }
    Kind kind;
    loka::core::String label;
    loka::core::State<loka::core::String> *text;
    loka::core::State<bool> *enabled;
    JsHandlerRecord *handler;
    short width;
    loka::core::String testId;
    loka::app::TextStyle style;
    bool operator<(const JsToolbarRow &other) const
    {
      if (this->kind != other.kind)
        return this->kind < other.kind;
      const int labelOrder = this->label.compare(other.label);
      if (labelOrder)
        return labelOrder < 0;
      if (this->text != other.text)
        return this->text < other.text;
      if (this->enabled != other.enabled)
        return this->enabled < other.enabled;
      if (this->handler != other.handler)
        return this->handler < other.handler;
      if (this->width != other.width)
        return this->width < other.width;
      const int idOrder = this->testId.compare(other.testId);
      if (idOrder)
        return idOrder < 0;
      return this->style < other.style;
    }
  };

  class JsToolbarNode;
  /** Completed projection, fixed for one structural lifetime (including parked reattach). */
  struct JsToolbarProps : loka::app::scene::NodePropsBase<JsToolbarProps>
  {
    typedef JsToolbarProps TypeTag;
    typedef JsToolbarNode NodeType;
    JsToolbarProps()
        : count(0)
    {
    }
    unsigned count;
    JsToolbarRow rows[kMaximumTreeChildren];
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      const JsToolbarProps &other = static_cast<const JsToolbarProps &>(rhs);
      if (this->count != other.count)
        return this->count < other.count;
      for (unsigned i = 0; i < this->count; ++i)
      {
        if (this->rows[i] < other.rows[i])
          return true;
        if (other.rows[i] < this->rows[i])
          return false;
      }
      return false;
    }
    const JsToolbarRow *button(JsToolbarSlot slot) const
    {
      unsigned seen = 0;
      for (unsigned i = 0; i < this->count; ++i)
        if (this->rows[i].kind == JsToolbarRow::BUTTON && seen++ == static_cast<unsigned>(slot))
          return &this->rows[i];
      return 0;
    }
  };

  namespace testing
  {
    class JsToolbarAccess;
  }
  /** Like JsClickNode, owns bindings withdrawn before the card releases records.
      Destruction never dereferences borrowed records. */
  class JsToolbarNode : public loka::app::scene::ComponentNodeWithProps<JsToolbarProps>
  {
    typedef loka::app::scene::ComponentNodeWithProps<JsToolbarProps> Base;

  public:
    explicit JsToolbarNode(const JsToolbarProps &props)
        : Base(props)
    {
    }

  protected:
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.actions(this->slots_, this, &JsToolbarNode::handleClick);
    }
    virtual void composeChildren(loka::app::scene::NodeComposition &composition)
    {
      loka::app::Toolbar toolbar;
      for (unsigned i = 0; i < this->props.count; ++i)
      {
        const JsToolbarRow &row = this->props.rows[i];
        const loka::core::StringBuffer text = row.label.bufferWithEncoding(loka::core::StringEncodingUtf8);
        const std::string label(static_cast<const char *>(text.data()), text.length());
        const loka::core::StringBuffer id = row.testId.bufferWithEncoding(loka::core::StringEncodingUtf8);
        const std::string testId(static_cast<const char *>(id.data()), id.length());
        switch (row.kind)
        {
        case JsToolbarRow::BUTTON:
          for (int k = 0; k < JS_TOOLBAR_SLOT_COUNT; ++k)
          {
            const JsToolbarSlot slot = static_cast<JsToolbarSlot>(k);
            if (this->props.button(slot) == &row)
              toolbar << loka::app::ToolbarItem(label.c_str(), this->slots_.emitter(slot))
                             .enabled(row.enabled)
                             .testId(testId.c_str());
          }
          break;
        case JsToolbarRow::LABEL:
          toolbar << (row.text ? loka::app::ToolbarItem::label(row.text) + row.style
                               : loka::app::ToolbarItem::label(label.c_str()) + row.style)
                         .testId(testId.c_str());
          break;
        case JsToolbarRow::FLEXIBLE_SPACE:
          toolbar << loka::app::ToolbarItem::flexibleSpace();
          break;
        case JsToolbarRow::FIXED_SPACE:
          toolbar << loka::app::ToolbarItem::fixedSpace(row.width);
          break;
        }
      }
      composition.declare(toolbar);
    }

  private:
    friend class testing::JsToolbarAccess;
    void handleClick(JsToolbarSlot slot)
    {
      const JsToolbarRow *row = this->props.button(slot);
      if (row)
        row->handler->card->fire(*row->handler);
    }
    loka::app::CommandSet<JsToolbarSlot, JS_TOOLBAR_SLOT_COUNT> slots_;
  };

} // namespace smirkycard
#endif
