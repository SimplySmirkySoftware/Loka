#include "SimpleViewerResponsiveTests.hpp"

#include "../example/SimpleViewer/src/MyAppConfig.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/Window.hpp"
#include "app/layout/RowLayout.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/scene/Scene.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "platform/null/NullWindow.hpp"
#include "support/TestVerify.hpp"
#include "testing/app/WindowTestAccess.hpp"
#include "testing/scene/SceneTestFlow.hpp"

namespace simpleviewer { namespace testing {
class MainAccess
{
public:
  static void setNavMode(simpleviewer::MainNode &node, simpleviewer::NavMode mode)
  {
    loka::core::StateTrackerGuard guard(node.tracker());
    node.navMode_.set(mode);
  }

  static loka::core::State<simpleviewer::DisplayMode> *displayModeState(
      simpleviewer::MainNode &node)
  {
    return node.displayMode_.state();
  }

  static loka::core::EmitterState *openDialogEvent(SimpleViewerAppConfig &config)
  {
    return &config.openDialogEvent_;
  }

  static loka::core::EmitterState *fitEvent(simpleviewer::MainNode &node)
  {
    return &node.fitToWindowEvent_;
  }

  static loka::core::EmitterState *actualEvent(simpleviewer::MainNode &node)
  {
    return &node.actualEvent_;
  }

  static loka::core::EmitterState *actualScrollEvent(simpleviewer::MainNode &node)
  {
    return &node.actualScrollEvent_;
  }

  static void setDisplayMode(simpleviewer::MainNode &node,
                             simpleviewer::DisplayMode mode)
  {
    loka::core::StateTrackerGuard guard(node.tracker());
    node.displayMode_.set(mode);
  }

  static void setLoadedImage(simpleviewer::MainNode &node,
                             const loka::core::resource::Image &image)
  {
    node.commitLoadedImage(image);
  }
};

} } // namespace simpleviewer::testing
using simpleviewer::testing::MainAccess;

namespace
{
  const int kRowGap = 8;

  class NullImageGeometryContext : public loka::app::scene::NodeContext
  {
  public:
    explicit NullImageGeometryContext(loka::app::ImageViewNode *node)
        : node_(node),
          geometry_()
    {
    }

    virtual short layout(loka::app::scene::IPlatformController *,
                         loka::app::scene::LayoutState &state)
    {
      int width = state.width;
      int height = state.height;
      if (this->node_ && this->node_->props.hasAttr_ &&
          this->node_->props.attr_.sizePolicyValue_ == loka::app::IMAGE_VIEW_SIZE_INTRINSIC &&
          this->node_->props.image_)
      {
        const loka::core::resource::Image image = this->node_->props.image_->get();
        if (image.width() > 0)
        {
          width = image.width();
        }
        if (image.height() > 0)
        {
          height = image.height();
        }
      }
      this->geometry_ = state;
      this->geometry_.width = static_cast<short>(width);
      this->geometry_.height = static_cast<short>(height);
      state.width = static_cast<short>(width);
      state.height = static_cast<short>(height);
      return static_cast<short>(state.y + height);
    }

    const loka::app::scene::LayoutState &geometry() const
    {
      return this->geometry_;
    }

  private:
    loka::app::ImageViewNode *node_;
    loka::app::scene::LayoutState geometry_;
  };

