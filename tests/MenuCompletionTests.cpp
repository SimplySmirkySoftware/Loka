#include "MenuCompletionTests.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/MenuComposition.hpp"
#include "app/MenuProjectionRecord.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullApp.hpp"
#include "support/TestVerify.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "core/Operation.hpp"
#include "core/util/StateTrackerGuard.hpp"

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  struct Offer
  {
    Offer(Window *w, const MenuSources &s) : window(w), sources(s) {}
    Window *window;
    MenuSources sources;
  };
  class RecordingApp : public NullApp
  {
  public:
    explicit RecordingApp(AppConfigurable *config = 0) : NullApp(config) {}
    void own(Window *w)
    {
      if (!this->group_)
        this->group_ = new AppComponentGroup(std::vector<AppComponent *>());
      this->group_->adopt(w);
    }
    using App::admitAndApplyWindows;
    using App::reclaimWindows;
    void operationLoop()
    {
      RunWindowAdmissionOperation(*this);
    }
    std::vector<Offer> offers;
  protected:
    virtual void projectMenu(Window *w, const MenuSources &sources)
    {
      this->offers.push_back(Offer(w, sources));
    }
  };
  MenuBarDefinition defaultBar()
  {
    MenuBarDefinition bar;
    bar << (Menu("File") << MenuItem("Open")) << (Menu("Help") << MenuItem("Help"));
    return bar;
  }
  template <int Kind> class MenuRoot : public BoundaryNodeFor<MenuRoot<Kind> >
  {
  public:
    explicit MenuRoot(const BoundaryPropsFor<MenuRoot<Kind> > &props)
        : BoundaryNodeFor<MenuRoot<Kind> >(props) {}
    virtual void composeNode(NodeComposition &c)
    {
      MenuBarDefinition bar;
      switch (Kind)
      {
      case 0:
        return;
      case 1:
        bar << (Menu("View") << MenuItem("Zoom"));
        break;
      default:
        bar << (Menu("File") << MenuItem("Save"));
        break;
      }
      LOKA_VERIFY(c.menuBar(bar));
    }
  };
  template <int Kind> NullWindow *addWindow(RecordingApp &app, NullPlatformContext &platform)
  {
    WindowProps props;
    props.scene(new Scene(Boundary<MenuRoot<Kind> >()));
    NullWindow *window = new NullWindow(&platform, props);
    app.own(window);
    return window;
  }
  class BootstrapConfig : public AppConfigurable
  {
  public:
    explicit BootstrapConfig(PlatformContext *platform) : AppConfigurable(platform) {}
    virtual void compose(AppComposition &) {}
    virtual void composeDefaultMenu(MenuComposition &c)
    {
      this->compositions.push_back(defaultBar());
      c << this->compositions.back();
    }
    std::vector<MenuBarDefinition> compositions;
  };

}
void testMenuProjectedAtCompletionNotOnActivation()
{
  NullPlatformContext platform;
  BootstrapConfig config(&platform);
  RecordingApp app(&config);
  app.run();
  app.offers.clear();
  NullWindow *window = new NullWindow(&platform, WindowProps());
  app.own(window);
  app.offers.clear();
  Operation turn;
  app.setActiveWindow(window);
  LOKA_VERIFY(app.offers.empty());
  turn.settle();
  app.admitAndApplyWindows();
  LOKA_VERIFY(app.offers.size() == 1);
  LOKA_VERIFY(app.offers[0].window == window && app.offers[0].sources.base == app.defaultMenuBar() && app.offers[0].sources.overlay == 0);
  turn.close();
  app.reclaimWindows();
}
void testLastWindowCloseProjectsDefaultInSameTurn()
{
  NullPlatformContext platform;
  BootstrapConfig config(&platform);
  RecordingApp app(&config);
  app.run();
  app.offers.clear();
  NullWindow *window = new NullWindow(&platform, WindowProps());
  app.own(window);
  app.setActiveWindow(window);
  app.offers.clear();
  Operation turn;
  app.requestWindowClose(window);
  LOKA_VERIFY(app.offers.empty());
  turn.settle();
  app.admitAndApplyWindows();
  LOKA_VERIFY(app.offers.size() == 1);
  LOKA_VERIFY(app.offers[0].window == 0 && app.offers[0].sources.source == 0);
  LOKA_VERIFY(app.offers[0].sources.base == app.defaultMenuBar() && app.offers[0].sources.overlay == 0);
  turn.close();
  app.reclaimWindows();
}

