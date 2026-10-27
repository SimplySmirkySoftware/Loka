#ifndef LOKA_TOOLBOX_SCROLL_VIEW_SETTLEMENT_HPP
#define LOKA_TOOLBOX_SCROLL_VIEW_SETTLEMENT_HPP
#include "ToolboxScrollViewVisit.hpp"
#include "app/nodes/nestable/ScrollViewSettlement.hpp"
class ToolboxScenePlatformController;
/** Stack owner for one protected Toolbox projection visit. */
class ToolboxScrollViewOwner : public loka::app::scene::SettleOwner<loka::core::Frame>
{
public:
  ToolboxScrollViewOwner(loka::app::scene::IPlatformController &controller, loka::app::scene::Node *node)
      : loka::app::scene::SettleOwner<loka::core::Frame>(controller, node, node->getContext())
  {
  }
  virtual loka::app::scene::FollowUpResult finishSettle(loka::app::scene::Node &, const loka::app::scene::FollowUps &)
  {
    return loka::app::scene::FOLLOW_UP_NONE;
  }
#ifdef TEST_BUILD
  virtual loka::core::Frame fact(loka::app::scene::Node &base) const
  {
    const loka::app::ScrollViewNode &node = static_cast<loka::app::ScrollViewNode &>(base);
    return node.props.viewport_.isValid() ? node.props.viewport_.state()->get() : loka::core::Frame();
  }
#endif
};
/** Applies only the rows committed by this visit; the controller owns their storage. */
class ToolboxScrollViewSeat : public loka::app::ScrollViewSeat
{
public:
  ToolboxScrollViewSeat(ToolboxScenePlatformController &controller, const ToolboxScrollViewLayoutResult &result)
      : loka::app::ScrollViewSeat(result.visit),
        controller_(controller),
        rows_(result.rows)
  {
  }
  virtual loka::app::scene::RequestApplication<loka::core::Frame, loka::app::ScrollRequestResult>
  apply(loka::app::scene::Node &base, const loka::app::ScrollTarget &pending);

private:
  ToolboxScenePlatformController &controller_;
  const ToolboxViewportRows rows_;
};
#endif
