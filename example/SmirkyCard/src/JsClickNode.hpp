#ifndef SMIRKYCARD_JS_CLICK_NODE_HPP
#define SMIRKYCARD_JS_CLICK_NODE_HPP

#include "CardNodes.hpp"
#include "app/scene/node/ComponentNode.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/Cell.hpp"
#include <string>

namespace smirkycard
{
  class JsClickNode;
  /** Completed projection facts and one borrowed ancestor-owned handler. */
  struct JsClickProps : loka::app::scene::NodePropsBase<JsClickProps>
  {
    typedef JsClickProps TypeTag;
    typedef JsClickNode NodeType;
    enum Kind
    {
      BUTTON,
      CELL
    };
    JsClickProps(const loka::core::String &labelValue,
                 loka::core::State<loka::core::String> *textValue,
                 loka::core::State<bool> *enabledValue,
                 Kind kindValue,
                 JsHandlerRecord *handlerValue,
                 const loka::core::String &testIdValue)
        : label(labelValue),
          text(textValue),
          enabled(enabledValue),
          kind(kindValue),
          handler(handlerValue),
          testId(testIdValue)
    {
    }
    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      if (rhs.propsTypeId() != this->propsTypeId())
        return this->propsTypeId() < rhs.propsTypeId();
      const JsClickProps &other = static_cast<const JsClickProps &>(rhs);
      if (this->handler != other.handler)
        return this->handler < other.handler;
      if (this->kind != other.kind)
        return this->kind < other.kind;
      if (this->text != other.text)
        return this->text < other.text;
      if (this->enabled != other.enabled)
        return this->enabled < other.enabled;
      const int labelOrder = this->label.compare(other.label);
      return labelOrder ? labelOrder < 0 : this->testId.compare(other.testId) < 0;
    }
    loka::core::String label;
    loka::core::State<loka::core::String> *text;
    loka::core::State<bool> *enabled;
    Kind kind;
    JsHandlerRecord *handler;
    loka::core::String testId;
  };

  /** Like JsToolbarNode, one component owns its click bindings. Detach withdraws
      the binding before the ancestor card releases its handler records.
      Destruction never dereferences the borrowed record. */
  class JsClickNode : public loka::app::scene::ComponentNodeWithProps<JsClickProps>
  {
    typedef loka::app::scene::ComponentNodeWithProps<JsClickProps> Base;

  public:
    explicit JsClickNode(const JsClickProps &props)
        : Base(props)
    {
    }

  protected:
    virtual void declareBindings(loka::app::scene::BindingToken &token)
    {
      token.action(this->click_, this, &JsClickNode::handleClick);
    }
    virtual void composeChildren(loka::app::scene::NodeComposition &composition)
    {
      const loka::core::StringBuffer id = this->props.testId.bufferWithEncoding(loka::core::StringEncodingUtf8);
      const std::string testId(static_cast<const char *>(id.data()), id.length());
      switch (this->props.kind)
      {
      case JsClickProps::BUTTON:
      {
        loka::app::ButtonProps button;
        if (this->props.text)
          button.text(this->props.text);
        else
          button.text(this->props.label);
        if (this->props.enabled)
          button.enabled(this->props.enabled);
        button.onClick(&this->click_);
        composition.declare(loka::app::Button(button).TEST_ID(testId.c_str()));
        break;
      }
      case JsClickProps::CELL:
      {
        loka::app::CellProps cell;
        if (this->props.text)
          cell.text(this->props.text);
        else
          cell.text(this->props.label);
        cell.onClick(&this->click_);
        composition.declare(loka::app::Cell(cell).TEST_ID(testId.c_str()));
        break;
      }
      }
    }

  private:
    void handleClick()
    {
      this->props.handler->card->fire(*this->props.handler);
    }
    loka::core::EmitterState click_;
  };
} // namespace smirkycard
#endif