  class NullImageGeometryHandler : public loka::app::scene::IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<loka::app::ImageViewNode>();
    }

    virtual loka::app::scene::NodeContext *ensureContext(
        loka::app::scene::Node *node,
        loka::app::scene::IPlatformController *controller,
        const loka::app::scene::LayoutState &state)
    {
      (void)state;
      loka::app::ImageViewNode *image = node ? node->asImageViewNode() : 0;
      if (!image || !controller)
      {
        return 0;
      }
      if (!image->getContext())
      {
        image->setContext(new NullImageGeometryContext(image));
      }
      return image->getContext();
    }
  };

  class NullButtonGeometryContext : public loka::app::scene::NodeContext
  {
  public:
    NullButtonGeometryContext()
        : geometry_()
    {
    }

    virtual short layout(loka::app::scene::IPlatformController *,
                         loka::app::scene::LayoutState &state)
    {
      this->geometry_ = state;
      return static_cast<short>(state.y + state.height);
    }

    const loka::app::scene::LayoutState &geometry() const
    {
      return this->geometry_;
    }

  private:
    loka::app::scene::LayoutState geometry_;
  };

  class NullButtonGeometryHandler : public loka::app::scene::IPlatformNodeHandler
  {
  public:
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<loka::app::ButtonNode>();
    }

    virtual loka::app::scene::NodeContext *ensureContext(
        loka::app::scene::Node *node,
        loka::app::scene::IPlatformController *controller,
        const loka::app::scene::LayoutState &state)
    {
      (void)state;
      loka::app::ButtonNode *button = node ? node->asButtonNode() : 0;
      if (!button || !controller)
      {
        return 0;
      }
      if (!button->getContext())
      {
        button->setContext(new NullButtonGeometryContext());
      }
      return button->getContext();
    }
  };

  loka::app::scene::Node *findNode(loka::app::scene::Node *node,
                                    const char *testId)
  {
    if (!node)
    {
      return 0;
    }
    if (node->testId() == testId)
    {
      return node;
    }
    loka::app::scene::INestable *nestable = node->asNestable();
    for (loka::app::scene::Node *child = nestable ? nestable->childrenHead() : 0;
         child;
         child = child->nextInComposition)
    {
      loka::app::scene::Node *found = findNode(child, testId);
      if (found)
      {
        return found;
      }
    }
    return 0;
  }

  void flushScene(loka::app::scene::Scene &scene)
  {
    if (scene.hasPendingInvalidation())
    {
      LOKA_VERIFY(scene.flushInvalidation());
    }
  }

  const loka::app::MenuDefinition *viewMenu(const loka::app::MenuBarDefinition *bar)
  {
    if (!bar || bar->menusCount() != 3)
    {
      return 0;
    }
    return bar->menuAt(2);
  }

  void verifyCheckedMode(const loka::app::MenuDefinition *view,
                         simpleviewer::DisplayMode mode)
  {
    LOKA_VERIFY(view != 0);
    LOKA_VERIFY(view->itemsCount() == 3);
    const loka::app::MenuItemDefinition *item = view->itemsHead();
    LOKA_VERIFY(item != 0);
    LOKA_VERIFY(item->isCheckedInitial() == (mode == simpleviewer::DISPLAY_FIT));
    item = item->nextInComposition;
    LOKA_VERIFY(item != 0);
    LOKA_VERIFY(item->isCheckedInitial() == (mode == simpleviewer::DISPLAY_ACTUAL));
    item = item->nextInComposition;
    LOKA_VERIFY(item != 0);
    LOKA_VERIFY(item->isCheckedInitial() == (mode == simpleviewer::DISPLAY_ACTUAL_SCROLL));
  }

  struct RowSeatRecorder
  {
    RowSeatRecorder()
        : navCalls(0),
          contentCalls(0),
          dialogCalls(0),
          navState(),
          contentState(),
          dialogState(),
          navNode(0),
          contentNode(0),
          dialogNode(0)
    {
    }

    int navCalls;
    int contentCalls;
    int dialogCalls;
    loka::app::scene::LayoutState navState;
    loka::app::scene::LayoutState contentState;
    loka::app::scene::LayoutState dialogState;
    loka::app::scene::Node *navNode;
    loka::app::scene::Node *contentNode;
    loka::app::scene::Node *dialogNode;
  };

  int recordRowSeat(void *context,
                    loka::app::scene::Node *node,
                    const loka::app::scene::LayoutState &state)
  {
    RowSeatRecorder *record = static_cast<RowSeatRecorder *>(context);
    if (node == record->navNode)
    {
      ++record->navCalls;
      record->navState = state;
    }
    if (node == record->contentNode)
    {
      ++record->contentCalls;
      record->contentState = state;
    }
    if (record->dialogNode && node == record->dialogNode)
    {
      ++record->dialogCalls;
      record->dialogState = state;
    }
    return state.y + state.height;
  }

  void recordRootSeats(loka::app::StackNode *rootRow,
                       RowSeatRecorder &record)
  {
    LOKA_VERIFY(rootRow != 0);
    record.navNode = rootRow->childrenHead();
    LOKA_VERIFY(record.navNode != 0);
    record.contentNode = record.navNode->nextInComposition;
    LOKA_VERIFY(record.contentNode != 0);
    // The root Row's third child is the dialog branch (an empty Fragment
    // while closed, the OpenFileDialog while open); it is recorded so the
    // geometry pin can check it never takes a seat (#588).
    record.dialogNode = record.contentNode->nextInComposition;
    LOKA_VERIFY(record.dialogNode != 0);
    LOKA_VERIFY(record.dialogNode->nextInComposition == 0);

    loka::app::scene::LayoutState state;
    state.width = 600;
    state.height = 400;
    state.spacing = kRowGap;
    loka::app::layout::RowLayoutMetrics metrics;
    metrics.gap = kRowGap;
    loka::app::layout::computeRowLayoutResultY(rootRow,
                                               state,
                                               metrics,
                                               0, // Synthetic allocation probe has no rail source.
                                               &record,
                                               &recordRowSeat,
                                               metrics.gap,
                                               0);
  }

  struct SimpleViewerHarness
  {
    SimpleViewerHarness()
        : platformContext(),
          config(&platformContext),
          imageGeometryHandler(),
          buttonGeometryHandler(),
          platform(),
          scene(0)
    {
      LOKA_VERIFY(this->platform.registerNodeHandler(&this->imageGeometryHandler));
      LOKA_VERIFY(this->platform.registerNodeHandler(&this->buttonGeometryHandler));
      simpleviewer::MainProps props;
      props.platformContext(&platformContext)
          .openDialogEvent(MainAccess::openDialogEvent(config));
      loka::app::scene::NodeDefinitionBase *rootDefinition =
          loka::app::scene::Boundary<simpleviewer::MainNode>(props).clone();
      LOKA_VERIFY(rootDefinition != 0);
      this->scene = new loka::app::scene::Scene(rootDefinition);
      LOKA_VERIFY(this->scene != 0);
      this->scene->mount(&this->platform);
      loka::dsl::testing::SceneTestAccess::updateAttached(*this->scene, true);
    }

    ~SimpleViewerHarness()
    {
      if (this->scene)
      {
        loka::dsl::testing::SceneTestAccess::unmount(*this->scene);
        delete this->scene;
      }
    }

    simpleviewer::MainNode *mainNode() const
    {
      return static_cast<simpleviewer::MainNode *>(
          loka::dsl::testing::SceneTestAccess::rootNode(*this->scene));
    }

    NullPlatformContext platformContext;
    SimpleViewerAppConfig config;
    NullImageGeometryHandler imageGeometryHandler;
    NullButtonGeometryHandler buttonGeometryHandler;
    NullScenePlatformController platform;
    loka::app::scene::Scene *scene;
  };
} // namespace

