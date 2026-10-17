#include "HelloWorldResponsiveTests.hpp"

#include "../example/HelloWorld/src/MainNode.hpp"
#include "../example/HelloWorld/src/MyAppConfig.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/core/Window.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "app/scene/Scene.hpp"
#include "core/util/OwnedDef.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/NullWindow.hpp"
#include "support/TestVerify.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/app/WindowTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace helloworld { namespace testing {
class MainAccess
{
public:
  static loka::core::State<loka::core::String> *randomTitle(MainNode &node, int index)
  {
    return node.randomTitles_[index].state();
  }
};
} }

namespace
{
  class ProductionConfig : public HelloWorldAppConfig
  {
  public:
    explicit ProductionConfig(PlatformContext *context)
        : HelloWorldAppConfig(context, 1)
    {
    }
    using HelloWorldAppConfig::productionWindowProps;
  };

  class MarginProjection : public NullScenePlatformController
  {
  public:
    loka::app::scene::LayoutState title;
    virtual bool prepareProjectedLayout(loka::app::scene::Node *node, loka::app::scene::LayoutState &state)
    {
      if (node->testId() == "HelloWorld.LeftPanel.Title")
        this->title = state;
      return NullScenePlatformController::prepareProjectedLayout(node, state);
    }
  };

  void driveNativeFrameAndLayout(NullWindow &window,
                                 NullScenePlatformController &platform,
                                 const loka::core::Frame &frame)
  {
    loka::app::testing::WindowTestAccess::storeNativeFrame(window, frame);
    loka::app::scene::Scene *scene = window.scene();
    LOKA_VERIFY(scene != 0);
    // The null rail has no native resize loop. Mirror the real rails' order:
    // publish the frame fact first, then lay out the now-current logical tree.
    platform.onChange(
        loka::dsl::testing::SceneTestAccess::rootNode(*scene),
        loka::app::scene::NODE_DIRTY_LAYOUT,
        false);
  }

  void findMainPanelsRecursive(loka::app::scene::Node *node,
                               long &idMatches,
                               long &typedMatches,
                               loka::app::StackNode *&result)
  {
    if (!node)
    {
      return;
    }
    if (node->testId() == "HelloWorld.MainPanels")
    {
      ++idMatches;
      loka::app::StackNode *stack = node->asStackNode();
      if (stack)
      {
        ++typedMatches;
        if (!result)
        {
          result = stack;
        }
      }
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
         child;
         child = child->nextInComposition)
    {
      findMainPanelsRecursive(child, idMatches, typedMatches, result);
    }
  }

  loka::app::StackNode *findMainPanels(loka::app::scene::Scene &scene)
  {
    long idMatches = 0;
    long typedMatches = 0;
    loka::app::StackNode *stack = 0;
    findMainPanelsRecursive(loka::dsl::testing::SceneTestAccess::rootNode(scene),
                            idMatches,
                            typedMatches,
                            stack);
    LOKA_VERIFY(idMatches == 1);
    LOKA_VERIFY(typedMatches == 1);
    LOKA_VERIFY(stack != 0);
    return stack;
  }

  void findMainPanelsScrollRecursive(loka::app::scene::Node *node,
                                     long &idMatches,
                                     long &typedMatches,
                                     loka::app::ScrollViewNode *&result)
  {
    if (!node)
    {
      return;
    }
    if (node->testId() == "HelloWorld.MainPanelsScroll")
    {
      ++idMatches;
      loka::app::ScrollViewNode *scrollView = node->asScrollViewNode();
      if (scrollView)
      {
        ++typedMatches;
        if (!result)
        {
          result = scrollView;
        }
      }
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
         child;
         child = child->nextInComposition)
    {
      findMainPanelsScrollRecursive(child, idMatches, typedMatches, result);
    }
  }

  loka::app::ScrollViewNode *findMainPanelsScroll(loka::app::scene::Scene &scene)
  {
    long idMatches = 0;
    long typedMatches = 0;
    loka::app::ScrollViewNode *scrollView = 0;
    findMainPanelsScrollRecursive(
        loka::dsl::testing::SceneTestAccess::rootNode(scene),
        idMatches,
        typedMatches,
        scrollView);
    LOKA_VERIFY(idMatches == 1);
    LOKA_VERIFY(typedMatches == 1);
    LOKA_VERIFY(scrollView != 0);
    return scrollView;
  }

