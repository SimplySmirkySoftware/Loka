#include "ToolbarTests.hpp"
#include "app/nodes/controls/Toolbar.hpp"
#include "app/style/StyleVocab.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/context/NullTextContext.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "support/TestVerify.hpp"
#include <map>
#include <string>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl::testing;
  using namespace loka::dsl;
  using namespace loka::core;

  /** Records offers at the existing platform door, retaining real Null leaves. */
  class ToolbarPlatform : public NullScenePlatformController
  {
  public:
    std::map<std::string, LayoutState> offers;
    virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
    {
      return false;
    }
    virtual bool prepareProjectedLayout(Node *node, LayoutState &state)
    {
      if (!node->testId().empty())
        this->offers[node->testId()] = state;
      return NullScenePlatformController::prepareProjectedLayout(node, state);
    }
  };
  LayoutState toolbarOffer(short width = 360)
  {
    LayoutState state;
    state.width = width;
    state.height = 100;
    state.lineHeight = 20;
    return state;
  }
  template <class T> T *lookup(Scene &scene, const char *id)
  {
    T *node = 0;
    FlowError error;
    LOKA_VERIFY(LookupNodeById<T>(&scene, id, node, error) == FLOW_STEP_SUCCEEDED);
    LOKA_VERIFY(node != 0);
    return node;
  }
  void sameSeat(const LayoutState &a, const LayoutState &b)
  {
    LOKA_VERIFY(a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height);
  }
  void settle(Scene &scene)
  {
    if (scene.hasPendingInvalidation())
      LOKA_VERIFY(scene.flushInvalidation());
  }
  void countClick(void *value)
  {
    ++*static_cast<int *>(value);
  }
} // namespace

void testToolbarTutorialStateAndSeats()
{
  ToolbarPlatform platform;
  MutableState<bool> canAdd(false);
  MutableState<String> title(String::Literal("Items: 3"));
  EmitterState add;
  int clicks = 0;
  add.bind(&countClick, &clicks, false);
  Scene scene((Toolbar().TEST_ID("bar") << ToolbarItem("Add item", &add).enabled(&canAdd).TEST_ID("add")
                                        << ToolbarItem::label(&title).TEST_ID("label")
                                        << ToolbarItem("Show/Hide map").TEST_ID("toggle"))
                  .clone());
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  Node *root = SceneTestAccess::rootNode(scene);
  ButtonNode *button = lookup<ButtonNode>(scene, "add");
  TextNode *text = lookup<TextNode>(scene, "label");
  LOKA_VERIFY(button->props.enabled_ && !button->props.enabled_->get());
  LOKA_VERIFY(button->props.onClick_ == &add);
  LOKA_VERIFY(lookup<ButtonNode>(scene, "toggle")->props.onClick_ == 0);
  const int height = platform.projectLayoutForTesting(root, toolbarOffer());
  const LayoutState first = platform.offers["add"], label = platform.offers["label"], last = platform.offers["toggle"];
  LOKA_VERIFY(first.width == 80 && last.width == 120);
  LOKA_VERIFY(first.x == 0 && label.x == 88 && label.width == 144 && last.x == 240);
  NullTextContext *context = static_cast<NullTextContext *>(text->getContext());
  LOKA_VERIFY(context != 0);
  LOKA_VERIFY(context->commitPresented(title.get(), platform.paintScope()));
  const char *values[] = {"Items: 100 (full)", "Items: 3", "x", ""};
  for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
  {
    const unsigned long changes = platform.onChangeCallCount();
    {
      StateTrackerGuard transaction(root->asBoundary()->tracker());
      title.set(String::Literal(values[i]));
    }
    settle(scene);
    LOKA_VERIFY(platform.onChangeCallCount() > changes);
    LOKA_VERIFY((platform.lastOnChangeFlags() & NODE_DIRTY_PROPS) != 0);
    LOKA_VERIFY((platform.lastOnChangeFlags() & NODE_DIRTY_LAYOUT) == 0);
    LOKA_VERIFY(platform.projectLayoutForTesting(root, toolbarOffer()) == height);
    sameSeat(first, platform.offers["add"]);
    sameSeat(label, platform.offers["label"]);
    sameSeat(last, platform.offers["toggle"]);
    const Frame &line = loka::app::testing::NullTextMeasurementAccess::line(context->measurement(), 0);
    LOKA_VERIFY(line.x == (label.width - line.width) / 2);
    LOKA_VERIFY(context->commitPresented(title.get(), platform.paintScope()));
  }
  {
    StateTrackerGuard transaction(root->asBoundary()->tracker());
    canAdd.set(true);
  }
  settle(scene);
  LOKA_VERIFY(button->props.enabled_->get());
  Scene *out = 0;
  FlowError error;
  LOKA_VERIFY(ClickButton("add").run(&scene, out, error) == FLOW_STEP_SUCCEEDED);
  LOKA_VERIFY(clicks == 1);
  SceneTestAccess::unmount(scene);
  add.unbind(&countClick, &clicks);
}