void testSimpleViewerNavSeatsFollowModeAndRetainContentImage()
{
  SimpleViewerHarness harness;
  simpleviewer::MainNode *main = harness.mainNode();
  LOKA_VERIFY(main != 0);
  loka::app::StackNode *rootRow = static_cast<loka::app::StackNode *>(
      findNode(main, "SimpleViewer.Root"));
  LOKA_VERIFY(rootRow != 0);
  loka::app::ImageViewNode *retainedImage = static_cast<loka::app::ImageViewNode *>(
      findNode(main, "SimpleViewer.Image"));
  LOKA_VERIFY(retainedImage != 0);

  RowSeatRecorder wide;
  recordRootSeats(rootRow, wide);
  LOKA_VERIFY(wide.navCalls == 1);
  LOKA_VERIFY(wide.contentCalls == 1);
  LOKA_VERIFY(wide.navState.width == 200);
  LOKA_VERIFY(wide.contentState.x == 200 + kRowGap);

  MainAccess::setNavMode(*main, simpleviewer::NAV_NARROW_CLOSED);
  flushScene(*harness.scene);
  LOKA_VERIFY(findNode(main, "SimpleViewer.NavPane") == 0);
  LOKA_VERIFY(findNode(main, "SimpleViewer.NavToggle") != 0);
  LOKA_VERIFY(findNode(main, "SimpleViewer.Image") == retainedImage);
  rootRow = static_cast<loka::app::StackNode *>(findNode(main, "SimpleViewer.Root"));
  RowSeatRecorder closed;
  recordRootSeats(rootRow, closed);
  LOKA_VERIFY(closed.navState.width == 0);
  LOKA_VERIFY(closed.contentState.x == 0);
  LOKA_VERIFY(closed.contentState.width == 600);

  MainAccess::setNavMode(*main, simpleviewer::NAV_NARROW_OPEN);
  flushScene(*harness.scene);
  LOKA_VERIFY(findNode(main, "SimpleViewer.NavPane") != 0);
  LOKA_VERIFY(findNode(main, "SimpleViewer.NavToggle") != 0);
  LOKA_VERIFY(findNode(main, "SimpleViewer.Image") == retainedImage);
  rootRow = static_cast<loka::app::StackNode *>(findNode(main, "SimpleViewer.Root"));
  RowSeatRecorder open;
  recordRootSeats(rootRow, open);
  LOKA_VERIFY(open.navState.width == 200);
  LOKA_VERIFY(open.contentState.x == 200 + kRowGap);
}