  loka::app::scene::Node *findByTestId(loka::app::scene::Node *node, const char *id)
  {
    if (!node)
      return 0;
    if (node->testId() == id)
      return node;
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
         child;
         child = child->nextInComposition)
    {
      loka::app::scene::Node *found = findByTestId(child, id);
      if (found)
        return found;
    }
    return 0;
  }

  loka::app::scene::Node *findDescendantByKind(loka::app::scene::Node *node,
                                                loka::app::scene::NodeKind kind,
                                                int &skip)
  {
    if (!node)
      return 0;
    if (node->kind() == kind && skip-- == 0)
      return node;
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
         child;
         child = child->nextInComposition)
    {
      loka::app::scene::Node *found = findDescendantByKind(child, kind, skip);
      if (found)
        return found;
    }
    return 0;
  }

  struct TextObserver
  {
    TextObserver() : calls(0) {}
    static void changed(void *data) { ++static_cast<TextObserver *>(data)->calls; }
    int calls;
  };
} // namespace

void testHelloWorldResponsivePanelsFollowNativeFrameAndRetainSeats()
{
  NullPlatformContext context;
  MarginProjection platform;
  ProductionConfig config(&context);
  WindowProps props =
      config.productionWindowProps(loka::app::scene::Boundary<helloworld::MainNode>(helloworld::MainProps()));
  LOKA_VERIFY(props.contentMarginValue == loka::app::layout::LayoutGap::of(0));
  // This fixture mounts directly rather than running App admission.
  loka::app::scene::NodeDefinitionBase *rootDefinition =
      loka::app::scene::Boundary<helloworld::MainNode>(helloworld::MainProps()).clone();
  LOKA_VERIFY(rootDefinition != 0);
  props.scene(new loka::app::scene::Scene(rootDefinition));
  NullWindow *window = new NullWindow(&context, props, &platform);
  LOKA_VERIFY(window->scene() != 0);
  loka::dsl::testing::SceneTestAccess::updateAttached(*window->scene(), true);

  loka::app::ScrollViewNode *mainPanelsScroll =
      findMainPanelsScroll(*window->scene());
  loka::app::StackNode *mainPanels = findMainPanels(*window->scene());
  loka::app::scene::Node *mainPanelsInset = mainPanelsScroll->childrenHead();
  LOKA_VERIFY(mainPanelsInset != 0);
  LOKA_VERIFY(mainPanelsInset->testId() == "HelloWorld.MainPanelsInset");
  LOKA_VERIFY(mainPanelsInset->nextInComposition == 0);
  LOKA_VERIFY(mainPanelsInset->asNestable()->childrenHead() == mainPanels);
  LOKA_VERIFY(mainPanels->nextInComposition == 0);
  LOKA_VERIFY(mainPanels->props.effectiveAxis() == loka::app::STACK_AXIS_ROW);
  loka::app::scene::Node *leftPanel = mainPanels->childrenHead();
  LOKA_VERIFY(leftPanel != 0);
  loka::app::scene::Node *rightPanel = leftPanel->nextInComposition;
  LOKA_VERIFY(rightPanel != 0);
  LOKA_VERIFY(rightPanel->nextInComposition == 0);
  const loka::core::Frame wideDefault(50, 50, 420, 330);
  driveNativeFrameAndLayout(*window, platform, wideDefault);
  LOKA_VERIFY(window->contentMargin() == loka::app::layout::LayoutGap::of(0));
  loka::app::BoxNode *inset = mainPanelsInset->asBoxNode();
  LOKA_VERIFY(inset && inset->props.padding == loka::app::StandardWindowMargin());
  LOKA_VERIFY(inset->props.padding.resolve(0, 9) == 9);
  LOKA_VERIFY(platform.title.x == 9 && platform.title.y == 9);
  LOKA_VERIFY(loka::dsl::testing::SceneTestAccess::rootNode(*window->scene())->asNestable()->childrenHead()
              == mainPanelsScroll);
  LOKA_VERIFY(mainPanelsScroll->nextInComposition == 0);

  LOKA_VERIFY(mainPanels->props.effectiveAxis() == loka::app::STACK_AXIS_ROW);
  LOKA_VERIFY(platform.lastOnChangeFlags() == loka::app::scene::NODE_DIRTY_LAYOUT);
  const unsigned long wideLayoutCount = platform.onChangeCallCount();

  const loka::core::Frame narrowFrame(50, 50, 399, 330);
  loka::app::testing::WindowTestAccess::storeNativeFrame(*window, narrowFrame);
  LOKA_VERIFY(findMainPanelsScroll(*window->scene()) == mainPanelsScroll);
  LOKA_VERIFY(findMainPanels(*window->scene()) == mainPanels);
  LOKA_VERIFY(mainPanelsScroll->childrenHead() == mainPanelsInset);
  LOKA_VERIFY(mainPanelsInset->asNestable()->childrenHead() == mainPanels);
  LOKA_VERIFY(mainPanels->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN);
  LOKA_VERIFY(mainPanels->childrenHead() == leftPanel);
  LOKA_VERIFY(leftPanel->nextInComposition == rightPanel);
  // One platform notification per flip: the retained Stack's LAYOUT dirt.
  // The initial helper drives its own layout call explicitly; these flips
  // publish only the frame so this count isolates State-driven delivery.
  LOKA_VERIFY(platform.onChangeCallCount() == wideLayoutCount + 1);
  LOKA_VERIFY(platform.lastOnChangeFlags() == loka::app::scene::NODE_DIRTY_LAYOUT);

  loka::app::testing::WindowTestAccess::storeNativeFrame(*window, wideDefault);
  LOKA_VERIFY(findMainPanelsScroll(*window->scene()) == mainPanelsScroll);
  LOKA_VERIFY(findMainPanels(*window->scene()) == mainPanels);
  LOKA_VERIFY(mainPanelsScroll->childrenHead() == mainPanelsInset);
  LOKA_VERIFY(mainPanelsInset->asNestable()->childrenHead() == mainPanels);
  LOKA_VERIFY(mainPanels->props.effectiveAxis() == loka::app::STACK_AXIS_ROW);
  LOKA_VERIFY(mainPanels->childrenHead() == leftPanel);
  LOKA_VERIFY(leftPanel->nextInComposition == rightPanel);
  LOKA_VERIFY(platform.onChangeCallCount() == wideLayoutCount + 2);
  LOKA_VERIFY(platform.lastOnChangeFlags() == loka::app::scene::NODE_DIRTY_LAYOUT);

  delete window;
}