void testMenuSourceOffersDefaultAndSceneIdentity()
{
  NullPlatformContext platform;
  BootstrapConfig config(&platform);
  RecordingApp app(&config);
  app.run();
  app.offers.clear();
  NullWindow *a = addWindow<1>(app, platform);
  NullWindow *b = addWindow<2>(app, platform);
  NullWindow *plain = addWindow<0>(app, platform);
  app.setActiveWindow(a);
  app.operationLoop();
  LOKA_VERIFY(app.offers.size() == 6);
  for (size_t i = 0; i < app.offers.size(); ++i)
    LOKA_VERIFY(app.offers[i].sources.base == app.defaultMenuBar());
  LOKA_VERIFY(app.offers[0].window == a && app.offers[0].sources.source == a->scene());
  LOKA_VERIFY(app.offers[0].sources.overlay == a->scene()->menuBar());
  LOKA_VERIFY(app.offers[1].window == b && app.offers[1].sources.source == b->scene());
  LOKA_VERIFY(app.offers[1].sources.overlay == b->scene()->menuBar());
  LOKA_VERIFY(app.offers[2].window == plain && app.offers[2].sources.source == 0);
  LOKA_VERIFY(app.offers[2].sources.overlay == 0);
  for (size_t i = 0; i < 3; ++i)
    LOKA_VERIFY(app.offers[i].sources == app.offers[i + 3].sources);
}
void testMergeMenuBarsReplacesInPlaceAndAppendsInOrder()
{
  MenuBarDefinition base = defaultBar(), overlay;
  overlay << (Menu("View") << MenuItem("Zoom")) << (Menu("File") << MenuItem("Save"))
          << (Menu("Tools") << MenuItem("Run"));
  MenuBarDefinition expected;
  expected << (Menu("File") << MenuItem("Save")) << (Menu("Help") << MenuItem("Help"))
           << (Menu("View") << MenuItem("Zoom")) << (Menu("Tools") << MenuItem("Run"));
  OwnedDef<MenuBarDefinition> merged(MergeMenuBars(&base, &overlay));
  LOKA_VERIFY(merged.isSet() && merged->equalsStructure(expected));
  LOKA_VERIFY(base.equalsStructure(defaultBar()));
  OwnedDef<MenuBarDefinition> baseOnly(MergeMenuBars(&base, 0));
  OwnedDef<MenuBarDefinition> overlayOnly(MergeMenuBars(0, &overlay));
  LOKA_VERIFY(baseOnly.isSet() && baseOnly->equalsStructure(base));
  LOKA_VERIFY(overlayOnly.isSet() && overlayOnly->equalsStructure(overlay));
  OwnedDef<MenuBarDefinition> absent(MergeMenuBars(0, 0));
  LOKA_VERIFY(!absent.isSet());
  loka::app::testing::failMenuBarDefinitionClones(2);
  OwnedDef<MenuBarDefinition> refusedBase(MergeMenuBars(&base, &overlay));
  OwnedDef<MenuBarDefinition> refusedOverlay(MergeMenuBars(0, &overlay));
  loka::app::testing::allowMenuBarDefinitionClones();
  LOKA_VERIFY(!refusedBase.isSet() && !refusedOverlay.isSet());
}
void testMenuOffersDoNotConsumeCloneRefusal()
{
  NullPlatformContext platform;
  BootstrapConfig config(&platform);
  RecordingApp app(&config);
  app.run();
  NullWindow *window = addWindow<1>(app, platform);
  app.setActiveWindow(window);
  app.operationLoop(); // Publish the overlay before arming the clone refusal.
  app.offers.clear();
  loka::app::testing::failMenuBarDefinitionClones(1);
  app.operationLoop();
  LOKA_VERIFY(app.offers.size() == 2);
  LOKA_VERIFY(app.offers[0].sources == app.offers[1].sources);
  LOKA_VERIFY(app.offers[0].sources.base == app.defaultMenuBar());
  LOKA_VERIFY(app.offers[0].sources.overlay == window->scene()->menuBar());
  LOKA_VERIFY(app.offers[0].sources.source == window->scene());
  OwnedDef<MenuBarDefinition> witness(app.defaultMenuBar()->clone());
  loka::app::testing::allowMenuBarDefinitionClones();
  LOKA_VERIFY(!witness.isSet());
}
void testInactiveWindowCloseKeepsActiveBar()
{
  NullPlatformContext platform;
  BootstrapConfig config(&platform);
  RecordingApp app(&config);
  app.run();
  app.offers.clear();
  NullWindow *active = addWindow<1>(app, platform);
  NullWindow *inactive = addWindow<2>(app, platform);
  app.setActiveWindow(active);
  app.operationLoop();
  const Offer before = app.offers[0];
  app.offers.clear();
  Operation turn;
  app.requestWindowClose(inactive);
  LOKA_VERIFY(app.offers.empty());
  turn.settle();
  app.admitAndApplyWindows();
  LOKA_VERIFY(app.offers.size() == 1);
  LOKA_VERIFY(app.offers[0].window == active && app.offers[0].sources.source == before.sources.source);
  LOKA_VERIFY(app.offers[0].sources == before.sources);
  turn.close();
  app.reclaimWindows();
}
void testTwoAdmissionsReofferSameSource()
{
  NullPlatformContext platform;
  RecordingApp app;
  NullWindow *window = addWindow<1>(app, platform);
  app.setActiveWindow(window);
  app.operationLoop();
  LOKA_VERIFY(app.offers.size() == 2);
  LOKA_VERIFY(app.offers[0].sources.source == window->scene() && app.offers[1].sources.source == window->scene());
  LOKA_VERIFY(app.offers[0].sources == app.offers[1].sources);
}
void testBootstrapProjectsOnce()
{
  RecordingApp empty;
  empty.run();
  LOKA_VERIFY(empty.defaultMenuBar() == 0);
  LOKA_VERIFY(empty.offers.size() == 1 && empty.offers[0].sources.absent());
  LOKA_VERIFY(empty.offers[0].window == 0 && empty.offers[0].sources.source == 0);
  empty.offers.clear();
  empty.operationLoop();
  LOKA_VERIFY(empty.offers.size() == 2 && empty.offers[0].sources.absent());
  NullPlatformContext platform;
  BootstrapConfig config(&platform);
  RecordingApp app(&config);
  LOKA_VERIFY(app.offers.empty());
  LOKA_VERIFY(!Operation::hasActive());
  app.run();
  LOKA_VERIFY(app.offers.size() == 1);
  LOKA_VERIFY(app.offers[0].window == 0 && app.offers[0].sources.source == 0);
  LOKA_VERIFY(app.offers[0].sources.base == app.defaultMenuBar());
  LOKA_VERIFY(app.offers[0].sources.overlay == 0);
  app.offers.clear();
  app.operationLoop();
  LOKA_VERIFY(app.offers.size() == 2 && app.offers[0].sources.base == app.defaultMenuBar());
}

