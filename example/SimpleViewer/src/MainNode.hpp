#ifndef LOKA_SIMPLE_VIEWER_MAIN_NODE_HPP
#define LOKA_SIMPLE_VIEWER_MAIN_NODE_HPP

#include "app/scene/BorrowedKeys.hpp"
#include "app/Menu.hpp"

#include "app/nodes/boundary/StdComposition.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "app/nodes/nestable/PolicyScope.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/OpenFileDialog.hpp"
#include "app/PlatformContext.hpp"
#include "app/scene/state/NodeState.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/core/Window.hpp"
#include "ImageLoadSession.hpp"
#include "core/State.hpp"
#include "core/String.hpp"
#include "core/resource/Image.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include <cassert>
#include <new>

#ifdef TEST_BUILD
class SimpleViewerTestAccess;
namespace simpleviewer { namespace testing { class MainAccess; } }
#endif

namespace simpleviewer
{
  using loka::app::Button;

  enum NavMode
  {
    NAV_WIDE = 0,
    NAV_NARROW_CLOSED = 1,
    NAV_NARROW_OPEN = 2
  };

  enum DisplayMode
  {
    DISPLAY_FIT = 0,
    DISPLAY_ACTUAL = 1,
    DISPLAY_ACTUAL_SCROLL = 2
  };

  class MainTypeTag
  {
  };

  class MainNode;

  struct MainProps : public loka::app::scene::NodePropsBase<MainProps>
  {
    typedef MainTypeTag TypeTag;
    typedef MainNode NodeType;

    MainProps &platformContext(PlatformContext *context)
    {
      this->keys_.set(KEY_PLATFORM, context);
      return *this;
    }

    PlatformContext *platformContext() const
    {
      return static_cast<PlatformContext *>(const_cast<void *>(this->keys_.get(KEY_PLATFORM)));
    }

    MainProps &openDialogEvent(loka::core::EmitterState *eventState)
    {
      this->keys_.set(KEY_OPEN_DIALOG, eventState);
      return *this;
    }

    loka::core::EmitterState *openDialogEvent() const
    {
      return static_cast<loka::core::EmitterState *>(const_cast<void *>(this->keys_.get(KEY_OPEN_DIALOG)));
    }

    void assertInitialized() const
    {
      assert(this->keys_.complete());
    }

    bool operator<(const loka::app::scene::PropsBase &rhs) const
    {
      return rhs.propsTypeId() == this->propsTypeId() &&
             this->keys_ < static_cast<const MainProps &>(rhs).keys_;
    }

  private:
    enum
    {
      KEY_PLATFORM,
      KEY_OPEN_DIALOG,
      KEY_COUNT
    };
    loka::app::scene::BorrowedKeys<KEY_COUNT> keys_;
  };

  class MainNode : public loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>
  {
    enum
    {
      kNavSeatTag = 2,
      kContentTag = 3,
      kNavToggleSeatTag = 4,
      kDisplaySeatTag = 5,
      kImageViewTag = 6,
      kOpenDialogTag = 7,
      kRootTag = 8
    };

    static const int kNavWidth = 200;
    static const int kNarrowBreakpoint = 480;

  public:
    typedef MainTypeTag TypeTag;

    MainNode(const MainProps &p)
        : loka::app::scene::StdCompositionBoundaryNodeBase<MainProps>(p),
          isDialogShown_(),
          chooserResult_(),
          chooserMessage_(),
          image_(),
          navMode_(),
          navOpen_(),
          viewport_(),
          toggleNavEvent_(),
          imageLoad_(*this)
    {
      this->state(this->displayMode_, DISPLAY_FIT);
      this->derived(this->fitChecked_, this->displayMode_,
                    new (std::nothrow) ModeCheckedEvalFn(this->displayMode_, DISPLAY_FIT));
      this->derived(this->actualChecked_, this->displayMode_,
                    new (std::nothrow) ModeCheckedEvalFn(this->displayMode_, DISPLAY_ACTUAL));
      this->derived(this->actualScrollChecked_, this->displayMode_,
                    new (std::nothrow) ModeCheckedEvalFn(this->displayMode_, DISPLAY_ACTUAL_SCROLL));
      this->state(this->isDialogShown_, false);
      this->state(this->chooserResult_, loka::app::FileChooserResult());
      this->state(this->chooserMessage_, loka::core::String::Literal("(none)"));
      this->state(this->image_, loka::core::resource::Image::Empty());
      this->state(this->navMode_, NAV_WIDE);
      this->state(this->navOpen_, false);
      this->state(this->viewport_, loka::core::Frame(0, 0, 0, 0));
    }