void testSimpleViewerOpenPaneToggleStaysInsideSubNavWidthViewport()
{
  SimpleViewerHarness harness;
  simpleviewer::MainNode *main = harness.mainNode();
  LOKA_VERIFY(main != 0);
  MainAccess::setNavMode(*main, simpleviewer::NAV_NARROW_OPEN);
  flushScene(*harness.scene);

  loka::app::StackNode *root = static_cast<loka::app::StackNode *>(
      findNode(main, "SimpleViewer.Root"));
  LOKA_VERIFY(root != 0);
  loka::app::scene::LayoutState viewport;
  viewport.x = 0;
  viewport.y = 0;
  viewport.width = 120;
  viewport.height = 280;
  viewport.lineHeight = 10;
  viewport.spacing = kRowGap;
  harness.platform.projectLayoutForTesting(root, viewport);

  loka::app::ButtonNode *toggle = static_cast<loka::app::ButtonNode *>(
      findNode(main, "SimpleViewer.NavToggle"));
  LOKA_VERIFY(toggle != 0);
  NullButtonGeometryContext *toggleContext =
      static_cast<NullButtonGeometryContext *>(toggle->getContext());
  LOKA_VERIFY(toggleContext != 0);
  const loka::app::scene::LayoutState &geometry = toggleContext->geometry();
  LOKA_VERIFY(geometry.width > 0);
  LOKA_VERIFY(geometry.x >= viewport.x);
  LOKA_VERIFY(geometry.x + geometry.width <= viewport.x + viewport.width);
}

void testSimpleViewerDisplayArmsAndMenuChecksFollowOwnedMode()
{
  SimpleViewerHarness harness;
  simpleviewer::MainNode *main = harness.mainNode();
  LOKA_VERIFY(main != 0);
  loka::app::ImageViewNode *image = static_cast<loka::app::ImageViewNode *>(
      findNode(main, "SimpleViewer.Image"));
  LOKA_VERIFY(image != 0);
  LOKA_VERIFY(image->props.attr_.sizePolicyValue_ == loka::app::IMAGE_VIEW_SIZE_FILL_PARENT);
  LOKA_VERIFY(findNode(main, "SimpleViewer.ActualScroll") == 0);
  verifyCheckedMode(viewMenu(harness.scene->menuBar()),
                    simpleviewer::DISPLAY_FIT);

  MainAccess::setDisplayMode(*harness.mainNode(),
                                         simpleviewer::DISPLAY_ACTUAL);
  flushScene(*harness.scene);
  image = static_cast<loka::app::ImageViewNode *>(findNode(main, "SimpleViewer.Image"));
  LOKA_VERIFY(image != 0);
  LOKA_VERIFY(image->props.attr_.sizePolicyValue_ == loka::app::IMAGE_VIEW_SIZE_INTRINSIC);
  verifyCheckedMode(viewMenu(harness.scene->menuBar()),
                    simpleviewer::DISPLAY_ACTUAL);

  MainAccess::setDisplayMode(*harness.mainNode(),
                                         simpleviewer::DISPLAY_ACTUAL_SCROLL);
  flushScene(*harness.scene);
  loka::app::ScrollViewNode *scroll = static_cast<loka::app::ScrollViewNode *>(
      findNode(main, "SimpleViewer.ActualScroll"));
  LOKA_VERIFY(scroll != 0);
  image = static_cast<loka::app::ImageViewNode *>(findNode(main, "SimpleViewer.Image"));
  LOKA_VERIFY(image != 0);
  LOKA_VERIFY(scroll->childrenHead() == image);
  LOKA_VERIFY(image->nextInComposition == 0);
  LOKA_VERIFY(image->props.attr_.sizePolicyValue_ == loka::app::IMAGE_VIEW_SIZE_INTRINSIC);
  verifyCheckedMode(viewMenu(harness.scene->menuBar()),
                    simpleviewer::DISPLAY_ACTUAL_SCROLL);
}