namespace
{
  class NarrowMountMainNode : public helloworld::MainNode
  {
  public:
    explicit NarrowMountMainNode(const helloworld::MainProps &props)
        : helloworld::MainNode(props) {}

    virtual void composeNode(loka::app::scene::NodeComposition &composition)
    {
      helloworld::MainNode::composeNode(composition);
      // Inspect the declaration before a late immediate watch could trigger
      // another composition and hide the first frame's axis.
      loka::app::scene::NodeDefinitionBase *scroll = composition.root();
      LOKA_VERIFY(scroll != 0);
      const std::string scrollId = scroll->testIdValue();
      LOKA_VERIFY(scrollId == "HelloWorld.MainPanelsScroll");
      loka::app::scene::INestableDefinition *scrollChildren = scroll->asNestableDefinition();
      LOKA_VERIFY(scrollChildren != 0);
      loka::app::scene::NodeDefinitionBase *inset = scrollChildren->childrenHead();
      LOKA_VERIFY(inset != 0);
      const std::string insetId = inset->testIdValue();
      LOKA_VERIFY(insetId == "HelloWorld.MainPanelsInset");
      loka::app::scene::INestableDefinition *insetChildren = inset->asNestableDefinition();
      LOKA_VERIFY(insetChildren != 0);
      loka::app::scene::NodeDefinitionBase *panels = insetChildren->childrenHead();
      LOKA_VERIFY(panels != 0);
      const std::string panelsId = panels->testIdValue();
      LOKA_VERIFY(panelsId == "HelloWorld.MainPanels");
      const loka::app::scene::PropsBase *props = panels->propsBase();
      LOKA_VERIFY(props && props->propsTypeId() == loka::app::StackProps::staticTypeId());
      LOKA_VERIFY(static_cast<const loka::app::StackProps *>(props)->effectiveAxis() ==
                  loka::app::STACK_AXIS_COLUMN);
    }
  };
} // namespace

void testHelloWorldNarrowMountComposesColumnFirst()
{
  NullPlatformContext context;
  NullScenePlatformController platform;
  loka::core::OwnedDef<loka::app::scene::NodeDefinitionBase> root(
      loka::app::scene::BoundaryDefinition<helloworld::MainProps, NarrowMountMainNode>().clone());
  const bool rootCloned = root.isSet();
  LOKA_VERIFY(rootCloned);
  WindowProps props;
  props.frame(50, 50, 399, 330);
  NullWindow window(&context, props, &platform);
  WindowAdmissionTestApp admission(window);
  // Publish before attach; inspect the first tree without a resize/layout pass.
  loka::app::testing::WindowTestAccess::storeNativeFrame(
      window, loka::core::Frame(50, 50, 399, 330));
  window.sceneManager()->commitTransaction(0, new loka::app::scene::Scene(root.take()));
  admission.flush();
  LOKA_VERIFY(findMainPanels(*window.scene())->props.effectiveAxis() ==
              loka::app::STACK_AXIS_COLUMN);
}