    virtual void composeNode(loka::app::scene::NodeComposition &c)
    {
      using namespace loka::app;
      this->props.assertInitialized();
      c.menuBar(MenuBarDefinition() << StandardMenuPresets()
                                    << (Menu("File") << MenuItem("Open...").onClick(this->props.openDialogEvent()))
                                    << (Menu("View")
                                        << MenuItem("Fit to Window")
                                               .attr(MenuItemAttr().checked(this->fitChecked_.state()))
                                               .onClick(&this->fitToWindowEvent_)
                                        << MenuItem("Actual Size")
                                               .attr(MenuItemAttr().checked(this->actualChecked_.state()))
                                               .onClick(&this->actualEvent_)
                                        << MenuItem("Actual Size (Scroll)")
                                               .attr(MenuItemAttr().checked(this->actualScrollChecked_.state()))
                                               .onClick(&this->actualScrollEvent_)));

      MatchDefinition<NavMode> nav = Match(*this->navMode_.state());
      nav.arm(NAV_WIDE, this->navPane(false))
          .arm(NAV_NARROW_OPEN, this->navPane(true))
          .otherwise(Fragment());
      nav.setNodeTag(kNavSeatTag);

      MatchDefinition<NavMode> navToggle = Match(*this->navMode_.state());
      navToggle.arm(NAV_NARROW_CLOSED, this->navToggleButton())
          .otherwise(Fragment());
      navToggle.setNodeTag(kNavToggleSeatTag);

      MatchDefinition<DisplayMode> display = Match(*this->displayMode_.state());
      display.arm(DISPLAY_FIT, this->imageView(IMAGE_VIEW_SIZE_FILL_PARENT))
          .arm(DISPLAY_ACTUAL, this->imageView(IMAGE_VIEW_SIZE_INTRINSIC))
          .arm(DISPLAY_ACTUAL_SCROLL,
               ScrollView(this->viewport_)
                       .TEST_ID("SimpleViewer.ActualScroll")
               << this->imageView(IMAGE_VIEW_SIZE_INTRINSIC));
      display.setNodeTag(kDisplaySeatTag);

      ShowDefinition openDialog =
          Show(*this->isDialogShown_.state())
          << (PolicyScopeDefinition().destroyOnDetach()
              << OpenFileDialog().result(this->chooserResult_).testId("SimpleViewerOpenFileDialog"));
      openDialog.setNodeTag(kOpenDialogTag);

      VStack content = VStack().TEST_ID("SimpleViewer.Content");
      content.setNodeTag(kContentTag);
      content << navToggle << display;
      // The dialog is a resource, not a seat: the Row's width consultation
      // skips it (#588), so it sits beside nav and content instead of under
      // a VStack that kept it out of the consultation.
      HStack root = HStack().TEST_ID("SimpleViewer.Root");
      root.setNodeTag(kRootTag);
      c.declare(root << nav << content << openDialog);
    }

  private:
    friend class ImageLoadSession;
#ifdef TEST_BUILD
    friend class testing::MainAccess;
    friend class ::SimpleViewerTestAccess;
#endif

    class ModeCheckedEvalFn : public loka::core::DerivedState<bool>::EvalFn
    {
    public:
      ModeCheckedEvalFn(const loka::app::scene::NodeState<DisplayMode> &mode,
                        DisplayMode selected)
          : mode_(mode), selected_(selected) {}
      virtual bool operator()() { return this->mode_.get() == this->selected_; }
    private:
      const loka::app::scene::NodeState<DisplayMode> &mode_;
      const DisplayMode selected_;
    };

    void fitToWindow() { this->displayMode_.set(DISPLAY_FIT); }
    void showActual() { this->displayMode_.set(DISPLAY_ACTUAL); }
    void showActualScrolling() { this->displayMode_.set(DISPLAY_ACTUAL_SCROLL); }

    loka::app::ButtonDefinition navToggleButton()
    {
      using namespace loka::app;
      return Button("=")
          .onClick(&this->toggleNavEvent_)
          .TEST_ID("SimpleViewer.NavToggle");
    }

    loka::app::HStack navToggleHeader()
    {
      using namespace loka::app;
      return HStack()
             << (Box().size(layout::FallbackControlMetrics::kButtonHeight,
                            layout::FallbackControlMetrics::kButtonHeight)
                 << this->navToggleButton());
    }

    loka::app::Box navPane(bool showToggle)
    {
      using namespace loka::app;
      VStack contents = VStack().alignHorizontal(HORIZONTAL_ALIGNMENT_LEADING);
      if (showToggle)
      {
        contents << this->navToggleHeader();
      }
      contents << Button("Open...").onClick(this->props.openDialogEvent())
               << Text("Loka file:")
               << Text(this->chooserMessage_.state())
                      + BlockStyle().wrap(TEXT_WRAP_CHAR).truncation(TEXT_TRUNCATION_NONE)
               << Button("Fit to Window")
                      .onClick(&this->fitToWindowEvent_)
                      .TEST_ID("SimpleViewer.Mode.Fit")
               << Button("Actual Size")
                      .onClick(&this->actualEvent_)
                      .TEST_ID("SimpleViewer.Mode.Actual")
               << Button("Actual Size (Scroll)")
                      .onClick(&this->actualScrollEvent_)
                      .TEST_ID("SimpleViewer.Mode.ActualScroll");
      return Box()
                 .size(kNavWidth, 0)
                 .TEST_ID("SimpleViewer.NavPane")
             << contents;
    }