void testSimpleViewerDisplayModesProjectExpectedNullGeometry()
{
  SimpleViewerHarness harness;
  simpleviewer::MainNode *main = harness.mainNode();
  LOKA_VERIFY(main != 0);
  const loka::core::resource::Image fixture =
      loka::core::resource::Image::FromNative(reinterpret_cast<void *>(1),
                                              80,
                                              60,
                                              0,
                                              0);
  MainAccess::setLoadedImage(*main, fixture);
  flushScene(*harness.scene);

  loka::app::StackNode *content = static_cast<loka::app::StackNode *>(
      findNode(main, "SimpleViewer.Content"));
  LOKA_VERIFY(content != 0);
  loka::app::scene::LayoutState contentState;
  contentState.x = 30;
  contentState.y = 40;
  contentState.width = 320;
  contentState.height = 240;
  contentState.lineHeight = 10;
  contentState.spacing = 4;

  loka::app::ImageViewNode *image = static_cast<loka::app::ImageViewNode *>(
      findNode(main, "SimpleViewer.Image"));
  LOKA_VERIFY(image != 0);
  harness.platform.projectLayoutForTesting(content, contentState);
  NullImageGeometryContext *imageContext =
      static_cast<NullImageGeometryContext *>(image->getContext());
  LOKA_VERIFY(imageContext != 0);
  const loka::app::scene::LayoutState *geometry = &imageContext->geometry();
  LOKA_VERIFY(geometry->x == 30);
  LOKA_VERIFY(geometry->y == 40);
  LOKA_VERIFY(geometry->width == 320);
  LOKA_VERIFY(geometry->height == 240);

  // The dialog branch is a sibling of nav and content in the root Row (no
  // VStack parks it), and while closed it is an empty Fragment that takes
  // neither a seat nor a gap.
  loka::app::StackNode *root = static_cast<loka::app::StackNode *>(
      findNode(main, "SimpleViewer.Root"));
  LOKA_VERIFY(root != 0);
  LOKA_VERIFY(root->props.axis_ == loka::app::STACK_AXIS_ROW);
  RowSeatRecorder seatsBeforeDialog;
  recordRootSeats(root, seatsBeforeDialog);
  LOKA_VERIFY(seatsBeforeDialog.dialogCalls == 1);
  LOKA_VERIFY(seatsBeforeDialog.dialogState.width == 0);
  LOKA_VERIFY(seatsBeforeDialog.contentState.x + seatsBeforeDialog.contentState.width ==
              seatsBeforeDialog.dialogState.x);
  loka::app::scene::LayoutState rootState;
  rootState.x = 0;
  rootState.y = 0;
  rootState.width = 600;
  rootState.height = 400;
  rootState.lineHeight = 10;
  rootState.spacing = 4;
  harness.platform.projectLayoutForTesting(root, rootState);
  const loka::app::scene::LayoutState fitBeforeDialog = imageContext->geometry();
  LOKA_VERIFY(fitBeforeDialog.height == rootState.height);

  MainAccess::openDialogEvent(harness.config)->emit();
  flushScene(*harness.scene);
  loka::app::OpenFileDialogNode *dialog =
      static_cast<loka::app::OpenFileDialogNode *>(
          findNode(main, "SimpleViewerOpenFileDialog"));
  LOKA_VERIFY(dialog != 0);
  root = static_cast<loka::app::StackNode *>(
      findNode(main, "SimpleViewer.Root"));
  LOKA_VERIFY(root != 0);
  // Materialized: the dialog now lives under the Row's third child, and the
  // Row's consultation still hands it no width and no gap, so the visual
  // siblings' allocation is unchanged.
  RowSeatRecorder seatsWithDialog;
  recordRootSeats(root, seatsWithDialog);
  LOKA_VERIFY(findNode(seatsWithDialog.dialogNode, "SimpleViewerOpenFileDialog") == dialog);
  LOKA_VERIFY(seatsWithDialog.dialogCalls == 1);
  LOKA_VERIFY(seatsWithDialog.dialogState.width == 0);
  LOKA_VERIFY(seatsWithDialog.dialogState.x == seatsBeforeDialog.dialogState.x);
  LOKA_VERIFY(seatsWithDialog.contentState.x == seatsBeforeDialog.contentState.x);
  LOKA_VERIFY(seatsWithDialog.contentState.width ==
              seatsBeforeDialog.contentState.width);
  harness.platform.projectLayoutForTesting(root, rootState);
  geometry = &imageContext->geometry();
  LOKA_VERIFY(geometry->x == fitBeforeDialog.x);
  LOKA_VERIFY(geometry->y == fitBeforeDialog.y);
  LOKA_VERIFY(geometry->width == fitBeforeDialog.width);
  LOKA_VERIFY(geometry->height == fitBeforeDialog.height);
  MainAccess::setDisplayMode(*harness.mainNode(),
                                         simpleviewer::DISPLAY_ACTUAL);
  flushScene(*harness.scene);
  image = static_cast<loka::app::ImageViewNode *>(
      findNode(main, "SimpleViewer.Image"));
  LOKA_VERIFY(image != 0);
  harness.platform.projectLayoutForTesting(content, contentState);
  imageContext = static_cast<NullImageGeometryContext *>(image->getContext());
  LOKA_VERIFY(imageContext != 0);
  geometry = &imageContext->geometry();
  LOKA_VERIFY(geometry->x == 30);
  LOKA_VERIFY(geometry->y == 40);
  LOKA_VERIFY(geometry->width == 80);
  LOKA_VERIFY(geometry->height == 60);
  loka::app::scene::Node *displaySeat = content->childrenHead();
  LOKA_VERIFY(displaySeat != 0);
  displaySeat = displaySeat->nextInComposition;
  LOKA_VERIFY(displaySeat != 0);
  loka::app::scene::INestable *displayNestable = displaySeat->asNestable();
  LOKA_VERIFY(displayNestable != 0);
  LOKA_VERIFY(displayNestable->childrenHead() == image);

  MainAccess::setDisplayMode(*harness.mainNode(),
                                         simpleviewer::DISPLAY_ACTUAL_SCROLL);
  flushScene(*harness.scene);
  loka::app::ScrollViewNode *scroll = static_cast<loka::app::ScrollViewNode *>(
      findNode(main, "SimpleViewer.ActualScroll"));
  image = static_cast<loka::app::ImageViewNode *>(
      findNode(main, "SimpleViewer.Image"));
  LOKA_VERIFY(scroll != 0);
  LOKA_VERIFY(image != 0);
  LOKA_VERIFY(scroll->childrenHead() == image);
  harness.platform.projectLayoutForTesting(content, contentState);
  imageContext = static_cast<NullImageGeometryContext *>(image->getContext());
  LOKA_VERIFY(imageContext != 0);
  geometry = &imageContext->geometry();
  LOKA_VERIFY(geometry->x == 30);
  LOKA_VERIFY(geometry->y == 40);
  LOKA_VERIFY(geometry->width == 80);
  LOKA_VERIFY(geometry->height == 60);
}