void testHelloWorldDerivedTextSeatsCoverInputsAndActions()
{
  using loka::core::String;
  using loka::core::StateTrackerGuard;
  using loka::dsl::testing::SceneTestAccess;

  NullScenePlatformController platform;
  loka::app::scene::Scene scene((loka::app::scene::Boundary<helloworld::MainNode>(helloworld::MainProps())));
  scene.mount(&platform);
  SceneTestAccess::updateAttached(scene, true);
  loka::app::scene::Node *root = SceneTestAccess::rootNode(scene);
  loka::app::TextNode *summary = findByTestId(root, "HelloWorld.LeftPanel.ActionSummary")->asTextNode();
  loka::app::TextNode *fruit = findByTestId(root, "HelloWorld.RightPanel.FruitMessage")->asTextNode();
  loka::app::scene::Node *bmiRoot = findByTestId(root, "HelloWorld.Bmi");
  int bmiTextSkip = 3;
  int heightSkip = 0;
  int weightSkip = 1;
  loka::app::TextNode *bmi = static_cast<loka::app::TextNode *>(
      findDescendantByKind(bmiRoot, loka::app::scene::NODE_KIND_TEXT, bmiTextSkip)->asTextNode());
  loka::app::PopupMenuNode *fruitPicker =
      findByTestId(root, "HelloWorld.RightPanel.FruitPopup")->asPopupMenuNode();
  loka::app::EditTextNode *height =
      static_cast<loka::app::EditTextNode *>(findDescendantByKind(
          bmiRoot, loka::app::scene::NODE_KIND_EDIT_TEXT, heightSkip)->asEditTextNode());
  loka::app::EditTextNode *weight =
      static_cast<loka::app::EditTextNode *>(findDescendantByKind(
          bmiRoot, loka::app::scene::NODE_KIND_EDIT_TEXT, weightSkip)->asEditTextNode());
  loka::app::ButtonNode *probe =
      findByTestId(root, "HelloWorld.LeftPanel.ProbeButton")->asButtonNode();
  loka::app::ButtonNode *toggle =
      findByTestId(root, "HelloWorld.LeftPanel.ToggleEnabledButton")->asButtonNode();
  LOKA_VERIFY(summary && fruit && bmi && fruitPicker && height && weight && probe && toggle);
  LOKA_VERIFY(summary->props.text_->get().equals(String::Literal("Enabled: yes / clicks: 0")));
  LOKA_VERIFY(fruit->props.text_->get().equals(String::Literal("You chose Apple.")));
  LOKA_VERIFY(bmi->props.text_->get().equals(String::Literal("BMI: 20.76")));

  TextObserver observer;
  bmi->props.text_->bind(&TextObserver::changed, &observer, false);
  loka::app::scene::BoundaryNode *owner = SceneTestAccess::rootBoundary(scene);
  {
    StateTrackerGuard guard(owner->tracker());
    height->props.text_.set(String::Literal("invalid"));
  }
  LOKA_VERIFY(bmi->props.text_->get().equals(String::Literal("BMI: --")) && observer.calls == 1);
  {
    StateTrackerGuard guard(owner->tracker());
    height->props.text_.set(String::Literal("170.0"));
  }
  LOKA_VERIFY(bmi->props.text_->get().equals(String::Literal("BMI: 20.76")) && observer.calls == 2);
  {
    StateTrackerGuard guard(owner->tracker());
    height->props.text_.set(String::Literal("0"));
  }
  LOKA_VERIFY(bmi->props.text_->get().equals(String::Literal("BMI: --")) && observer.calls == 3);
  {
    StateTrackerGuard guard(owner->tracker());
    height->props.text_.set(String::Literal("100"));
    weight->props.text_.set(String::Literal("20.6249"));
  }
  LOKA_VERIFY(bmi->props.text_->get().equals(String::Literal("BMI: 20.62")));
  const int callsBeforeEqualOutput = observer.calls;
  {
    StateTrackerGuard guard(owner->tracker());
    weight->props.text_.set(String::Literal("20.625"));
  }
  LOKA_VERIFY(bmi->props.text_->get().equals(String::Literal("BMI: 20.62")));
  LOKA_VERIFY(observer.calls == callsBeforeEqualOutput);
  {
    StateTrackerGuard guard(owner->tracker());
    weight->props.text_.set(String::Literal("20.635"));
  }
  LOKA_VERIFY(bmi->props.text_->get().equals(String::Literal("BMI: 20.64")));
  LOKA_VERIFY(observer.calls == callsBeforeEqualOutput + 1);
  bmi->props.text_->unbind(&TextObserver::changed, &observer);

  {
    StateTrackerGuard guard(owner->tracker());
    fruitPicker->props.selectedIndex_.set(99);
  }
  LOKA_VERIFY(fruit->props.text_->get().equals(String::Literal("You chose Apple.")));
  {
    StateTrackerGuard guard(owner->tracker());
    fruitPicker->props.selectedIndex_.set(0);
  }
  LOKA_VERIFY(fruit->props.text_->get().equals(String::Literal("You chose Apple.")));
  fruitPicker->props.selectedIndex_.set(2, true);
  LOKA_VERIFY(fruit->props.text_->get().equals(String::Literal("You chose Cherry.")));
  probe->props.onClick_->emit();
  LOKA_VERIFY(summary->props.text_->get().equals(String::Literal("Enabled: yes / clicks: 1")));
  toggle->props.onClick_->emit();
  LOKA_VERIFY(summary->props.text_->get().equals(String::Literal("Enabled: no / clicks: 1")));
  // The raw emitter is not enabled-aware (the native control gates disabled
  // clicks), so the disabled state is checked on the Button's input instead
  // of by emitting through it.
  LOKA_VERIFY(probe->props.enabled_ && !probe->props.enabled_->get());
  SceneTestAccess::unmount(scene);
}