void testDefaultBarComposedOnceAtBootstrap()
{
  NullPlatformContext platform;
  BootstrapConfig config(&platform);
  RecordingApp app(&config);
  app.run();
  for (int i = 0; i < 5; ++i)
    app.operationLoop();
  LOKA_VERIFY(config.compositions.size() == 1);
  LOKA_VERIFY(app.defaultMenuBar() != 0);
  LOKA_VERIFY(app.offers.size() == 11);
  for (size_t i = 0; i < app.offers.size(); ++i)
    LOKA_VERIFY(app.offers[i].sources.base == app.defaultMenuBar() &&
                app.offers[i].sources.overlay == 0 && app.offers[i].sources.source == 0);
}

void testDefaultBarBootstrapCloneRefusalLeavesNoDefault()
{
  NullPlatformContext platform;
  BootstrapConfig config(&platform);
  RecordingApp app(&config);
  loka::app::testing::failNextMenuBarDefinitionClone();
  app.run();
  loka::app::testing::allowMenuBarDefinitionClones();
  LOKA_VERIFY(app.defaultMenuBar() == 0);
  NullWindow *window = addWindow<1>(app, platform);
  app.setActiveWindow(window);
  app.offers.clear();
  for (int i = 0; i < 5; ++i)
    app.operationLoop();
  LOKA_VERIFY(config.compositions.size() == 1);
  LOKA_VERIFY(app.defaultMenuBar() == 0);
  LOKA_VERIFY(app.offers.size() == 10);
  for (size_t i = 0; i < app.offers.size(); ++i)
  {
    LOKA_VERIFY(app.offers[i].window == window && app.offers[i].sources.source == window->scene());
    LOKA_VERIFY(app.offers[i].sources.base == 0 &&
                app.offers[i].sources.overlay == window->scene()->menuBar());
  }
}

void testMenuProjectionRecordIdentityAndRelease()
{
  MenuBarDefinition base, otherBase, overlay, otherOverlay;
  Scene scene((Boundary<MenuRoot<1> >())), otherScene((Boundary<MenuRoot<1> >()));
  const MenuSources offer = {&base, &overlay, &scene};
  const MenuSources changedBase = {&otherBase, &overlay, &scene};
  const MenuSources changedOverlay = {&base, &otherOverlay, &scene};
  const MenuSources changedSource = {&base, &overlay, &otherScene};
  const MenuSources absent = {0, 0, 0};
  MenuProjectionRecord record;
  LOKA_VERIFY(!record.unchanged(absent));
  LOKA_VERIFY(!record.unchanged(offer));
  // commit asserts overlay/source presence agreement in debug; do not test abort.
  record.commit(offer);
  LOKA_VERIFY(record.unchanged(offer));
  LOKA_VERIFY(!record.unchanged(changedBase));
  LOKA_VERIFY(!record.unchanged(changedOverlay));
  LOKA_VERIFY(!record.unchanged(changedSource));
  record.release(&otherScene);
  LOKA_VERIFY(record.unchanged(offer));
  record.release(0);
  LOKA_VERIFY(record.unchanged(offer));
  record.release(&scene);
  LOKA_VERIFY(!record.unchanged(offer));
  LOKA_VERIFY(!record.unchanged(absent));
  const MenuSources baseOnly = {&base, 0, 0};
  record.commit(baseOnly);
  record.release(0);
  LOKA_VERIFY(record.unchanged(baseOnly));
  record.clear();
  LOKA_VERIFY(!record.unchanged(baseOnly));
  LOKA_VERIFY(!record.unchanged(absent));
  record.commit(absent);
  LOKA_VERIFY(!record.unchanged(absent));
}