void testSimpleViewerCommittedImageResetsRailScrollOffset()
{
  SimpleViewerHarness harness;
  simpleviewer::MainNode *main = harness.mainNode();
  LOKA_VERIFY(main != 0);
  const loka::core::resource::Image tallImage =
      loka::core::resource::Image::FromNative(reinterpret_cast<void *>(1),
                                              80,
                                              800,
                                              0,
                                              0);
  MainAccess::setLoadedImage(*main, tallImage);
  MainAccess::setDisplayMode(*harness.mainNode(),
                                         simpleviewer::DISPLAY_ACTUAL_SCROLL);
  flushScene(*harness.scene);

  loka::app::ScrollViewNode *scroll = static_cast<loka::app::ScrollViewNode *>(
      findNode(main, "SimpleViewer.ActualScroll"));
  LOKA_VERIFY(scroll != 0);
  scroll->props.viewport_.set(loka::core::Frame(11, 73, 0, 0));
  LOKA_VERIFY(scroll->props.viewport_.state()->get() == loka::core::Frame(11, 73, 0, 0));

  MainAccess::setDisplayMode(*harness.mainNode(),
                                         simpleviewer::DISPLAY_FIT);
  flushScene(*harness.scene);
  MainAccess::setDisplayMode(*harness.mainNode(),
                                         simpleviewer::DISPLAY_ACTUAL_SCROLL);
  flushScene(*harness.scene);
  scroll = static_cast<loka::app::ScrollViewNode *>(
      findNode(main, "SimpleViewer.ActualScroll"));
  LOKA_VERIFY(scroll != 0);
  LOKA_VERIFY(scroll->props.viewport_.state()->get() == loka::core::Frame(11, 73, 0, 0));

  const loka::core::resource::Image nextImage =
      loka::core::resource::Image::FromNative(reinterpret_cast<void *>(2),
                                              60,
                                              600,
                                              0,
                                              0);
  MainAccess::setLoadedImage(*main, nextImage);
  LOKA_VERIFY(scroll->props.viewport_.state()->get() == loka::core::Frame(0, 0, 0, 0));
}