void testHelloWorldShuffleRotatesSlotTitles()
{
  using namespace loka::app;
  using loka::core::String;
  NullScenePlatformController platform;
  scene::Scene scene((scene::Boundary<helloworld::MainNode>(helloworld::MainProps(0x13579BDFUL))));
  scene.mount(&platform);
  loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
  const MenuBarDefinition *bar = scene.menuBar();
  LOKA_VERIFY(bar != 0);
  LOKA_VERIFY(bar->menusCount() == 5);
  const char *menuTitles[] = {"", "View", "File", "Special", "Random"};
  for (int i = 1; i < 5; ++i)
    LOKA_VERIFY(bar->menuAt(i)->title.equals(String::Literal(menuTitles[i])));
  const MenuDefinition *random = bar->menuAt(4);
  LOKA_VERIFY(random->itemsCount() == 8);
  const MenuItemDefinition *shuffle = random->itemsHead();
  LOKA_VERIFY(shuffle->title.equals(String::Literal("Shuffle")) && shuffle->onClickState);
  helloworld::MainNode *main = static_cast<helloworld::MainNode *>(
      loka::dsl::testing::SceneTestAccess::rootNode(scene));
  const MenuItemDefinition *item = shuffle->nextInComposition->nextInComposition;
  String before[6];
  for (int i = 0; i < 6; ++i, item = item->nextInComposition)
  {
    LOKA_VERIFY(item && item->titleState == helloworld::testing::MainAccess::randomTitle(*main, i));
    before[i] = item->titleState->get();
    LOKA_VERIFY(before[i].equals(String::Literal("Random ") + String::FromInt(i + 1)));
  }
  const int expected[2][6] = {{3, 5, 4, 1, 6, 2}, {3, 4, 6, 1, 5, 2}};
  for (int pass = 0; pass < 2; ++pass)
  {
    shuffle->onClickState->emit();
    LOKA_VERIFY(scene.menuBar() == bar);
    item = shuffle->nextInComposition->nextInComposition;
    bool changed = false;
    int counts[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 6; ++i, item = item->nextInComposition)
    {
      LOKA_VERIFY(item->titleState == helloworld::testing::MainAccess::randomTitle(*main, i));
      const String title = item->titleState->get();
      LOKA_VERIFY(title.equals(String::Literal("Random ") + String::FromInt(expected[pass][i])));
      if (!title.equals(before[i])) changed = true;
      for (int j = 0; j < 6; ++j)
        if (title.equals(before[j])) ++counts[j];
    }
    LOKA_VERIFY(changed);
    for (int i = 0; i < 6; ++i) LOKA_VERIFY(counts[i] == 1);
  }
}
