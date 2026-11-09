#ifndef LOKA_WEB_SCENE_PLATFORM_CONTROLLER_HPP
#define LOKA_WEB_SCENE_PLATFORM_CONTROLLER_HPP
#include "WebResources.hpp"
#include "app/scene/projection/ProjectionParentScope.hpp"
#include "app/nodes/nestable/ScrollViewSettlement.hpp"
#include "WebTextLayout.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/projection/PlatformLayoutHandler.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "app/layout/NaturalSizeSource.hpp"
class WebSession;
/** Window-owned DOM projection; layout containers other than ScrollView create no DOM nodes.
    Cost: apply visits the supplied Scene subtree once; retirement drains only
    this controller's intrusive rows once per safe completion or destruction. */
class WebScenePlatformController : public loka::app::scene::IPlatformController,
                                   public loka::app::layout::INaturalSizeSource
{
public:
  WebScenePlatformController(WebSession &session, loka::web::Token root, int width, int height);
  virtual ~WebScenePlatformController();
  /** Cost: Scene apply once; delegates a traversal of that supplied Scene subtree. */
  virtual void onChange(loka::app::scene::Node *, loka::app::scene::NodeDirtyFlags, bool);
  /** Cost: Window safe completion once; O(1), DOM writes are synchronous. */
  virtual void synchronize();
  /** Cost: Window poll once per admission; O(1) own retirement-list emptiness. */
  virtual bool hasPendingSync() const;
  /** Cost: Scene/Window teardown; releases its projection contexts, then own retired rows. */
  virtual void destroy();
  /** Cost: Window safe completion or destroy; walks only own retired rows once. */
  virtual void drainNativeRetirements();
  /** Cost: each leaf layout visit; fixed handler lookup and at most one creation attempt. */
  virtual bool prepareProjectedLayout(loka::app::scene::Node *, loka::app::scene::LayoutState &);
  /** Cost: measurement refusal or resize; O(1) request on the mounted owner Scene. */
  virtual void requestRelayout();
  /** Cost: Scene completion once; one own root native query and bounded route lookup. */
  virtual bool readNativeFocus(loka::app::scene::NodeContext *&out);
  /** Cost: layout natural-size query; one own text run, O(scalars), no tree walk. */
  virtual bool queryNaturalWidth(loka::app::scene::Node *, short &) const;
  /** Cost: layout natural-size query; one normalized temporary and service query for one leaf. */
  virtual bool queryNaturalHeight(loka::app::scene::Node *, short &) const;
  /** Cost: input receiver twice per click; bounded ancestor route lookup and
      App-owned pending-close query, O(pending closures), no scene walk. */
  bool acceptsInput() const;
  /** Cost: root input once; update own layout offer and schedule its Scene. */
  void resize(int width, int height);
  /** Cost: terminal fact once; one bounded extraction and O(1) own retirement-list insertion. */
  void retire(WebElement &element);
  /** Cost: handler once per missing-context visit; one own resource construction attempt. */
  bool materialize(WebElement &element, loka::web::Kind kind, WebResourceContext &receiver);

  /** Cost: Leaf layout once; translates one supplied seat in O(1). */
  bool project(const loka::app::scene::LayoutState &, loka::app::scene::LayoutState &);
  /** Cost: Traversal failure once; marks only the current channel in O(1). */
  void refuseMeasurement();
  /** Cost: Each channel coordinate before assignment; checks one integer, no rows. */
  bool checkCoordinate(int value);

private:
  struct ScrollResult
  {
    ScrollResult(int y, const loka::app::ScrollViewVisit &v)
        : bottom(y),
          visit(v)
    {
    }
    const int bottom;
    const loka::app::ScrollViewVisit visit;
  };
  ScrollResult layoutScroll(loka::app::ScrollViewNode *, const loka::app::scene::LayoutState &);
  bool applyScroll(loka::app::ScrollViewNode &, const ScrollResult &, const loka::core::Frame &);
#ifdef TEST_BUILD
  friend struct loka::web::testing::Access;
#endif
  class Traversal;
  /** Cost: one supplied node per traversal visit; containers delegate child layout through common handlers. */
  int layoutNode(loka::app::scene::Node *, const loka::app::scene::LayoutState &);
  WebSession &session_;
  const loka::web::Token root_;
  loka::app::scene::Node *tree_;
  loka::app::scene::LayoutState layout_;
  loka::app::scene::PlatformLayoutHandlerRegistry layouts_;
  WebResource *retired_;
  loka::app::scene::ProjectionParentScopeStack projectionParents_;
};
/** Shared leaf resource procedure. Initial lifecycle is applied after adoption,
    before afterAttach (RetainedNodeHandler). Cost: one owned element per fact,
    layout or render visit; no live-state subscriptions outside the logical node. */
/** Owns one native token and the shared synchronous retirement procedure. */
class WebResourceContext : public loka::app::scene::NativeNodeContext
{
public:
  WebResourceContext(loka::app::scene::Node &, WebScenePlatformController &);
  virtual ~WebResourceContext();
  /** Cost: Missing context once; creates one owned resource through its controller. */
  bool materialize(loka::web::Kind kind);
  /** Cost: Adoption once; O(1) test refusal, unconditional acceptance in shipping. */
  virtual bool acceptsInstallation() const;
  /** Cost: Adoption once; writes visibility of one owned resource. */
  void readLifecycleFactOnAttach();
  /** Cost: Lifecycle transition once; retires or hides one owned resource. */
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact);
  WebScenePlatformController &controller()
  {
    return this->controller_;
  }
  loka::web::Token token() const
  {
    return this->element_.token();
  }
  /** Cost: input ingress and dispatch once each; reads own token and owner fact. */
  bool attached() const;

protected:
  friend struct loka::web::testing::Access;
  WebScenePlatformController &controller_;
  WebElement element_;
};
/** Leaf projection snapshot; token lifetime belongs to WebResourceContext. */
class WebElementContext : public WebResourceContext
{
public:
  WebElementContext(loka::app::scene::Node &node, WebScenePlatformController &controller)
      : WebResourceContext(node, controller)
  {
  }
  /** Cost: Lifecycle transition once; retires or hides one owned resource. */
  virtual void onFactChanged(loka::app::scene::NodeLifecycleFact, loka::app::scene::NodeLifecycleFact);
  virtual short layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &);
  virtual void render(loka::app::scene::IPlatformController *);
  bool active() const;
  virtual bool click()
  {
    return false;
  }

protected:
  WebTextSlot projection_;
};
class WebTextContext : public WebElementContext
{
public:
  WebTextContext(loka::app::scene::Node &node, WebScenePlatformController &c)
      : WebElementContext(node, c)
  {
  }
};
class WebButtonContext : public WebElementContext
{
public:
  WebButtonContext(loka::app::scene::Node &node, WebScenePlatformController &c)
      : WebElementContext(node, c)
  {
  }
  /** Cost: one admitted native click; one own emitter, no rows. */
  virtual bool click();
};
#endif