void testSimpleViewerPaneScrollButtonUsesMenuEmitter()
{
  SimpleViewerHarness harness;
  simpleviewer::MainNode *main = harness.mainNode();
  LOKA_VERIFY(main != 0);
  loka::app::ButtonNode *button = static_cast<loka::app::ButtonNode *>(
      findNode(main, "SimpleViewer.Mode.ActualScroll"));
  LOKA_VERIFY(button != 0);
  LOKA_VERIFY(button->props.onClick_ ==
              MainAccess::actualScrollEvent(*harness.mainNode()));

  button->props.onClick_->emit();
  flushScene(*harness.scene);
  LOKA_VERIFY(MainAccess::displayModeState(*harness.mainNode())->get() ==
              simpleviewer::DISPLAY_ACTUAL_SCROLL);
  verifyCheckedMode(viewMenu(harness.scene->menuBar()),
                    simpleviewer::DISPLAY_ACTUAL_SCROLL);
}

void testSimpleViewerNarrowWindowFileMenuMaterializesDialogOutsideParkedNav()
{
  NullPlatformContext platformContext;
  SimpleViewerAppConfig config(&platformContext);
  NullScenePlatformController platform;
  simpleviewer::MainProps mainProps;
  mainProps.platformContext(&platformContext)
      .openDialogEvent(MainAccess::openDialogEvent(config));
  loka::app::scene::NodeDefinitionBase *rootDefinition =
      loka::app::scene::Boundary<simpleviewer::MainNode>(mainProps).clone();
  LOKA_VERIFY(rootDefinition != 0);

  WindowProps windowProps;
  windowProps.frame(40, 40, 320, 240)
      .scene(new loka::app::scene::Scene(rootDefinition));
  NullWindow window(&platformContext, windowProps, &platform);
  LOKA_VERIFY(window.scene() != 0);
  loka::app::testing::WindowTestAccess::storeNativeFrame(
      window, loka::core::Frame(40, 40, 320, 240));
  loka::dsl::testing::SceneTestAccess::updateAttached(*window.scene(), true);
  if (window.hasPendingSceneInvalidation())
  {
    LOKA_VERIFY(window.flushSceneInvalidation());
  }

  simpleviewer::MainNode *main = static_cast<simpleviewer::MainNode *>(
      loka::dsl::testing::SceneTestAccess::rootNode(*window.scene()));
  LOKA_VERIFY(main != 0);
  LOKA_VERIFY(findNode(main, "SimpleViewer.NavPane") == 0);
  LOKA_VERIFY(findNode(main, "SimpleViewer.NavToggle") != 0);
  LOKA_VERIFY(findNode(main, "SimpleViewerOpenFileDialog") == 0);

  LOKA_VERIFY(window.scene()->menuBar());
  loka::core::EmitterState *open = window.scene()->menuBar()->menuAt(1)->itemsHead()->onClickState;
  LOKA_VERIFY(open == MainAccess::openDialogEvent(config));
  open->emit();
  window.flushSceneInvalidation();
  LOKA_VERIFY(findNode(main, "SimpleViewerOpenFileDialog") != 0);
}

