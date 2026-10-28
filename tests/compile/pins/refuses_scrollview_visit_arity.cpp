#include "app/nodes/nestable/ScrollViewSettlement.hpp"
#include "platform/null/NullScenePlatformController.hpp"
using namespace loka::app;
using namespace loka::app::scene;
using loka::core::Frame;
class Arity1 : public NullScenePlatformController
{
  struct Result
  {
    const ScrollViewVisit visit;
    Result()
        : visit(ScrollViewVisit::succeededUnclamped())
    {
    }
  };
  Result layout(ScrollViewNode *, const LayoutState &)
  {
    return Result();
  }
  bool apply(ScrollViewNode &, const Result &, const Frame &) const
  {
    return true;
  }

public:
  void run(ScrollViewNode *node)
  {
    LayoutState state;
    SettleScrollViewVisit(*this, &Arity1::layout, &Arity1::apply, node, state);
  }
};
class Arity2 : public NullScenePlatformController
{
  struct Result
  {
    const ScrollViewVisit visit;
    Result()
        : visit(ScrollViewVisit::succeededUnclamped())
    {
    }
  };
  Result layout(ScrollViewNode *, LayoutState &, int)
  {
    return Result();
  }
  bool apply(ScrollViewNode &, const Result &, const Frame &)
  {
    return true;
  }

public:
  void run(ScrollViewNode *node)
  {
    LayoutState state;
    SettleScrollViewVisit(*this, &Arity2::layout, &Arity2::apply, node, state, 0);
  }
};
class Arity3 : public NullScenePlatformController
{
  struct Result
  {
    const ScrollViewVisit visit;
    Result()
        : visit(ScrollViewVisit::succeededUnclamped())
    {
    }
  };
  Result layout(scene::Node *, const LayoutState &, int, int *)
  {
    return Result();
  }
  bool apply(ScrollViewNode &, const Result &, const Frame &)
  {
    return true;
  }

public:
  void run(ScrollViewNode *node)
  {
    LayoutState state;
    SettleScrollViewVisit(*this, &Arity3::layout, &Arity3::apply, node, state, 0, static_cast<int *>(0));
  }
};
