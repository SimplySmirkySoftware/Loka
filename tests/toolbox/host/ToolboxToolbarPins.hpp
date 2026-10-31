#ifndef LOKA_TOOLBOX_TOOLBAR_PINS_HPP
#define LOKA_TOOLBOX_TOOLBAR_PINS_HPP
#include "app/nodes/controls/Toolbar.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "ToolboxLayoutMetrics.hpp"

namespace
{
  /** The Toolbox Row twin receives real natural metrics and paints its Text leaf. */
  class ToolbarPaintTraversal : public IPlatformLayoutTraversal
  {
  public:
    explicit ToolbarPaintTraversal(ToolboxScenePlatformController &controller)
        : controller_(controller),
          y_(0),
          labelSeat(),
          text(0)
    {
    }
    virtual bool queryNaturalWidth(Node *node, short &width) const
    {
      return this->controller_.queryNaturalWidth(node, width);
    }
    virtual bool queryNaturalHeight(Node *node, short &height) const
    {
      return this->controller_.queryNaturalHeight(node, height);
    }
    virtual int layoutChild(Node *node, const LayoutState &offer)
    {
      LayoutState state = offer;
      if (node->asTextNode())
      {
        this->labelSeat = offer;
        NodeContext *context =
            this->controller_.nodeHandlerRegistry_.find(node)->ensureContext(node, &this->controller_, state);
        LOKA_VERIFY(context != 0);
        this->text = static_cast<ToolboxTextContext *>(context);
        const int width = node->layout(&this->controller_, state);
        this->y_ = state.y;
        return width;
      }
      LOKA_VERIFY(node->asButtonNode() != 0);
      this->y_ = static_cast<short>(state.y + state.height);
      return state.width;
    }
    virtual void setLayoutResultY(short y)
    {
      this->y_ = y;
    }
    virtual short layoutResultY() const
    {
      return this->y_;
    }

  private:
    ToolboxScenePlatformController &controller_;
    short y_;

  public:
    LayoutState labelSeat;
    ToolboxTextContext *text;
  };

  void ToolbarPaintPins()
  {
    const char *values[] = {"Items: 3", "A very long toolbar label that cannot fit", "x", ""};
    for (int narrow = 0; narrow != 2; ++narrow)
      for (unsigned value = 0; value != sizeof(values) / sizeof(values[0]); ++value)
      {
        ToolboxWindow window;
        HostPortScope port(window.window());
        ToolboxScenePlatformController controller(&window);
        LOKA_VERIFY(RegisterToolboxBuiltInSupport(controller));
        Toolbar definition;
        definition << ToolbarItem("Add item") << ToolbarItem::label(values[value]) << ToolbarItem("Show/Hide map");
        StackNode row(definition.props);
        loka::dsl::CompositionCursor<NodeDefinitionBase> definitions(definition.childrenHead(),
                                                                     definition.childrenCount());
        for (NodeDefinitionBase *child = definitions.next(); child; child = definitions.next())
        {
          Node *node = child->create();
          LOKA_VERIFY(node != 0);
          row.addChild(node);
        }
        short first = 0, last = 0;
        LOKA_VERIFY(controller.queryNaturalWidth(row.childrenHead(), first));
        LOKA_VERIFY(controller.queryNaturalWidth(row.childrenHead()->nextInComposition->nextInComposition, last));
        LayoutState offer = Seat(narrow ? static_cast<short>(first + last + 17) : 360);
        offer.lineHeight = 14;
        offer.spacing = static_cast<short>(layout::resolveLayoutGap(&row, 0, ToolboxLayoutMetrics::spacing()));
        ToolbarPaintTraversal traversal(controller);
        ComputeToolboxRowLayout(&row, offer, &traversal);
        LOKA_VERIFY(traversal.text != 0);
        const short expected = narrow ? 1 : static_cast<short>(360 - first - last - 16);
        LOKA_VERIFY(traversal.labelSeat.width == expected);
        LOKA_VERIFY(traversal.labelSeat.x == offer.x + first + 8);
        layout::RowWidthConsultation common(row.childrenHead(),
                                            row.childrenCount(),
                                            offer.width,
                                            offer.spacing,
                                            ToolboxLayoutMetrics::spacing(),
                                            row.props.rowUndeclaredWidth_,
                                            &controller);
        common.next(row.childrenHead());
        LOKA_VERIFY(common.next(row.childrenHead()->nextInComposition).width() == expected);
        const Rect rect = MeasurementRect(*traversal.text);
        LOKA_VERIFY(rect.right - rect.left == expected);
        toolbox_host::reset();
        traversal.text->draw(&controller);
        LOKA_VERIFY(toolbox_host::pascalDraws.size() == 1);
        LOKA_VERIFY(toolbox_host::draws.size() == 1);
        const std::string drawn = toolbox_host::pascalDraws[0];
        if (narrow)
          LOKA_VERIFY(drawn.empty());
        else
          LOKA_VERIFY(drawn == values[value]);
        const short advance = TextWidth(drawn.data(), 0, static_cast<short>(drawn.size()));
        LOKA_VERIFY(toolbox_host::draws[0].x == rect.left + (expected - advance) / 2);
        LifecycleFactTestAccess::MarkSubtreeRetired(&row);
        LifecycleFactTestAccess::DeliverFacts(&row);
        controller.releaseNodeContexts(&row);
        controller.retired.clear();
      }
  }
} // namespace
#endif
