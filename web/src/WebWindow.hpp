#ifndef LOKA_WEB_WINDOW_HPP
#define LOKA_WEB_WINDOW_HPP
#include "WebScenePlatformController.hpp"
#include "app/core/Window.hpp"
#include "app/scene/Scene.hpp"
/** Window-owned root, distinct from the controller leaf retirement queue.
    Cost: create once before mount, teardown once after child drain; one own row. */
class WebRoot
{
public:
  explicit WebRoot(WebSession &session)
      : session_(session),
        visibility_(NOT_CREATED)
  {
  }
  ~WebRoot();
  /** Cost: window factory once before mount; one own root record and adapter entry. */
  bool create(WebWindow &window, int &width, int &height);
  loka::web::Token token() const
  {
    return this->element_.token();
  }
  /** Cost: one visibility transition; one own adapter write, preserve applied fact on refusal. */
  bool park(bool hidden);
  bool isShown() const
  {
    return this->visibility_ == SHOWN;
  }

private:
  WebSession &session_;
  WebElement element_;
  enum AppliedVisibility
  {
    NOT_CREATED,
    SHOWN,
    HIDDEN
  };
  AppliedVisibility visibility_;
};
/** Native Window projects content-box facts. Cost: resize once per root event,
    delegates layout to its controller; visibility once per App admission. */
class WebWindow : public Window
{
public:
  WebWindow(PlatformContext *platform, const WindowProps &props, WebSession &session);
  virtual ~WebWindow();
  /** Cost: window factory once; root creation then one controller and Scene mount. */
  bool initialize();
  /** Cost: ingress once; O(1) own controller and Scene attachment facts. */
  bool live() const;
  /** Cost: one admitted resize; publish own geometry and request own Scene layout. */
  void handleNativeSize(int width, int height);
  WebScenePlatformController *controller()
  {
    return this->controller_;
  }
  /** Cost: App poll once per admission; O(1) own controller pending query. */
  virtual bool hasPendingScenePlatformSync() const;
  /** Cost: Window safe completion once; own controller synchronization. */
  virtual void synchronizeScenePlatform();
  /** Cost: Window safe completion once; own controller retirement rows. */
  virtual void drainNativeRetirements();

protected:
  /** Cost: Window scene seat once per replacement; delegates mount to the candidate Scene. */
  virtual bool mountReplacementScene(loka::app::scene::Scene *next);
  /** Cost: scene seat once per replacement; O(1) native presence. */
  virtual bool hasLiveScenePlatform() const;

private:
  /** Cost: App poll once per admission/continuation; O(1), no JS crossing. */
  virtual bool hasPendingNativeVisibility() const;
  /** Cost: App admission once per admitted Window; one own root if intent differs. */
  virtual void applyNativeVisibility();
  WebSession &session_;
  WebRoot root_;
  WebScenePlatformController *controller_;
};
#endif