void testToolbarTitleHeightOffers()
{
  ToolbarPlatform platform;
  Scene scene(
      (Toolbar() << ToolbarItem("A").TEST_ID("button") << (ToolbarItem::label("Title").TEST_ID("label") + Title))
          .clone());
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  const int bottom = platform.projectLayoutForTesting(SceneTestAccess::rootNode(scene), toolbarOffer());
  LOKA_VERIFY(platform.offers["label"].height == 18);
  LOKA_VERIFY(platform.offers["label"].y == 0);
  LOKA_VERIFY(platform.offers["button"].height == 10);
  LOKA_VERIFY(platform.offers["button"].y == 4);
  // The real Null Button grows past its table offer to its native fallback 32.
  LOKA_VERIFY(bottom == 4 + 32);
  SceneTestAccess::unmount(scene);
}

void testToolbarShownSeatsAndGaps()
{
  ToolbarPlatform platform;
  MutableState<bool> shown(true);
  Scene scene((Toolbar().TEST_ID("bar") << ToolbarItem("A").TEST_ID("first")
                                        << ToolbarItem("middle").shown(shown).TEST_ID("middle")
                                        << ToolbarItem("B").TEST_ID("last"))
                  .clone());
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  Node *root = SceneTestAccess::rootNode(scene);
  platform.projectLayoutForTesting(root, toolbarOffer());
  const LayoutState original = platform.offers["last"];
  LOKA_VERIFY(original.x == 24 + 8 + 64 + 8);
  for (int pass = 0; pass < 2; ++pass)
  {
    {
      StateTrackerGuard transaction(root->asBoundary()->tracker());
      shown.set(pass != 0);
    }
    settle(scene);
    // Existing Null onChange retains the last explicit offer and lays out again.
    LOKA_VERIFY(platform.offers["last"].x == (pass ? original.x : 24 + 8));
    platform.projectLayoutForTesting(root, toolbarOffer());
    LOKA_VERIFY(platform.offers["last"].x == (pass ? original.x : 24 + 8));
    if (pass)
      sameSeat(original, platform.offers["last"]);
  }
  SceneTestAccess::unmount(scene);
}

void testToolbarSpaceAllocation()
{
  ToolbarPlatform platform;
  Scene scene((Toolbar().TEST_ID("bar") << ToolbarItem::flexibleSpace() << ToolbarItem::label("x").TEST_ID("label")
                                        << ToolbarItem::flexibleSpace() << ToolbarItem::fixedSpace(17)
                                        << ToolbarItem("A").TEST_ID("last"))
                  .clone());
  LOKA_VERIFY(scene.mount(&platform));
  SceneTestAccess::updateAttached(scene, true);
  StackNode *row = lookup<Node>(scene, "bar")->asStackNode();
  LOKA_VERIFY(row != 0);
  platform.projectLayoutForTesting(SceneTestAccess::rootNode(scene), toolbarOffer(100));
  // C=17+24+4*8=73, L=1, R=26; shares 9,9,8, with label floor added.
  const short widths[] = {9, 10, 8, 17, 24};
  layout::RowWidthConsultation seats(row->childrenHead(),
                                     row->childrenCount(),
                                     100,
                                     8,
                                     layout::RailSpacing(12, 8, 9),
                                     row->props.rowUndeclaredWidth_,
                                     &platform);
  unsigned index = 0;
  for (Node *child = row->childrenHead(); child; child = child->nextInComposition)
    LOKA_VERIFY(seats.next(child).width() == widths[index++]);
  LOKA_VERIFY(index == 5);
  LOKA_VERIFY(platform.offers["label"].x == 17 && platform.offers["label"].width == 10);
  LOKA_VERIFY(platform.offers["last"].x == 76);
  SceneTestAccess::unmount(scene);
}