void testSimpleViewerProductionConfigStartsAtWideBreakpoint()
{
  NullPlatformContext platformContext;
  SimpleViewerAppConfig config(&platformContext);
  AppComposition composition(&platformContext);
  config.compose(composition);
  std::vector<AppComponent *> components = composition.build();
  LOKA_VERIFY(components.size() == 1);
  Window *window = components[0] ? components[0]->asWindow() : 0;
  LOKA_VERIFY(window != 0);
  const loka::core::Frame frame = window->frameState().get();
  LOKA_VERIFY(frame == loka::core::Frame(16, 16, 480, 280));
  LOKA_VERIFY(frame.width >= 480);

  for (std::size_t i = 0; i < components.size(); ++i)
  {
    delete components[i];
  }
}

void testSimpleViewerMainPropsCompareEveryBorrowedSource()
{
  NullPlatformContext platform;
  NullPlatformContext otherPlatform;
  loka::core::EmitterState open, replacement;
  const simpleviewer::MainProps original = simpleviewer::MainProps()
      .platformContext(&platform).openDialogEvent(&open);
  original.assertInitialized();
  LOKA_VERIFY(original.platformContext() == &platform);
  LOKA_VERIFY(original.openDialogEvent() == &open);
  const simpleviewer::MainProps copy(original);
  LOKA_VERIFY(!(original < copy) && !(copy < original));
  simpleviewer::MainProps changed[2];
  for (int i = 0; i < 2; ++i)
    changed[i] = original;
  changed[0].platformContext(&otherPlatform);
  changed[1].openDialogEvent(&replacement);
  for (int i = 0; i < 2; ++i)
    LOKA_VERIFY((original < changed[i]) != (changed[i] < original));
}

void testSimpleViewerSceneDeclaresViewMenuWithCheckedBindings()
{
  SimpleViewerHarness harness;
  const loka::app::MenuBarDefinition *bar = harness.scene->menuBar();
  LOKA_VERIFY(bar != 0);
  const loka::app::MenuDefinition *view = viewMenu(bar);
  LOKA_VERIFY(view && view->title.equals(loka::core::String::Literal("View")));
  verifyCheckedMode(view, simpleviewer::DISPLAY_FIT);
  const loka::app::MenuItemDefinition *item = view->itemsHead();
  for (int i = 0; i < 3; ++i, item = item->nextInComposition)
  {
    LOKA_VERIFY(item && item->checkedBindingState() && item->onClickState);
    item->onClickState->emit();
    flushScene(*harness.scene);
    LOKA_VERIFY(MainAccess::displayModeState(*harness.mainNode())->get() ==
                static_cast<simpleviewer::DisplayMode>(i));
    verifyCheckedMode(view, static_cast<simpleviewer::DisplayMode>(i));
    LOKA_VERIFY(harness.scene->menuBar() == bar);
  }
  MainAccess::setDisplayMode(*harness.mainNode(), simpleviewer::DISPLAY_FIT);
  verifyCheckedMode(view, simpleviewer::DISPLAY_FIT);
}