    loka::app::ImageViewDefinitionWithAttr imageView(
        loka::app::ImageViewSizePolicy sizePolicy)
    {
      using namespace loka::app;
      ImageViewDefinitionWithAttr image =
          ImageView()
              .image(this->image_.state())
              .attr(ImageViewAttr().sizePolicy(sizePolicy).fit(IMAGE_FIT_CONTAIN))
              .TEST_ID("SimpleViewer.Image");
      image.setNodeTag(kImageViewTag);
      return image;
    }

    virtual void declareBindings(loka::app::scene::BindingToken &t)
    {
      t.action(this->fitToWindowEvent_, this, &MainNode::fitToWindow);
      t.action(this->actualEvent_, this, &MainNode::showActual);
      t.action(this->actualScrollEvent_, this, &MainNode::showActualScrolling);
      t.action(*this->props.openDialogEvent(), this, &MainNode::openDialog);
      t.action(this->toggleNavEvent_, this, &MainNode::toggleNavigation);
      ::Window *window = this->windowOrNull();
      if (window)
      {
        t.watch(window->nativeFrame(), this, &MainNode::refreshLayoutMode, true);
      }
    }

    ::Window *windowOrNull() const
    {
      const AttachedContext *ctx = this->attachedContext();
      return ctx ? ctx->window() : 0;
    }

    void refreshLayoutMode()
    {
      ::Window *window = this->windowOrNull();
      if (!window || !this->navMode_.isValid())
      {
        return;
      }
      const loka::core::Frame frame = window->nativeFrame().get();
      const bool narrow = frame.hasSize() && frame.width > 0 && frame.width < kNarrowBreakpoint;
      const NavMode mode = narrow
                           ? (this->navOpen_.get() ? NAV_NARROW_OPEN : NAV_NARROW_CLOSED)
                           : NAV_WIDE;
      if (this->navMode_.get() == mode)
      {
        return;
      }
      this->navMode_.set(mode);
      this->markViewDirty(loka::app::scene::NODE_DIRTY_CHILD);
    }

    void toggleNavigation()
    {
      this->navOpen_.set(!this->navOpen_.get());
      this->refreshLayoutMode();
    }

    void openDialog()
    {
      this->imageLoad_.begin(
          *this,
          this->props.platformContext(),
          this->chooserResult_.state(),
          static_cast<loka::core::PushStateTracker *>(this->tracker()));
      this->isDialogShown_.set(true, true);
    }

    bool hasCurrentImage() const
    {
      return this->image_.get().isValid();
    }

    void releaseCurrentImageForLoad()
    {
      const loka::core::resource::Image empty = loka::core::resource::Image::Empty();
      if (this->image_.get() == empty)
      {
        return;
      }
      this->image_.set(empty);
    }

    void commitLoadedImage(const loka::core::resource::Image &image)
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->viewport_.set(loka::core::Frame(0, 0, 0, 0));
      this->image_.set(image, true);
    }

    void closeDialogForChooserResult(const loka::app::FileChooserResult &result)
    {
      if (result.kind != loka::app::FileChooserResult::RESULT_NONE && this->isDialogShown_.get())
      {
        this->isDialogShown_.set(false, true);
      }
    }

    bool isImageLoadDialogShown() const
    {
      return this->isDialogShown_.get();
    }

    void setChooserMessageIfChanged(const loka::core::String &message)
    {
      if (this->chooserMessage_.get().equals(message))
      {
        return;
      }
      this->chooserMessage_.set(message);
    }

    loka::app::scene::NodeState<bool> isDialogShown_;
    loka::app::scene::NodeState<loka::app::FileChooserResult> chooserResult_;
    loka::app::scene::NodeState<loka::core::String> chooserMessage_;
    loka::app::scene::NodeState<loka::core::resource::Image> image_;
    loka::app::scene::NodeState<NavMode> navMode_;
    loka::app::scene::NodeState<bool> navOpen_;
    loka::app::scene::NodeState<loka::core::Frame> viewport_;
    loka::core::EmitterState toggleNavEvent_;
    ImageLoadSession imageLoad_;
    loka::app::scene::NodeState<DisplayMode> displayMode_;
    loka::app::scene::DerivedNodeState<bool> fitChecked_;
    loka::app::scene::DerivedNodeState<bool> actualChecked_;
    loka::app::scene::DerivedNodeState<bool> actualScrollChecked_;
    loka::core::EmitterState fitToWindowEvent_;
    loka::core::EmitterState actualEvent_;
    loka::core::EmitterState actualScrollEvent_;
  };
} // namespace simpleviewer

#include "ImageLoadSessionFlow.hpp"

#endif // LOKA_SIMPLE_VIEWER_MAIN_NODE_HPP
