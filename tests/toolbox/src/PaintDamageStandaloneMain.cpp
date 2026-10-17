#include "ToolboxLayoutMetrics.hpp"
#include "app/style/CardShadow.hpp"
#include "core/Profiler.hpp"
#include "ToolboxProfiler.hpp"
#include "context/ToolboxCardBandContext.hpp"
#include "ToolboxPaintGradient.hpp"
#include "ToolboxPaintGround.hpp"
#include "app/layout/FramedLayout.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "context/ToolboxAttributedTextTable.hpp"
#include <cstdio>
#include <cstring>
#include <Fonts.h>
#include <MacMemory.h>

#include "StandaloneFlowRunner.hpp"
#include "ObservedMainDefinition.hpp"
#include "ToolboxScenePlatformController.hpp"
#include "ToolboxWindow.hpp"
#include "context/ToolboxImageViewContext.hpp"
#include "ToolboxApp.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include "context/ToolboxLayoutUtil.hpp"
#include "context/ToolboxTextContext.hpp"
#include "app/core/AppComposition.hpp"
#include "app/core/AppConfigurable.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/RectSurface.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "context/ToolboxEditTextContext.hpp"
#include "context/ToolboxPopupMenuContext.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"
#include "app/scene/projection/NativeNodeContext.hpp"
#include "context/ToolboxPaintSupport.hpp"
#include "core/io/File.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/file/AppLocation.hpp"
#include "platform/file/FileIO.hpp"
#include "ToolboxPlatformContext.hpp"
#include "core/resource/Blob.hpp"
#include "core/resource/Image.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"

namespace loka
{
  namespace testing
  {
    /** Read native placement without painting, relayout, or dirtying test data. */
    class ToolboxTextContextAccess
    {
    public:
      static Rect rect(const ToolboxTextContext &context) { return context.rect_; }
      static short baseline(const ToolboxTextContext &context) { return context.textY_; }
    };
  }
}

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;

  // These windows keep unspecified margins, so their root origins follow
  // the rail standard. Fixture sizes and local ink offsets are separate.
  enum
  {
    kRootX = ToolboxLayoutMetrics::kRootInset,
    kRootY = ToolboxLayoutMetrics::kRootInset,
    kRootGap = 8,
    kScrollSurfaceY = kRootY + 48 + kRootGap - 32,
    kScrollTextY = kScrollSurfaceY + 50 + kRootGap + 50 + kRootGap,
    kBoundsViewportY = kRootY + 40 + kRootGap,
    kCellThirdY = kRootY + 2 * (24 + kRootGap),
    kOverflowCellX = kRootX + 42 + kRootGap
  };

  /** A StateTrackerGuard write inside the idle turn commits at the turn's
      settle (#1080). An arm that reads the result in the same turn settles
      first, as the turn's tail would. */
  void SettleTurn()
  {
    if (loka::core::Operation *turn = loka::core::testing::OperationTestAccess::active())
      turn->settle();
  }

  template <bool HasViewport> class PaintDamageNode;

  /** The Boundary owns both models and the live text for the whole pin. */
  template <bool HasViewport>
  class PaintDamageNode : public StdCompositionBoundaryNodeBase<BoundaryPropsFor<PaintDamageNode<HasViewport> > >
  {
  public:
    typedef BoundaryPropsFor<PaintDamageNode<HasViewport> > Props;
    typedef typename Props::TypeTag TypeTag;
    explicit PaintDamageNode(const Props &props)
        : StdCompositionBoundaryNodeBase<Props>(props)
    {
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(4, 4, 12, 12);
      this->state(this->first_, model);
      this->state(this->sibling_, model);
      this->state(this->text_, loka::core::String::Literal("Viewport paint damage"));
      this->state(this->offset_, loka::core::Frame(0, 32, 0, 0));
    }
    virtual void composeNode(NodeComposition &composition)
    {
      Column surfaces = Column()
                        << RectSurface(this->first_.state()).size(180, 50).useRegionClip(true).TEST_ID("PaintDamage.First")
                        << RectSurface(this->sibling_.state()).size(180, 50).TEST_ID("PaintDamage.Sibling");
      if (HasViewport)
        composition.declare(Box().size(240, 180)
                            << (ScrollView(this->offset_)
                                << (Column() << Box().size(180, 48) << surfaces
                                    << Text(this->text_.state()).TEST_ID("PaintDamage.ScrollText")
                                    << Box().size(180, 160))));
      else
        composition.declare(surfaces);
    }
    void advance()
    {
      RectSurfaceModel model = this->first_.get();
      model.rects[0].x += 12;
      loka::core::StateTrackerGuard guard(this->tracker());
      this->first_.set(model);
    }

  private:
    NodeState<RectSurfaceModel> first_;
    NodeState<RectSurfaceModel> sibling_;
    NodeState<loka::core::String> text_;
    NodeState<loka::core::Frame> offset_;
  };

  typedef PaintDamageNode<true> ViewportDamageNode;
  typedef PaintDamageNode<false> PlainDamageNode;

  class OffscreenDamageNode;
  typedef BoundaryPropsFor<OffscreenDamageNode> OffscreenDamageProps;
  /** One column overflows without truncation or wrapping, like HelloWorld. */
  class OffscreenDamageNode : public StdCompositionBoundaryNodeBase<OffscreenDamageProps>
  {
  public:
    typedef OffscreenDamageProps::TypeTag TypeTag;
    explicit OffscreenDamageNode(const OffscreenDamageProps &props)
        : StdCompositionBoundaryNodeBase<OffscreenDamageProps>(props)
    {
      this->state(this->sibling_, loka::core::String::Literal("MMMM"));
      this->state(this->text_, loka::core::String::Literal("MMMM"));
      this->state(this->hidden_, loka::core::String::Literal("MMMM"));
      this->state(this->edit_, loka::core::String::Literal("Edit"));
      this->state(this->offset_, loka::core::Frame(0, 0, 0, 0));
      this->state(this->button_, loka::core::String::Literal("Button"));
      this->state(this->drawersShown_, true);
      this->items_.push_back(loka::core::String::Literal("Fruit"));
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Box().size(180, 110)
                          << (ScrollView(this->offset_)
                              << (Column() << Text(this->sibling_.state())
                                  << Text(this->text_.state()).TEST_ID("Offscreen.VisibleText")
                                  << Box().size(150, 140)
                                  << (Show(*this->drawersShown_.state())
                                      << Text(this->hidden_.state()).TEST_ID("Offscreen.Text")
                                      << EditText(this->edit_).TEST_ID("Offscreen.Edit")
                                      << PopupMenu(this->items_).TEST_ID("Offscreen.Popup")
                                      << loka::app::Button(this->button_.state()).TEST_ID("Offscreen.Button")))));
    }
    void writeSibling()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->sibling_.set(loka::core::String::Literal("IIII"));
    }
    void writeText(bool hidden, const char *value = "IIII")
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      if (hidden)
        this->hidden_.set(loka::core::String::Literal(value));
      else
        this->text_.set(loka::core::String::Literal("IIII"));
    }
    void writeButton(bool wider)
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->button_.set(loka::core::String::Literal(wider ? "Much wider button title" : "Button"));
    }
    void hideDrawers()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->drawersShown_.set(false);
    }
    void scrollToTop()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->offset_.set(loka::core::Frame(0, 0, 0, 0));
    }
    void reveal()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->offset_.set(loka::core::Frame(0, 160, 0, 0));
    }
  private:
    NodeState<loka::core::String> sibling_, text_, hidden_, edit_, button_;
    NodeState<loka::core::Frame> offset_;
    NodeState<bool> drawersShown_;
    loka::Vector<loka::core::String> items_;
  };

  class CompositedDamageNode;
  typedef BoundaryPropsFor<CompositedDamageNode> CompositedDamageProps;
  class CompositedDamageNode : public StdCompositionBoundaryNodeBase<CompositedDamageProps>
  {
  public:
    typedef CompositedDamageProps::TypeTag TypeTag;
    explicit CompositedDamageNode(const CompositedDamageProps &props)
        : StdCompositionBoundaryNodeBase<CompositedDamageProps>(props)
    {
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(4, 4, 12, 12);
      this->state(this->model_, model);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Box().size(180, 110)
                          << (ScrollView()
                              << Text("Baseline").TEST_ID("PaintDamage.Baseline")
                              << (ZStack() << RectSurface(this->model_.state()).size(150, 70) << Text("ZStack"))));
    }
    void advance()
    {
      RectSurfaceModel model = this->model_.get();
      model.rects[0].x += 12;
      loka::core::StateTrackerGuard guard(this->tracker());
      this->model_.set(model);
    }
  private:
    NodeState<RectSurfaceModel> model_;
  };

  class ImageSurfaceNode;
  typedef BoundaryPropsFor<ImageSurfaceNode> ImageSurfaceProps;
  /** Only this Boundary owns and changes the overlapping surface model. */
  class ImageSurfaceNode : public StdCompositionBoundaryNodeBase<ImageSurfaceProps>
  {
  public:
    typedef ImageSurfaceProps::TypeTag TypeTag;
    explicit ImageSurfaceNode(const ImageSurfaceProps &props)
        : StdCompositionBoundaryNodeBase<ImageSurfaceProps>(props)
    {
      this->setTestId("ImageDamage.Owner");
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(0, 0, 120, 24);
      this->state(this->model_, model);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(RectSurface(this->model_.state()).size(150, 70)
                          .TEST_ID("ImageDamage.Surface"));
    }
    void advance()
    {
      RectSurfaceModel model = this->model_.get();
      model.rects[0].x = 128;
      loka::core::StateTrackerGuard guard(this->tracker());
      this->model_.set(model);
    }
  private:
    NodeState<RectSurfaceModel> model_;
  };

  class ImagePlaceholderNode;
  typedef BoundaryPropsFor<ImagePlaceholderNode> ImagePlaceholderProps;
  /** The image has a separate paint-answer scope from the changing surface. */
  class ImagePlaceholderNode : public StdCompositionBoundaryNodeBase<ImagePlaceholderProps>
  {
  public:
    typedef ImagePlaceholderProps::TypeTag TypeTag;
    explicit ImagePlaceholderNode(const ImagePlaceholderProps &props)
        : StdCompositionBoundaryNodeBase<ImagePlaceholderProps>(props) {}
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(ImageView().size(150, 70));
    }
  };

  class ImageOverlapNode;
  typedef BoundaryPropsFor<ImageOverlapNode> ImageOverlapProps;
  class ImageOverlapNode : public StdCompositionBoundaryNodeBase<ImageOverlapProps>
  {
  public:
    typedef ImageOverlapProps::TypeTag TypeTag;
    explicit ImageOverlapNode(const ImageOverlapProps &props)
        : StdCompositionBoundaryNodeBase<ImageOverlapProps>(props) {}
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Box().size(180, 110)
                          << (ScrollView()
                              << (ZStack()
                                  << loka::app::scene::Boundary<ImageSurfaceNode>()
                                  << loka::app::scene::Boundary<ImagePlaceholderNode>())));
    }
  };

  class ImageVacatedNode;
  typedef BoundaryPropsFor<ImageVacatedNode> ImageVacatedProps;
  /** An ImageView alone in its window, so its dirty repaint takes the
      controller's image path (no ZStack, no other drawers). */
  class ImageVacatedNode : public StdCompositionBoundaryNodeBase<ImageVacatedProps>
  {
  public:
    typedef ImageVacatedProps::TypeTag TypeTag;
    explicit ImageVacatedNode(const ImageVacatedProps &props)
        : StdCompositionBoundaryNodeBase<ImageVacatedProps>(props) {}
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(ImageView().size(150, 70).TEST_ID("ImageVacated.Image"));
    }
  };

  class EditDamageNode;
  typedef BoundaryPropsFor<EditDamageNode> EditDamageProps;
  class EditDamageNode : public StdCompositionBoundaryNodeBase<EditDamageProps>
  {
  public:
    typedef EditDamageProps::TypeTag TypeTag;
    explicit EditDamageNode(const EditDamageProps &props)
        : StdCompositionBoundaryNodeBase<EditDamageProps>(props)
    {
      this->state(this->text_, loka::core::String::Literal("Clipped edit"));
    }
    virtual void composeNode(NodeComposition &composition)
    {
      // The spacer plus inter-seat gap is 48: the 62px viewport leaves
      // 14 px of the 20px EditText chrome visible and clips 6 px.
      composition.declare(Box().size(180, 62)
                          << (ScrollView()
                              << (Column() << Box().size(150, 48 - kRootGap)
                                  << EditText(this->text_).TEST_ID("PaintDamage.Edit"))));
    }
  private:
    NodeState<loka::core::String> text_;
  };

  class EditZStackNode;
  typedef BoundaryPropsFor<EditZStackNode> EditZStackProps;
  /** The live edit and its later overlapping surface share one Boundary. */
  class EditZStackNode : public StdCompositionBoundaryNodeBase<EditZStackProps>
  {
  public:
    typedef EditZStackProps::TypeTag TypeTag;
    explicit EditZStackNode(const EditZStackProps &props)
        : StdCompositionBoundaryNodeBase<EditZStackProps>(props)
    {
      this->state(this->text_, loka::core::String::Literal(""));
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(4, 4, 12, 12);
      this->state(this->model_, model);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(ZStack()
                          << EditText(this->text_).TEST_ID("PaintDamage.Edit")
                          << RectSurface(this->model_.state()).size(16, 20).TEST_ID("EditZStack.Surface"));
    }
    void advance()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->text_.set(loka::core::String::Literal("    HHHH"));
    }
  private:
    NodeState<loka::core::String> text_;
    NodeState<RectSurfaceModel> model_;
  };

  class EditExactNode;
  typedef BoundaryPropsFor<EditExactNode> EditExactProps;
  /** One owner makes the paint walk visit the edit and both siblings. */
  class EditExactNode : public StdCompositionBoundaryNodeBase<EditExactProps>
  {
  public:
    typedef EditExactProps::TypeTag TypeTag;
    explicit EditExactNode(const EditExactProps &props)
        : StdCompositionBoundaryNodeBase<EditExactProps>(props)
    {
      this->state(this->text_, loka::core::String::Literal(""));
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(4, 4, 12, 12);
      this->state(this->model_, model);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Box().size(180, 140)
                          << (ScrollView()
                              << (Column()
                                  << EditText(this->text_).TEST_ID("PaintDamage.Edit")
                                  << Text("Sibling ink").TEST_ID("EditExact.Text")
                                  << RectSurface(this->model_.state()).size(150, 40)
                                      .TEST_ID("EditExact.Surface"))));
    }
    void advance()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->text_.set(loka::core::String::Literal("HHHH"));
    }
  private:
    NodeState<loka::core::String> text_;
    NodeState<RectSurfaceModel> model_;
  };

  class ButtonExactNode;
  typedef BoundaryPropsFor<ButtonExactNode> ButtonExactProps;
  /** Each arm owns its button input and unchanged siblings in one Boundary. */
  class ButtonExactNode : public StdCompositionBoundaryNodeBase<ButtonExactProps>
  {
  public:
    typedef ButtonExactProps::TypeTag TypeTag;
    explicit ButtonExactNode(const ButtonExactProps &props)
        : StdCompositionBoundaryNodeBase<ButtonExactProps>(props)
    {
      this->state(this->enabled_, true);
      this->state(this->title_, loka::core::String::Literal("MMMMIIII"));
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(4, 4, 12, 12);
      this->state(this->model_, model);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Column()
                          << loka::app::Button(this->title_.state()).enabled(this->enabled_.state()).controlTag(911).TEST_ID("ButtonExact.Button")
                          << Text("Sibling ink").TEST_ID("ButtonExact.Text")
                          << RectSurface(this->model_.state()).size(150, 40)
                              .TEST_ID("ButtonExact.Surface"));
    }
    void advance(bool label)
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      if (label)
        this->title_.set(loka::core::String::Literal("IIIIMMMM"));
      else
        this->enabled_.set(false);
    }
    void widenTitle()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->title_.set(loka::core::String::Literal("MMMMMMMM"));
    }
  private:
    NodeState<bool> enabled_;
    NodeState<loka::core::String> title_;
    NodeState<RectSurfaceModel> model_;
  };

  class TextStyleChangeNode;
  typedef BoundaryPropsFor<TextStyleChangeNode> TextStyleChangeProps;
  /** The Boundary owns the MutableState; Text borrows its read-only State door. */
  class TextStyleChangeNode : public StdCompositionBoundaryNodeBase<TextStyleChangeProps>
  {
  public:
    typedef TextStyleChangeProps::TypeTag TypeTag;
    explicit TextStyleChangeNode(const TextStyleChangeProps &props)
        : StdCompositionBoundaryNodeBase<TextStyleChangeProps>(props)
    {
      this->state<loka::app::TextStyle>(this->style_, FontSize<12>());
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Column()
                          << (Text("M") + this->style_.state()).TEST_ID("TextStyleChange.Text")
                          << (Text("MMMMMMMM") + FontSize<12>()).TEST_ID("TextStyleChange.Sibling"));
    }
    void enlarge()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->style_.set(FontSize<24>());
    }
  private:
    NodeState<loka::app::TextStyle> style_;
  };

  /** Completed pre-change placement and a row of ink that must be erased. */
  class TextStyleChangeBefore
  {
  public:
    TextStyleChangeBefore() : text_(), sibling_(), oldInkRow_(), baseline_(0) {}
    TextStyleChangeBefore(const Rect &text, const Rect &sibling, const Rect &oldInkRow, short baseline)
        : text_(text), sibling_(sibling), oldInkRow_(oldInkRow), baseline_(baseline) {}
    const Rect &text() const { return this->text_; }
    const Rect &sibling() const { return this->sibling_; }
    const Rect &oldInkRow() const { return this->oldInkRow_; }
    short baseline() const { return this->baseline_; }
  private:
    Rect text_;
    Rect sibling_;
    Rect oldInkRow_;
    short baseline_;
  };

  int CountTextStyleInk(const Rect &rect)
  {
    int ink = 0;
    for (short y = rect.top; y < rect.bottom; ++y)
      for (short x = rect.left; x < rect.right; ++x)
        if (GetPixel(x, y))
          ++ink;
    return ink;
  }

  class TextWidthNode;
  typedef BoundaryPropsFor<TextWidthNode> TextWidthProps;
  /** One Boundary owns a Row whose first Text grows wider; the Row seats its
      Texts by flex allocation, so the sibling must keep its column. */
  class TextWidthNode : public StdCompositionBoundaryNodeBase<TextWidthProps>
  {
  public:
    typedef TextWidthProps::TypeTag TypeTag;
    explicit TextWidthNode(const TextWidthProps &props)
        : StdCompositionBoundaryNodeBase<TextWidthProps>(props), siblingBefore_(), siblingInk_(0)
    {
      this->state(this->first_, loka::core::String::Literal("II"));
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(4, 4, 12, 12);
      this->state(this->model_, model);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Column()
                          << (Row() << Text(this->first_.state()).TEST_ID("TextWidth.First")
                                    << Text("Sibling").TEST_ID("TextWidth.Second")
                                    << RectSurface(this->model_.state()).size(40, 40)
                                        .TEST_ID("TextWidth.Surface")));
    }
    void advance()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->first_.set(loka::core::String::Literal("MMMMMMMM"));
    }
    bool captureSibling(const PaintDamage &damage)
    {
      this->siblingBefore_ = damage;
      this->siblingInk_ = 0;
      // Unchanged EXACT answers retain x/y but have zero extent. Sample the
      // default Text line, just as ButtonTitlePixels samples its title interior.
      for (int y = 0; y < ToolboxLayoutMetrics::kDefaultLineHeight; ++y)
        if (GetPixel(damage.x + 2, damage.y + y))
          this->siblingInk_ |= 1UL << y;
      return this->siblingInk_ != 0;
    }
    /** The sibling answers the same column and every captured ink pixel survives. */
    bool siblingSeated(const PaintDamage &damage) const
    {
      if (damage.x != this->siblingBefore_.x || damage.y != this->siblingBefore_.y)
        return false;
      for (int y = 0; y < ToolboxLayoutMetrics::kDefaultLineHeight; ++y)
        if ((this->siblingInk_ & (1UL << y)) && !GetPixel(damage.x + 2, damage.y + y))
          return false;
      return true;
    }
    short siblingColumn() const { return this->siblingBefore_.x; }
  private:
    NodeState<loka::core::String> first_;
    NodeState<RectSurfaceModel> model_;
    PaintDamage siblingBefore_;
    unsigned long siblingInk_;
  };

  /** Fixture-local timing stream completion; always closes, including write failure. */
  bool CloseCardTiming(std::FILE *timing)
  {
    const bool written = std::ferror(timing) == 0;
    const bool closed = std::fclose(timing) == 0;
    return written && closed;
  }

  /** One-arm packed native pixel capture. Nullable storage is released before
      the arm returns; a second packed plane holds each current-port comparison. */
  class CardCapture
  {
  public:
    explicit CardCapture(const Rect &frame)
        : frame_(frame),
          pixels_(NewPtrClear(2 * stride(frame) * (frame.bottom - frame.top)))
    {
      if (this->pixels_)
        this->capture(this->pixels_);
    }
    ~CardCapture()
    {
      if (this->pixels_)
        DisposePtr(this->pixels_);
    }
    bool ready() const
    {
      return this->pixels_ != 0;
    }
    /** Small independent native pin for packed capture phase and right padding. */
    bool matchesProbe() const
    {
      const int width = this->frame_.right - this->frame_.left;
      const int height = this->frame_.bottom - this->frame_.top;
      for (int y = 0; y < 8 && y < height; ++y)
        for (int probe = 0; probe < 9; ++probe)
        {
          const int x = probe == 8 ? width - 1 : probe;
          if (x >= width)
            continue;
          const bool bit = (this->pixels_[y * stride(this->frame_) + x / 8] & (0x80 >> (x & 7))) != 0;
          if (bit != (GetPixel(this->frame_.left + x, this->frame_.top + y) != 0))
            return false;
        }
      return true;
    }
    int differences(const Pattern *ground = 0) const
    {
      Ptr current = this->pixels_ + stride(this->frame_) * (this->frame_.bottom - this->frame_.top);
      this->capture(current);
      const unsigned char *pattern = reinterpret_cast<const unsigned char *>(ground);
      const int width = this->frame_.right - this->frame_.left;
      int differences = 0;
      for (int y = 0; y < this->frame_.bottom - this->frame_.top; ++y)
      {
        unsigned char background = pattern ? pattern[(this->frame_.top + y) & 7] : 0;
        const int phase = this->frame_.left & 7;
        background = static_cast<unsigned char>((background << phase) | (background >> (8 - phase)));
        for (int x = 0; x < (width + 7) / 8; ++x)
        {
          const int index = y * stride(this->frame_) + x;
          unsigned char changed = static_cast<unsigned char>(current[index] ^ (this->pixels_[index] | background));
          if (x * 8 + 8 > width)
            changed &= 0xff << (8 - (width & 7));
          while (changed)
          {
            differences += changed & 1;
            changed >>= 1;
          }
        }
      }
      return differences;
    }
    void dump(std::FILE *log, const char *label, bool current) const
    {
      Ptr pixels = this->pixels_;
      if (current)
      {
        pixels += stride(this->frame_) * (this->frame_.bottom - this->frame_.top);
        this->capture(pixels);
      }
      std::fprintf(log,
                   "card-dump %s x=%d y=%d w=%d h=%d\r",
                   label,
                   this->frame_.left,
                   this->frame_.top,
                   this->frame_.right - this->frame_.left,
                   this->frame_.bottom - this->frame_.top);
      for (int y = 0; y < this->frame_.bottom - this->frame_.top; ++y)
      {
        for (int x = 0; x < this->frame_.right - this->frame_.left; ++x)
          std::fputc((pixels[y * stride(this->frame_) + x / 8] & (0x80 >> (x & 7))) ? '1' : '0', log);
        std::fputc('\r', log);
      }
    }

  private:
    const Rect frame_;
    Ptr pixels_;
    static int stride(const Rect &frame)
    {
      return 2 * ((frame.right - frame.left + 15) / 16);
    }
    void capture(Ptr pixels) const
    {
      GrafPtr port;
      GetPort(&port);
      BitMap bitmap;
      bitmap.baseAddr = pixels;
      bitmap.rowBytes = stride(this->frame_);
      bitmap.bounds = this->frame_;
      CopyBits(&port->portBits, &bitmap, &this->frame_, &bitmap.bounds, srcCopy, 0);
    }
    CardCapture(const CardCapture &);
    CardCapture &operator=(const CardCapture &);
  };

  /** Pixel fixture using production framed placement and production band contexts.
        The enclosing standalone window supplies the real QuickDraw port. */
  class CardPixels
  {
  public:
    explicit CardPixels(const CardStyle &style = CardStyle(CARD_SHADOW_DROP))
        : card_(CardProps(style))
    {
      this->card_.addChild(new Node());
      for (int role = 0; role < CARD_BAND_COUNT; ++role)
      {
        CardBandNode *band = new CardBandNode(CardBandProps(static_cast<CardBandRole>(role), style));
        band->setContext(new ToolboxCardBandContext(band, 0));
        this->card_.addChild(band);
      }
    }
    void layout(const Rect &frame)
    {
      LayoutState state;
      state.x = frame.left;
      state.y = frame.top;
      state.width = frame.right - frame.left;
      state.height = frame.bottom - frame.top;
      loka::app::layout::computeFramedLayoutResultY(&this->card_, state, 0, Place, 0);
    }
    void draw(int role = -1)
    {
      loka::dsl::CompositionCursor<Node> children(this->card_.childrenHead(), this->card_.childrenCount());
      for (Node *child = children.next(); child; child = children.next())
        if (child->asCardBandNode() && (role < 0 || child->asCardBandNode()->props.role == role))
          child->getContext()->render(0);
    }
    static void capture(const Rect &frame, unsigned char *pixels)
    {
      for (short y = frame.top; y < frame.bottom; ++y)
        for (short x = frame.left; x < frame.right; ++x)
          *pixels++ = GetPixel(x, y) != 0;
    }
    /** Whole-card common oracle with the native border, independent of band partitioning. */
    static bool tableReference(const Rect &frame, const CardStyle &style = CardStyle(CARD_SHADOW_DROP))
    {
      const loka::app::detail::CardShadowTable &g = style.geometry();
      const loka::core::Frame body(frame.left + g.allowance[0],
                                   frame.top + g.allowance[1],
                                   frame.right - frame.left - g.allowance[0] - g.allowance[2],
                                   frame.bottom - frame.top - g.allowance[1] - g.allowance[3]);
      PenState pen;
      GetPenState(&pen);
      PenNormal();
      PenMode(patOr);
      // The oracle walks whole rows, never the painter's tile/band partition.
      // CopyBits avoids one QuickDraw trap per black pixel.
      union
      {
        unsigned char bytes[12];
        long alignment;
      } bits;
      unsigned char densities[96], thresholds[8];
      const int width = frame.right - frame.left;
      if (width > 96)
      {
        SetPenState(&pen);
        return false;
      }
      GrafPtr port;
      GetPort(&port);
      BitMap bitmap;
      bitmap.baseAddr = reinterpret_cast<Ptr>(bits.bytes);
      bitmap.rowBytes = 2 * ((width + 15) / 16);
      SetRect(&bitmap.bounds, 0, 0, width, 1);
      for (int y = frame.top; y < frame.bottom; ++y)
      {
        std::memset(bits.bytes, 0, sizeof(bits.bytes));
        CardShadowDensityRun(style, body, frame.left, y, width, densities);
        CardShadowStencilThresholdRow(y, thresholds);
        for (int x = 0; x < width; ++x)
          if (densities[x] > thresholds[(frame.left + x) & 7])
            bits.bytes[x / 8] |= 0x80 >> (x & 7);
        Rect row;
        SetRect(&row, frame.left, y, frame.right, y + 1);
        CopyBits(&bitmap, &port->portBits, &bitmap.bounds, &row, srcOr, 0);
      }
      Rect nativeBody;
      SetRect(&nativeBody, body.x, body.y, body.x + body.width, body.y + body.height);
      PenNormal();
      FrameRoundRect(&nativeBody, 12, 12);
      SetPenState(&pen);
      return true;
    }
    /** One byte per pixel; header gives window origin and dimensions. */
    static void dump(std::FILE *log, const Rect &frame, const unsigned char *pixels, const char *label)
    {
      std::fprintf(log,
                   "card-dump %s x=%d y=%d w=%d h=%d\r",
                   label,
                   frame.left,
                   frame.top,
                   frame.right - frame.left,
                   frame.bottom - frame.top);
      for (int y = frame.top; y < frame.bottom; ++y)
      {
        for (int x = frame.left; x < frame.right; ++x)
          std::fputc(*pixels++ ? '1' : '0', log);
        std::fputc('\r', log);
      }
    }
    /** Independent oracle: actual whole body, no local reconstruction or band clips. */
    static bool reference(const Rect &frame)
    {
      RgnHandle bodyRegion = NewRgn(), shadowRegion = NewRgn();
      if (!bodyRegion || !shadowRegion)
      {
        if (bodyRegion)
          DisposeRgn(bodyRegion);
        if (shadowRegion)
          DisposeRgn(shadowRegion);
        return false;
      }
      Rect body = frame;
      body.right -= 3;
      body.bottom -= 3;
      PenState pen;
      GetPenState(&pen);
      PenNormal();
      OpenRgn();
      FrameRoundRect(&body, 12, 12);
      CloseRgn(bodyRegion);
      Rect shadow = body;
      OffsetRect(&shadow, 3, 3);
      OpenRgn();
      FrameRoundRect(&shadow, 12, 12);
      CloseRgn(shadowRegion);
      DiffRgn(shadowRegion, bodyRegion, shadowRegion);
      PenPat(&loka::toolbox::kToolboxGradientPatterns[4].pattern);
      PenMode(patOr);
      PaintRgn(shadowRegion);
      PenNormal();
      FrameRoundRect(&body, 12, 12);
      SetPenState(&pen);
      DisposeRgn(shadowRegion);
      DisposeRgn(bodyRegion);
      return true;
    }

  private:
    CardNode card_;
    static int Place(void *, Node *node, const LayoutState &offer)
    {
      if (!node->asCardBandNode())
        return offer.y + offer.height;
      LayoutState state = offer;
      node->getContext()->layout(0, state);
      return state.y;
    }
  };

  /** Samples only the title interior, excluding the standard CDEF border. */
  class ButtonTitlePixels
  {
  public:
    ButtonTitlePixels() : pixels_() {}
    void capture(const Rect &rect)
    {
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 48; ++x)
          this->pixels_[y * 48 + x] = GetPixel(rect.left + 8 + x, rect.top + 3 + y) != 0;
    }
    int erasedInk(const Rect &rect) const
    {
      int count = 0;
      for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 48; ++x)
          if (this->pixels_[y * 48 + x] && !GetPixel(rect.left + 8 + x, rect.top + 3 + y))
            ++count;
      return count;
    }
  private:
    bool pixels_[48 * 8];
  };

  class PopupExactNode;
  typedef BoundaryPropsFor<PopupExactNode> PopupExactProps;
  /** One Boundary owns the popup inputs and the siblings visited on each write. */
  class PopupExactNode : public StdCompositionBoundaryNodeBase<PopupExactProps>
  {
  public:
    typedef PopupExactProps::TypeTag TypeTag;
    explicit PopupExactNode(const PopupExactProps &props)
        : StdCompositionBoundaryNodeBase<PopupExactProps>(props)
    {
      this->items_.push_back(loka::core::String::Literal(""));
      this->items_.push_back(loka::core::String::Literal("HHHH"));
      this->state(this->selection_, 0);
      this->state(this->popupShown_, true);
      this->state(this->text_, loka::core::String::Literal("Sibling ink"));
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(4, 4, 12, 12);
      this->state(this->model_, model);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(Box().size(180, 140)
                          << (ScrollView()
                              << (Column()
                                  << (Show(*this->popupShown_.state())
                                      << PopupMenu(this->items_).selectedIndex(this->selection_)
                                          .TEST_ID("PopupExact.Popup"))
                                  << Text(this->text_.state())
                                  << RectSurface(this->model_.state()).size(150, 40)
                                      .TEST_ID("PopupExact.Surface"))));
    }
    void writeSibling()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->text_.set(loka::core::String::Literal("Changed ink"));
    }
    void hidePopup()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->popupShown_.set(false);
    }
    void selectNext()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->selection_.set(1);
    }
  private:
    loka::Vector<loka::core::String> items_;
    NodeState<int> selection_;
    NodeState<bool> popupShown_;
    NodeState<loka::core::String> text_;
    NodeState<RectSurfaceModel> model_;
  };

  /** Both distant drawers share the Boundary whose answers are collected. */
  template <bool Popup>
  class HistoryNode : public StdCompositionBoundaryNodeBase<BoundaryPropsFor<HistoryNode<Popup> > >
  {
  public:
    typedef BoundaryPropsFor<HistoryNode<Popup> > Props;
    typedef typename Props::TypeTag TypeTag;
    explicit HistoryNode(const Props &props) : StdCompositionBoundaryNodeBase<Props>(props)
    {
      this->state(this->first_, loka::core::String::Literal("AAAA"));
      this->state(this->second_, loka::core::String::Literal("    "));
      this->state(this->selection_, 0);
      this->items_.push_back(loka::core::String::Literal("    "));
      this->items_.push_back(loka::core::String::Literal("HHHH"));
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(4, 4, 12, 12);
      this->state(this->model_, model);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      Column drawers = Column() << Text(this->first_.state()).TEST_ID("History.A")
                               << Box().size(150, 48);
      if (Popup)
        drawers << PopupMenu(this->items_).selectedIndex(this->selection_).TEST_ID("History.B");
      else
        drawers << Text(this->second_.state()).TEST_ID("History.B");
      drawers << Box().size(150, 24)
              << RectSurface(this->model_.state()).size(150, 24)
                  .TEST_ID("History.Surface");
      composition.declare(ZStack() << drawers);
    }
    void writeFirst()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->first_.set(loka::core::String::Literal("HHHH"));
    }
    void writeSecond()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      if (Popup)
        this->selection_.set(1);
      else
        this->second_.set(loka::core::String::Literal("HHHH"));
    }
  private:
    NodeState<loka::core::String> first_;
    NodeState<loka::core::String> second_;
    NodeState<int> selection_;
    NodeState<RectSurfaceModel> model_;
    loka::Vector<loka::core::String> items_;
  };

  /** Snapshot two finite face rows in the fixture's window coordinates. */
  class PopupFaceRows
  {
  public:
    PopupFaceRows() : frame_(), label_() {}
    void capture(const Rect &rect)
    {
      for (int x = 0; x < 128; ++x)
      {
        this->frame_[x] = GetPixel(rect.left + x, rect.top + 7) != 0;
        this->label_[x] = GetPixel(rect.left + x, rect.top + 9) != 0;
      }
    }
    bool matches(const Rect &rect, bool label) const
    {
      for (int x = 0; x < 128; ++x)
        if ((label ? this->label_[x] : this->frame_[x])
            != (GetPixel(rect.left + x, rect.top + (label ? 9 : 7)) != 0))
          return false;
      return true;
    }
  private:
    bool frame_[128];
    bool label_[128];
  };

  class CardReplayNode;
  typedef BoundaryPropsFor<CardReplayNode> CardReplayProps;
  class CardReplayNode : public StdCompositionBoundaryNodeBase<CardReplayProps>
  {
  public:
    typedef CardReplayProps::TypeTag TypeTag;
    explicit CardReplayNode(const CardReplayProps &props)
        : StdCompositionBoundaryNodeBase<CardReplayProps>(props)
    {
      this->state(this->width_, 96);
      this->state(this->offset_, loka::core::Frame(0, 0, 0, 0));
    }
    virtual void composeNode(NodeComposition &composition)
    {
      // A width-only Box is a Row seat claim (#576); as the root it would pass the full window width through.
      composition.declare(Column() << (Row() << (Box().width(this->width_.state())
                                                 << (Card().TEST_ID("Card.Replay") << Box().size(1, 63))))
                                   << (Box().size(180, 70)
                                       << (ScrollView(this->offset_).TEST_ID("Card.Viewport")
                                           << (Column() << (Card(CardStyle(CARD_SHADOW_WIDE)).TEST_ID("Card.Scrolled")
                                                            << Box().size(220, 80))
                                                        << Box().size(220, 80)))));
    }
    void scroll()
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->offset_.set(loka::core::Frame(3, 5, 0, 0));
    }
    void narrow(bool narrow)
    {
      loka::core::StateTrackerGuard guard(this->tracker());
      this->width_.set(narrow ? 14 : 96);
    }

  private:
    NodeState<int> width_;
    NodeState<loka::core::Frame> offset_;
  };

  /** Snapshot owned by the finite attached-Card replay fixture. */
  class CardReplayPixels
  {
  public:
    CardReplayPixels()
        : frame_(),
          pixels_()
    {
    }
    /** Setup checks in log order; capture() returns the first one that fails, or READY. */
    enum Check
    {
      CHECK_FOUND,
      CHECK_BANDS_EXACT,
      CHECK_FRAME_96X72,
      CHECK_OLD_INK,
      READY
    };
    Check capture(Node *card)
    {
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      PaintDamage top = {}, bottom = {};
      int exact = 0;
      for (Node *child = card->asNestable()->childrenHead(); child; child = child->nextInComposition)
      {
        if (!child->asCardBandNode())
          continue;
        if (!child->getContext())
          return CHECK_BANDS_EXACT;
        const PaintAnswer answer = static_cast<NativeNodeContext *>(child->getContext())->queryPaintDamage(query);
        if (answer.kind != PAINT_ANSWER_EXACT)
          return CHECK_BANDS_EXACT;
        ++exact;
        if (child->asCardBandNode()->props.role == CARD_BAND_TOP)
          top = answer.damage;
        if (child->asCardBandNode()->props.role == CARD_BAND_BOTTOM)
          bottom = answer.damage;
      }
      if (exact != CARD_BAND_COUNT)
        return CHECK_BANDS_EXACT;
      SetRect(&this->frame_, top.x, top.y, bottom.x + bottom.width, bottom.y + bottom.height);
      if (this->frame_.right - this->frame_.left != 96 || this->frame_.bottom - this->frame_.top != 72)
        return CHECK_FRAME_96X72;
      CardPixels::capture(this->frame_, this->pixels_);
      for (unsigned i = 0; i < sizeof(this->pixels_); ++i)
        if (this->pixels_[i])
          return READY;
      return CHECK_OLD_INK;
    }
    /** Reuse the completed replay snapshot for one visible pre-scroll row. */
    bool captureScrollRow(Node *card)
    {
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      if (!card || !card->asNestable())
        return false;
      for (Node *child = card->asNestable()->childrenHead(); child; child = child->nextInComposition)
        if (child->asCardBandNode() && child->getContext() && child->asCardBandNode()->props.role == CARD_BAND_TOP)
        {
          const PaintAnswer answer = static_cast<NativeNodeContext *>(child->getContext())->queryPaintDamage(query);
          if (answer.kind != PAINT_ANSWER_EXACT || answer.damage.width <= 0 || answer.damage.height <= 0
              || answer.damage.width > static_cast<int>(sizeof(this->pixels_)))
            return false;
          SetRect(&this->frame_,
                  answer.damage.x,
                  answer.damage.y + answer.damage.height - 1,
                  answer.damage.x + answer.damage.width,
                  answer.damage.y + answer.damage.height);
          CardPixels::capture(this->frame_, this->pixels_);
          return true;
        }
      return false;
    }
    bool scrollRowChanged() const
    {
      for (int x = 0; x < this->frame_.right - this->frame_.left; ++x)
        if ((GetPixel(this->frame_.left + x, this->frame_.top) != 0) != (this->pixels_[x] != 0))
          return true;
      return false;
    }
    bool matches(bool disabled) const
    {
      for (short y = 0; y < 72; ++y)
        for (short x = 0; x < 96; ++x)
          if ((GetPixel(this->frame_.left + x, this->frame_.top + y) != 0)
              != (disabled ? false : this->pixels_[y * 96 + x] != 0))
            return false;
      return true;
    }

  private:
    Rect frame_;
    unsigned char pixels_[96 * 72];
  };

  class RibbonFrameNode;
  typedef BoundaryPropsFor<RibbonFrameNode> RibbonFrameProps;
  class RibbonFrameNode : public StdCompositionBoundaryNodeBase<RibbonFrameProps>
  {
  public:
    typedef RibbonFrameProps::TypeTag TypeTag;
    explicit RibbonFrameNode(const RibbonFrameProps &props)
        : StdCompositionBoundaryNodeBase<RibbonFrameProps>(props)
    {
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(RibbonControl().testId("Ribbon.Frame") << (RibbonGroup("Caption") << RibbonItem("Button")));
    }
  };

  class SurfaceBoundsNode;
  typedef BoundaryPropsFor<SurfaceBoundsNode> SurfaceBoundsProps;
  /** A fixed model overhangs the declared surface by eight pixels on two sides. */
  class SurfaceBoundsNode : public StdCompositionBoundaryNodeBase<SurfaceBoundsProps>
  {
  public:
    typedef SurfaceBoundsProps::TypeTag TypeTag;
    explicit SurfaceBoundsNode(const SurfaceBoundsProps &props)
        : StdCompositionBoundaryNodeBase<SurfaceBoundsProps>(props)
    {
      RectSurfaceModel model;
      model.rectCount = 1;
      model.rects[0] = RectSprite(52, 32, 16, 16);
      this->state(this->model_, model);
    }
    virtual void composeNode(NodeComposition &composition)
    {
      // Keep six pixels of Text inside the viewport after adding its gap.
      composition.declare(Column()
                          << (Box().size(180, 40)
                              << RectSurface(this->model_.state()).size(60, 40))
                          << (Box().size(180, 54)
                              << (ScrollView()
                                  << (Column() << Box().size(150, 48 - kRootGap)
                                      << Text("HHHH").TEST_ID("PaintDamage.ClippedText")))));
    }
  private:
    NodeState<RectSurfaceModel> model_;
  };

  enum DrawerFixture { CELL_VIEWPORT, POPUP_VIEWPORT, CELL_OVERFLOW };

  template <DrawerFixture Kind>
  class DrawerDamageNode : public StdCompositionBoundaryNodeBase<BoundaryPropsFor<DrawerDamageNode<Kind> > >
  {
  public:
    typedef BoundaryPropsFor<DrawerDamageNode<Kind> > Props;
    typedef typename Props::TypeTag TypeTag;
    explicit DrawerDamageNode(const Props &props) : StdCompositionBoundaryNodeBase<Props>(props) {}
    virtual void composeNode(NodeComposition &composition)
    {
      // Preserve 14 visible pixels of the third Cell across two seat gaps.
      if (Kind == CELL_VIEWPORT)
        composition.declare(Row() << (Box().size(180, 62 + 2 * kRootGap)
                            << (ScrollView().TEST_ID("Drawer.Viewport")
                                << (Column() << (Box().size(100, 24) << loka::app::Cell("One"))
                                    << (Box().size(100, 24) << loka::app::Cell("Two"))
                                    << (Box().size(100, 24) << loka::app::Cell("Three"))))) << Text("Rail"));
      else if (Kind == POPUP_VIEWPORT)
        composition.declare(Box().size(96, 62)
                            << (ScrollView().TEST_ID("Drawer.Viewport") << PopupMenu()));
      else
        composition.declare(Row() << Box().size(42, 24)
                            << (Box().size(8, 24) << loka::app::Cell("WWWWWWW")));
    }
  };

  /** Finite pixel snapshot owned by the fixture, never a production cache. */
  class PopupRow
  {
  public:
    PopupRow() : pixels_() {}
    void capture()
    {
      for (short x = kRootX; x < kRootX + 144; ++x)
        this->pixels_[x - kRootX] = GetPixel(x, kRootY + 7) != 0;
    }
    bool matches() const
    {
      for (short x = kRootX; x < kRootX + 144; ++x)
        if (this->pixels_[x - kRootX] != (GetPixel(x, kRootY + 7) != 0))
          return false;
      return true;
    }
  private:
    bool pixels_[144];
  };

  struct FindEdit : public IPaintResidentVisitor
  {
    FindEdit() : context(0) {}
    virtual void visit(Node *node, NodeContext *native, BoundaryNode *)
    {
      if (node->testId() == "PaintDamage.Edit")
        this->context = static_cast<ToolboxEditTextContext *>(native);
    }
    ToolboxEditTextContext *context;
  };

  /** Fixture-local answers, including the first refusal's kind to pair with
      PaintApplyVerdict::refusalReason(). No observation survives a collection. */
  struct ProbeSource
  {
    /** Exact damage may share a conservative, non-erasing destination only when true. */
    enum { kMergesExactDamage = 0 };
    ProbeSource() : siblingX(0), siblingY(0), foundSibling(false), first(0), firstDamage(), refusingKind(NODE_KIND_UNKNOWN) {}
    bool queryPaintAnswer(Node *node, NodeContext *context, const PaintQuery &query, PaintAnswer &answer)
    {
      if (node->kind() != NODE_KIND_RECT_SURFACE && node->kind() != NODE_KIND_TEXT
          && node->kind() != NODE_KIND_EDIT_TEXT && node->kind() != NODE_KIND_POPUP_MENU
          && node->kind() != NODE_KIND_BUTTON)
        return false;
      answer = context ? static_cast<NativeNodeContext *>(context)->queryPaintDamage(query)
                       : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
      if (answer.kind == PAINT_ANSWER_REFUSED && this->refusingKind == NODE_KIND_UNKNOWN)
        this->refusingKind = node->kind();
      if (node->testId() == "PaintDamage.First" && answer.kind == PAINT_ANSWER_EXACT)
      {
        this->first = context;
        this->firstDamage = answer.damage;
      }
      if (node->testId() == "PaintDamage.Sibling" && answer.kind == PAINT_ANSWER_EXACT)
      {
        this->siblingX = answer.damage.x + 8;
        this->siblingY = answer.damage.y + 8;
        this->foundSibling = true;
      }
      return true;
    }
    int siblingX, siblingY;
    bool foundSibling;
    NodeContext *first;
    PaintDamage firstDamage;
    NodeKind refusingKind;
  };

  /** Window borrows remain inside the configuration that owns this finite run. */
  class ObservedWindowDefinition : public WindowDefinition<WindowProps>
  {
  public:
    ObservedWindowDefinition(const WindowProps &props, Window **result)
        : WindowDefinition<WindowProps>(props), result_(result) {}
    virtual WindowDefinitionBase *clone() const
    {
      return new (std::nothrow) ObservedWindowDefinition(*this);
    }
    virtual Window *create(PlatformContext *context) const
    {
      Window *window = WindowDefinition<WindowProps>::create(context);
      *this->result_ = window;
      return window;
    }
  private:
    Window **result_;
  };

  class PaintDamageConfig : public AppConfigurable
  {
  public:
    explicit PaintDamageConfig(PlatformContext *context)
        : AppConfigurable(context),
          app_(0),
          node_(0),
          composited_(0),
          edit_(0),
          log_(0),
          phase_(SETTLE),
          result_(0),
          initial_(),
          marker_(),
          scrollTextMarker_(),
          gate_(false),
          editGeometry_(),
          paintWindow_(0),
          compositedWindow_(0),
          editWindow_(0),
          plain_(0),
          plainWindow_(0),
          boundsWindow_(0),
          cellWindow_(0),
          popupWindow_(0),
          overflowWindow_(0),
          imageWindow_(0),
          imageVacatedWindow_(0),
          popupRow_(),
          editExact_(0),
          editExactWindow_(0),
          editZStack_(0),
          editZStackWindow_(0),
          popupExact_(0),
          popupExactWindow_(0),
          popupFaceRows_(),
          history_(0),
          historyWindow_(0),
          popupHistory_(0),
          popupHistoryWindow_(0),
          buttonEnabled_(0),
          buttonEnabledWindow_(0),
          buttonLabel_(0),
          buttonLabelWindow_(0),
          buttonTitlePixels_(),
          textWidth_(0),
          textWidthWindow_(0),
          textStyleChange_(0),
          textStyleChangeWindow_(0),
          textStyleBefore_(),
          offscreen_(0),
          offscreenWindow_(0),
          ribbonWindow_(0),
          cardWindow_(0),
          card_(0),
          cardReplay_(),
          scrollStart_(0)
    {
      if (loka::platform::file::ResolveApplicationSidecar(
              loka::file::File::Application() << loka::file::File("LOG.TXT"), this->file_))
        this->log_ = loka::platform::file::OpenWriteTruncate(this->file_);
      if (!this->log_)
        this->result_ = 1;
      else if (std::fprintf(this->log_, "paint-damage BEGIN\r") < 0
               || !loka::platform::file::FlushWrite(this->log_, this->file_))
        this->result_ = 1;
    }
    virtual ~PaintDamageConfig()
    {
      if (this->log_)
        std::fclose(this->log_);
    }
    void setApp(App *app) { this->app_ = app; }
    int exitCode() const { return this->result_; }
    virtual void compose(AppComposition &composition)
    {
      composition << ObservedWindowDefinition(WindowProps().frame(50, 50, 280, 220).title("Paint damage")
                               .scene(loka::scenario_tests::ObservedMainDefinition<ViewportDamageNode::Props, ViewportDamageNode>(
                                   ViewportDamageNode::Props(), &this->node_))
                               .visible(true).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->paintWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(50, 285, 280, 165).title("Exact damage")
                               .scene(loka::scenario_tests::ObservedMainDefinition<PlainDamageNode::Props, PlainDamageNode>(
                                   PlainDamageNode::Props(), &this->plain_))
                               .visible(true).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->plainWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 50, 220, 160).title("Composited damage")
                               .scene(loka::scenario_tests::ObservedMainDefinition<CompositedDamageProps, CompositedDamageNode>(
                                   CompositedDamageProps(), &this->composited_))
                               .visible(true).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->compositedWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 160).title("Edit replay")
                               .scene(loka::scenario_tests::ObservedMainDefinition<EditDamageProps, EditDamageNode>(
                                   EditDamageProps(), &this->edit_))
                               .visible(true).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->editWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 160).title("Surface bounds")
                               .scene(loka::scenario_tests::ObservedMainDefinition<SurfaceBoundsProps, SurfaceBoundsNode>(
                                   SurfaceBoundsProps(), 0))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->boundsWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 160).title("Cell replay")
                               .scene(loka::scenario_tests::ObservedMainDefinition<DrawerDamageNode<CELL_VIEWPORT>::Props, DrawerDamageNode<CELL_VIEWPORT> >(
                                   DrawerDamageNode<CELL_VIEWPORT>::Props(), 0))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->cellWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 160).title("Popup replay")
                               .scene(loka::scenario_tests::ObservedMainDefinition<DrawerDamageNode<POPUP_VIEWPORT>::Props, DrawerDamageNode<POPUP_VIEWPORT> >(
                                   DrawerDamageNode<POPUP_VIEWPORT>::Props(), 0))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->popupWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 160).title("Cell ink")
                               .scene(loka::scenario_tests::ObservedMainDefinition<DrawerDamageNode<CELL_OVERFLOW>::Props, DrawerDamageNode<CELL_OVERFLOW> >(
                                   DrawerDamageNode<CELL_OVERFLOW>::Props(), 0))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->overflowWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 160).title("Image vacated")
                               .scene(loka::scenario_tests::ObservedMainDefinition<ImageVacatedProps, ImageVacatedNode>(
                                   ImageVacatedProps(), 0))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->imageVacatedWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 160).title("Image replay")
                               .scene(loka::scenario_tests::ObservedMainDefinition<ImageOverlapProps, ImageOverlapNode>(
                                   ImageOverlapProps(), 0))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->imageWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 180).title("Edit exact")
                               .scene(loka::scenario_tests::ObservedMainDefinition<EditExactProps, EditExactNode>(
                                   EditExactProps(), &this->editExact_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->editExactWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 180).title("Edit ZStack order")
                               .scene(loka::scenario_tests::ObservedMainDefinition<EditZStackProps, EditZStackNode>(
                                   EditZStackProps(), &this->editZStack_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->editZStackWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 180).title("Popup exact")
                               .scene(loka::scenario_tests::ObservedMainDefinition<PopupExactProps, PopupExactNode>(
                                   PopupExactProps(), &this->popupExact_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->popupExactWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 220).title("History replay")
                               .scene(loka::scenario_tests::ObservedMainDefinition<HistoryNode<false>::Props, HistoryNode<false> >(
                                   HistoryNode<false>::Props(), &this->history_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->historyWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 220).title("History replay")
                               .scene(loka::scenario_tests::ObservedMainDefinition<HistoryNode<true>::Props, HistoryNode<true> >(
                                   HistoryNode<true>::Props(), &this->popupHistory_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->popupHistoryWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 180).title("Button enabled exact")
                               .scene(loka::scenario_tests::ObservedMainDefinition<ButtonExactProps, ButtonExactNode>(
                                   ButtonExactProps(), &this->buttonEnabled_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->buttonEnabledWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 180).title("Button label exact")
                               .scene(loka::scenario_tests::ObservedMainDefinition<ButtonExactProps, ButtonExactNode>(
                                   ButtonExactProps(), &this->buttonLabel_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->buttonLabelWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 260, 180).title("Text width refusal")
                               .scene(loka::scenario_tests::ObservedMainDefinition<TextWidthProps, TextWidthNode>(
                                   TextWidthProps(), &this->textWidth_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->textWidthWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 180).title("Text style layout")
                               .scene(loka::scenario_tests::ObservedMainDefinition<TextStyleChangeProps, TextStyleChangeNode>(
                                   TextStyleChangeProps(), &this->textStyleChange_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->textStyleChangeWindow_);
      composition << ObservedWindowDefinition(WindowProps().frame(350, 250, 220, 180).title("Column overflow exact")
                               .scene(loka::scenario_tests::ObservedMainDefinition<OffscreenDamageProps, OffscreenDamageNode>(
                                   OffscreenDamageProps(), &this->offscreen_))
                               .visible(false).idlePolicy(IdlePolicy::everyTick())
                               .onIdle(&PaintDamageConfig::DispatchIdle, this), &this->offscreenWindow_);
      composition << ObservedWindowDefinition(
          WindowProps()
              .frame(350, 250, 220, 180)
              .title("Ribbon frame")
              .scene(loka::scenario_tests::ObservedMainDefinition<RibbonFrameProps, RibbonFrameNode>(RibbonFrameProps(),
                                                                                                     0))
              .visible(false)
              .idlePolicy(IdlePolicy::everyTick())
              .onIdle(&PaintDamageConfig::DispatchIdle, this),
          &this->ribbonWindow_);
      composition << ObservedWindowDefinition(
          WindowProps()
              .frame(350, 250, 220, 180)
              .title("Card replay")
              .scene(loka::scenario_tests::ObservedMainDefinition<CardReplayProps, CardReplayNode>(CardReplayProps(),
                                                                                                   &this->card_))
              .visible(false)
              .idlePolicy(IdlePolicy::everyTick())
              .onIdle(&PaintDamageConfig::DispatchIdle, this),
          &this->cardWindow_);
    }

  private:
    enum Phase { SETTLE, WRITE, CHECK, PLAIN_WRITE, PLAIN_CHECK, INVALIDATED_WRITE, INVALIDATED_CHECK,
                 COMPOSITED_WRITE, COMPOSITED_CHECK, EDIT_WRITE, EDIT_CHECK, BOUNDS_SHOW, BOUNDS_CHECK, CELL_SHOW, CELL_REPLAY, CELL_CHECK,
                 POPUP_SHOW, POPUP_REPLAY, POPUP_CHECK, OVERFLOW_SHOW, OVERFLOW_REPLAY, OVERFLOW_CHECK, IMAGE_SHOW, IMAGE_WRITE, IMAGE_CHECK, IMAGE_VACATED_SHOW, IMAGE_VACATED_WRITE, IMAGE_VACATED_CHECK, EDIT_EXACT_SHOW, EDIT_EXACT_WRITE, EDIT_EXACT_CHECK, EDIT_ZSTACK_SHOW, EDIT_ZSTACK_WRITE, EDIT_ZSTACK_CHECK, POPUP_EXACT_SHOW, POPUP_SIBLING_WRITE, POPUP_SIBLING_CHECK, POPUP_SELECTION_CHECK, HISTORY_SHOW, HISTORY_FIRST, HISTORY_SECOND, HISTORY_CHECK,
                 POPUP_HISTORY_SHOW, POPUP_HISTORY_FIRST, POPUP_HISTORY_SECOND, POPUP_HISTORY_CHECK, BUTTON_ENABLED_SHOW, BUTTON_ENABLED_WRITE, BUTTON_ENABLED_CHECK,
                 BUTTON_LABEL_SHOW, BUTTON_LABEL_WRITE, BUTTON_LABEL_CHECK,
                 TEXT_WIDTH_SHOW, TEXT_WIDTH_WRITE, TEXT_WIDTH_CHECK, TEXT_WIDTH_RELAYOUT,
                 TEXT_STYLE_SHOW, TEXT_STYLE_WRITE, TEXT_STYLE_CHECK,
                 OFFSCREEN_SHOW,
                 OFFSCREEN_WRITE,
                 OFFSCREEN_SIBLING_CHECK,
                 OFFSCREEN_TEXT_CHECK,
                 OFFSCREEN_HIDDEN_CHECK,
                 OFFSCREEN_REVEAL_CHECK,
                 OFFSCREEN_REVEALED_WRITE_CHECK,
                 OFFSCREEN_DETACH_CHECK,
                 RIBBON_SHOW,
                 RIBBON_CHECK,
                 RIBBON_REPAINT,
                 CARD_SHOW,
                 CARD_CAPTURE,
                 CARD_DISABLED,
                 CARD_RESTORED,
                 CARD_SCROLLED,
                 COMPLETE
    };
    App *app_;
    ViewportDamageNode *node_;
    CompositedDamageNode *composited_;
    EditDamageNode *edit_;
    loka::platform::file::FileHandle file_;
    std::FILE *log_;
    Phase phase_;
    int result_;
    ToolboxSceneDebugStats initial_;
    Point marker_;
    Point scrollTextMarker_;
    bool gate_;
    ToolboxScenePlatformController::EditTextGeometry editGeometry_;

    Window *paintWindow_;
    Window *compositedWindow_;
    Window *editWindow_;
    PlainDamageNode *plain_;
    Window *plainWindow_;
    Window *boundsWindow_;
    Window *cellWindow_;
    Window *popupWindow_;
    Window *overflowWindow_;
    Window *imageWindow_;
    Window *imageVacatedWindow_;
    PopupRow popupRow_;
    EditExactNode *editExact_;
    Window *editExactWindow_;
    EditZStackNode *editZStack_;
    Window *editZStackWindow_;
    PopupExactNode *popupExact_;
    Window *popupExactWindow_;
    PopupFaceRows popupFaceRows_;
    HistoryNode<false> *history_;
    Window *historyWindow_;
    HistoryNode<true> *popupHistory_;
    Window *popupHistoryWindow_;
    ButtonExactNode *buttonEnabled_;
    Window *buttonEnabledWindow_;
    ButtonExactNode *buttonLabel_;
    Window *buttonLabelWindow_;
    ButtonTitlePixels buttonTitlePixels_;
    TextWidthNode *textWidth_;
    Window *textWidthWindow_;
    TextStyleChangeNode *textStyleChange_;
    Window *textStyleChangeWindow_;
    TextStyleChangeBefore textStyleBefore_;
    OffscreenDamageNode *offscreen_;
    Window *offscreenWindow_;
    Window *ribbonWindow_;
    Window *cardWindow_;
    CardReplayNode *card_;
    CardReplayPixels cardReplay_;
    unsigned long scrollStart_;

    void recordArm(const char *name, bool pass, Phase next)
    {
      if (!pass)
        this->result_ = 1;
      if (std::fprintf(this->log_, "%s %s\r", name, pass ? "PASS" : "FAIL") < 0
          || !loka::platform::file::FlushWrite(this->log_, this->file_))
        this->result_ = 1;
      this->phase_ = next;
    }

    /** Only the active Window receives idle. Any active window drives the
        same finite sequence over the explicit fixture borrows. */
    static void DispatchIdle(Window *, double elapsed, void *data)
    {
      PaintDamageConfig *self = static_cast<PaintDamageConfig *>(data);
      Window *target = 0;
      switch (self->phase_)
      {
      case SETTLE: case WRITE: case CHECK:
        target = self->paintWindow_;
        break;
      case PLAIN_WRITE: case PLAIN_CHECK: case INVALIDATED_WRITE: case INVALIDATED_CHECK:
        target = self->plainWindow_;
        break;
      case COMPOSITED_WRITE: case COMPOSITED_CHECK:
        target = self->compositedWindow_;
        break;
      case EDIT_WRITE: case EDIT_CHECK:
        target = self->editWindow_;
        break;
      case BOUNDS_SHOW: case BOUNDS_CHECK:
        target = self->boundsWindow_;
        break;
      case CELL_SHOW: case CELL_REPLAY: case CELL_CHECK:
        target = self->cellWindow_;
        break;
      case POPUP_SHOW: case POPUP_REPLAY: case POPUP_CHECK:
        target = self->popupWindow_;
        break;
      case OVERFLOW_SHOW: case OVERFLOW_REPLAY: case OVERFLOW_CHECK:
        target = self->overflowWindow_;
        break;
      case IMAGE_VACATED_SHOW: case IMAGE_VACATED_WRITE: case IMAGE_VACATED_CHECK:
        target = self->imageVacatedWindow_;
        break;
      case IMAGE_SHOW: case IMAGE_WRITE: case IMAGE_CHECK:
        target = self->imageWindow_;
        break;
      case EDIT_EXACT_SHOW: case EDIT_EXACT_WRITE: case EDIT_EXACT_CHECK:
        target = self->editExactWindow_;
        break;
      case EDIT_ZSTACK_SHOW: case EDIT_ZSTACK_WRITE: case EDIT_ZSTACK_CHECK:
        target = self->editZStackWindow_;
        break;
      case POPUP_EXACT_SHOW: case POPUP_SIBLING_WRITE: case POPUP_SIBLING_CHECK: case POPUP_SELECTION_CHECK:
        target = self->popupExactWindow_;
        break;
      case HISTORY_SHOW: case HISTORY_FIRST: case HISTORY_SECOND: case HISTORY_CHECK:
        target = self->historyWindow_;
        break;
      case POPUP_HISTORY_SHOW: case POPUP_HISTORY_FIRST: case POPUP_HISTORY_SECOND: case POPUP_HISTORY_CHECK:
        target = self->popupHistoryWindow_;
        break;
      case BUTTON_ENABLED_SHOW: case BUTTON_ENABLED_WRITE: case BUTTON_ENABLED_CHECK:
        target = self->buttonEnabledWindow_;
        break;
      case BUTTON_LABEL_SHOW: case BUTTON_LABEL_WRITE: case BUTTON_LABEL_CHECK:
        target = self->buttonLabelWindow_;
        break;
      case TEXT_WIDTH_SHOW: case TEXT_WIDTH_WRITE: case TEXT_WIDTH_CHECK: case TEXT_WIDTH_RELAYOUT:
        target = self->textWidthWindow_;
        break;
      case TEXT_STYLE_SHOW: case TEXT_STYLE_WRITE: case TEXT_STYLE_CHECK:
        target = self->textStyleChangeWindow_;
        break;
      case OFFSCREEN_SHOW: case OFFSCREEN_WRITE: case OFFSCREEN_SIBLING_CHECK:
      case OFFSCREEN_TEXT_CHECK: case OFFSCREEN_HIDDEN_CHECK: case OFFSCREEN_REVEAL_CHECK: case OFFSCREEN_REVEALED_WRITE_CHECK: case OFFSCREEN_DETACH_CHECK:
        target = self->offscreenWindow_;
        break;
      case RIBBON_SHOW:
      case RIBBON_CHECK:
      case RIBBON_REPAINT:
        target = self->ribbonWindow_;
        break;
      case CARD_SHOW:
      case CARD_CAPTURE:
      case CARD_DISABLED:
      case CARD_RESTORED:
      case CARD_SCROLLED:
        target = self->cardWindow_;
        break;
      case COMPLETE:
        return;
      }
      ToolboxWindow *native = target ? target->asToolboxWindow() : 0;
      if (!native)
      {
        self->finish(false);
        return;
      }
      // One bounded delivery per phase; never wait for a permanently pending
      // flag. Native drawing may itself publish work for the following phase.
      target->flushSceneInvalidation();
      if (target == self->offscreenWindow_ && self->offscreen_)
      {
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        PaintAnswerBuffer<> answers;
        ProbeSource source;
        const PaintApplyVerdict verdict = CollectPaintAnswers(*self->offscreen_, query, answers, source);
        native->flushInvalidate();
        OnOffscreenIdle(target, self, verdict, source.refusingKind);
        return;
      }
      native->flushInvalidate();
      if (target == self->paintWindow_)
        OnPaintIdle(target, self->node_, self);
      else if (target == self->plainWindow_)
        OnPaintIdle(target, self->plain_, self);
      else if (target == self->compositedWindow_)
        OnCompositedIdle(target, elapsed, data);
      else if (target == self->editWindow_)
        OnEditIdle(target, elapsed, data);
      else if (target == self->boundsWindow_)
        OnBoundsIdle(target, self);
      else if (target == self->editExactWindow_)
        OnEditExactIdle(target, self);
      else if (target == self->editZStackWindow_)
        OnEditZStackIdle(target, self);
      else if (target == self->popupExactWindow_)
        OnPopupExactIdle(target, self);
      else if (target == self->historyWindow_)
        OnHistoryIdle(target, self->history_, self);
      else if (target == self->popupHistoryWindow_)
        OnHistoryIdle(target, self->popupHistory_, self);
      else if (target == self->buttonEnabledWindow_)
        OnButtonIdle(target, self->buttonEnabled_, false, self);
      else if (target == self->buttonLabelWindow_)
        OnButtonIdle(target, self->buttonLabel_, true, self);
      else if (target == self->textWidthWindow_)
        OnTextWidthIdle(target, self->textWidth_, self);
      else if (target == self->textStyleChangeWindow_)
        OnTextStyleIdle(target, self->textStyleChange_, self);
      else if (target == self->imageVacatedWindow_)
        OnImageVacatedIdle(target, self);
      else if (target == self->cardWindow_)
        OnCardIdle(target, self);
      else if (target == self->ribbonWindow_)
        OnRibbonIdle(target, self);
      else if (target == self->imageWindow_)
        OnImageIdle(target, self);
      else
        OnDrawerIdle(target, self);
    }

    static bool earlyBusyReturn(CursorOwner &owner, ToolboxScenePlatformController &controller)
    {
      BusyScope scope(owner);
      return controller.debugStatsForTesting().cursorDepth == 1
          && controller.debugStatsForTesting().cursorLastApplied == CursorOwner::WATCH;
    }

    void checkCursor(ToolboxWindow &window, ToolboxScenePlatformController &controller)
    {
      // No mouse-warp fixture exists here. This front fixture has no edit
      // fields, so its sampled hover is deterministically arrow.
      WindowPtr previousFront = FrontWindow();
      SelectWindow(window.window());
      CursorOwner &owner = window.toolboxApp()->cursorOwner();
      owner.reconcile();
      const unsigned long before = controller.debugStatsForTesting().cursorNativeApplies;
      const unsigned long entries = controller.debugStatsForTesting().cursorOuterEntries;
      const unsigned long exits = controller.debugStatsForTesting().cursorOuterExits;
      {
        BusyScope outer(owner);
        this->recordArm("cursor-outer-entry",
            controller.debugStatsForTesting().cursorNativeApplies == before + 1
            && controller.debugStatsForTesting().cursorLastApplied == CursorOwner::WATCH
            && controller.debugStatsForTesting().cursorDepth == 1
            && controller.debugStatsForTesting().cursorOuterEntries == entries + 1, SETTLE);
        {
          BusyScope inner(owner);
          this->recordArm("cursor-nested-entry",
              controller.debugStatsForTesting().cursorNativeApplies == before + 1
              && controller.debugStatsForTesting().cursorDepth == 2
              && controller.debugStatsForTesting().cursorOuterEntries == entries + 1, SETTLE);
        }
        this->recordArm("cursor-inner-exit",
            controller.debugStatsForTesting().cursorNativeApplies == before + 1
            && controller.debugStatsForTesting().cursorDepth == 1
            && controller.debugStatsForTesting().cursorOuterExits == exits, SETTLE);
        // Seed stale hover while busy: exit must sample the front window.
        owner.setHover(CursorOwner::HOVER_IBEAM);
      }
      this->recordArm("cursor-outer-exit-resamples",
          controller.debugStatsForTesting().cursorNativeApplies == before + 2
          && controller.debugStatsForTesting().cursorLastApplied == CursorOwner::ARROW
          && controller.debugStatsForTesting().cursorDepth == 0
          && controller.debugStatsForTesting().cursorOuterExits == exits + 1, SETTLE);
      const bool earlyWatch = earlyBusyReturn(owner, controller);
      this->recordArm("cursor-early-return",
          earlyWatch && controller.debugStatsForTesting().cursorDepth == 0
          && controller.debugStatsForTesting().cursorNativeApplies == before + 4
          && controller.debugStatsForTesting().cursorLastApplied == CursorOwner::ARROW, SETTLE);
      owner.apply(false);
      this->recordArm("cursor-unchanged-no-apply",
          controller.debugStatsForTesting().cursorNativeApplies == before + 4, SETTLE);
      // Use the production external-writer door, with unchanged effective arrow.
      owner.reconcile();
      this->recordArm("cursor-external-reconcile",
          controller.debugStatsForTesting().cursorNativeApplies == before + 5
          && controller.debugStatsForTesting().cursorLastApplied == CursorOwner::ARROW, SETTLE);
      if (previousFront)
        SelectWindow(previousFront);
      owner.reconcile();
    }

    void finish(bool pass)
    {
      this->phase_ = COMPLETE;
      pass = pass && this->result_ == 0;
      this->result_ = pass ? 0 : 1;
      if (std::fprintf(this->log_, "paint-damage %s\r", pass ? "PASS" : "FAIL") < 0
          || !loka::platform::file::FlushWrite(this->log_, this->file_))
        this->result_ = 1;
      this->app_->quit();
    }
    template <bool HasViewport>
    static void OnPaintIdle(Window *window, PaintDamageNode<HasViewport> *node, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window ? window->asToolboxWindow() : 0;
      if (!native || !window->scene() || !node)
      {
        self->finish(false);
        return;
      }
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(
          loka::dsl::testing::SceneTestAccess::platformController(*window->scene()));
      if (!controller)
      {
        self->finish(false);
        return;
      }
      if (self->phase_ == SETTLE)
      {
        self->checkCursor(*native, *controller);
        self->phase_ = WRITE;
        return;
      }
      if (self->phase_ == WRITE)
        self->recordArm("cursor-loop-end-depth",
            controller->debugStatsForTesting().cursorDepth == 0, WRITE);
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      if (self->phase_ == INVALIDATED_WRITE)
      {
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        PaintAnswerBuffer<> answers;
        ProbeSource source;
        CollectPaintAnswers(*node, query, answers, source);
        if (!source.first)
        {
          SetPort(previousPort);
          self->recordArm("unknown-history-setup", false, COMPOSITED_WRITE);
          return;
        }
        self->marker_.h = static_cast<short>(source.firstDamage.x + 18);
        self->marker_.v = static_cast<short>(source.firstDamage.y + 8);
        const bool oldPixelBlack = GetPixel(self->marker_.h, self->marker_.v) != 0;
        self->initial_ = controller->debugStatsForTesting();
        node->advance();
        SettleTurn();
        // Freeze the real exact request before invalidating presentation
        // history through the retained-props lifecycle door. The pending
        // window delivery must reconstruct all ground with region clipping on.
        loka::dsl::testing::SceneTestAccess::flushInvalidation(*window->scene());
        source.first->onPropsApplied();
        self->phase_ = INVALIDATED_CHECK;
        SetPort(previousPort);
        if (!oldPixelBlack)
          self->recordArm("unknown-history-setup", false, COMPOSITED_WRITE);
        return;
      }
      if (self->phase_ == INVALIDATED_CHECK)
      {
        const bool erased = GetPixel(self->marker_.h, self->marker_.v) == 0;
        const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
        const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
        const int dirty = stats.windowFlushDirtyCount - self->initial_.windowFlushDirtyCount;
        std::fprintf(self->log_, "unknown_history_old_pixel_erased=%d whole_window=%d dirty_flushes=%d source=%s\r",
                     erased ? 1 : 0, whole, dirty,
                     whole > 0 && stats.windowFullRequestSource ? stats.windowFullRequestSource : "none");
        SetPort(previousPort);
        self->recordArm("unknown-history", erased && whole == 0 && dirty > 0, COMPOSITED_WRITE);
        return;
      }
      if (self->phase_ == WRITE || self->phase_ == PLAIN_WRITE)
      {
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        PaintAnswerBuffer<> answers;
        ProbeSource source;
        const PaintApplyVerdict verdict = CollectPaintAnswers(*node, query, answers, source);
        BoundaryLocalApplyInfo info;
        info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
        self->gate_ = verdict.canSkipBroadPaint(info);
        if (!source.foundSibling)
        {
          SetPort(previousPort);
          self->recordArm(HasViewport ? "viewport-exact-setup" : "plain-exact-setup", false,
                          HasViewport ? PLAIN_WRITE : INVALIDATED_WRITE);
          return;
        }
        self->marker_.h = static_cast<short>(source.siblingX);
        self->marker_.v = static_cast<short>(source.siblingY);
        // Sample actual sibling ink: a correct full repaint preserves it.
        // An artificial pixel in white ground would reject that valid repaint.
        if (!GetPixel(self->marker_.h, self->marker_.v))
        {
          SetPort(previousPort);
          self->recordArm(HasViewport ? "viewport-exact-setup" : "plain-exact-setup", false,
                          HasViewport ? PLAIN_WRITE : INVALIDATED_WRITE);
          return;
        }
        if (HasViewport)
        {
          // Include the gap after the spacer and between/after the surfaces.
          // The viewport translates all content by the initial offset of 32.
          bool foundText = false;
          for (short y = kScrollTextY; y < kScrollTextY + 16 && !foundText; ++y)
            for (short x = kRootX; x < kRootX + 168 && !foundText; ++x)
              if (GetPixel(x, y))
              {
                self->scrollTextMarker_.h = x;
                self->scrollTextMarker_.v = y;
                foundText = true;
              }
          const bool scrolled = source.firstDamage.y == kScrollSurfaceY;
          const bool oldBlack = GetPixel(kRootX + 8, kScrollSurfaceY + 8) != 0;
          const bool newWhite = GetPixel(kRootX + 20, kScrollSurfaceY + 8) == 0;
          self->recordArm("viewport-scroll-replay-setup",
                          scrolled && foundText && oldBlack && newWhite, WRITE);
        }
        self->initial_ = controller->debugStatsForTesting();
        node->advance();
        self->phase_ = HasViewport ? CHECK : PLAIN_CHECK;
      }
      else
      {
        const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
        const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
        const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
        const int draws = stats.totalControlDrawCount - self->initial_.totalControlDrawCount;
        const bool siblingPreserved = GetPixel(self->marker_.h, self->marker_.v) != 0;
        std::fprintf(self->log_, "gate=%d invalidate_rects=%d whole_window=%d control_draws=%d sibling_preserved=%d\r",
                     self->gate_ ? 1 : 0, rects, whole, draws, siblingPreserved ? 1 : 0);
        if (HasViewport)
        {
          const bool textPreserved = GetPixel(self->scrollTextMarker_.h, self->scrollTextMarker_.v) != 0;
          const bool oldErased = GetPixel(kRootX + 8, kScrollSurfaceY + 8) == 0;
          const bool newPainted = GetPixel(kRootX + 20, kScrollSurfaceY + 8) != 0;
          std::fprintf(self->log_, "scrolled_text_preserved=%d old_erased=%d new_painted=%d\r",
                       textPreserved ? 1 : 0, oldErased ? 1 : 0, newPainted ? 1 : 0);
          self->recordArm("viewport-scroll-replay",
                          whole == 0 && rects >= 1 && textPreserved && oldErased && newPainted
                          && stats.totalRenderCalls == self->initial_.totalRenderCalls
                          && stats.totalRenderDirtyCalls > self->initial_.totalRenderDirtyCalls, CHECK);
        }
        SetPort(previousPort);
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        PaintAnswerBuffer<> answers;
        ProbeSource source;
        const PaintApplyVerdict verdict = CollectPaintAnswers(*node, query, answers, source);
        std::fprintf(self->log_, "delivery=%s reason=%d kind=%d\r",
                     whole == 0 ? "EXACT" : "FULL",
                     verdict.refusedCount() ? static_cast<int>(verdict.refusalReason()) : -1,
                     verdict.refusedCount() ? static_cast<int>(source.refusingKind) : -1);
        self->recordArm(HasViewport ? "viewport-exact" : "plain-exact",
                        self->gate_ && siblingPreserved && whole == 0
                        && (HasViewport ? rects >= 1 : rects == 1),
                        HasViewport ? PLAIN_WRITE : INVALIDATED_WRITE);
        return;
      }
      SetPort(previousPort);
    }
    static void OnCompositedIdle(Window *window, double, void *data)
    {
      PaintDamageConfig *self = static_cast<PaintDamageConfig *>(data);
      if (self->phase_ != COMPOSITED_WRITE && self->phase_ != COMPOSITED_CHECK)
        return;
      ToolboxWindow *native = window ? window->asToolboxWindow() : 0;
      if (!native || !window->scene() || !self->composited_)
      {
        self->finish(false);
        return;
      }
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(
          loka::dsl::testing::SceneTestAccess::platformController(*window->scene()));
      if (!controller)
      {
        self->finish(false);
        return;
      }
      if (self->phase_ == COMPOSITED_WRITE)
      {
        // This fixture's viewport starts at the root seat kRootY. Require a
        // complete recorded Text placement and visible ink inside that seat.
        TextNode *text = 0;
        loka::dsl::FlowError error;
        loka::dsl::testing::LookupNodeById<TextNode>(
            window->scene(), "PaintDamage.Baseline", text, error);
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        const PaintAnswer answer = text && text->getContext()
            ? static_cast<NativeNodeContext *>(text->getContext())->queryPaintDamage(query)
            : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
        const bool placedInside = answer.kind == PAINT_ANSWER_EXACT && answer.damage.y >= kRootY;
        GrafPtr previousPort;
        GetPort(&previousPort);
        SetPort(native->window());
        bool above = false;
        bool inside = false;
        for (short y = kRootY - 12; y < kRootY + 14; ++y)
          for (short x = kRootX; x < kRootX + 58; ++x)
            if (GetPixel(x, y))
            {
              if (y < kRootY)
                above = true;
              else
                inside = true;
            }
        SetPort(previousPort);
        self->recordArm("scrollview-first-text-placement", placedInside, COMPOSITED_WRITE);
        self->recordArm("startup-text-baseline", !above && inside, COMPOSITED_WRITE);
        self->checkBlockingWorkCursor(*controller, COMPOSITED_WRITE);
        self->initial_ = controller->debugStatsForTesting();
        self->composited_->advance();
        self->phase_ = COMPOSITED_CHECK;
        return;
      }
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      const int broad = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      PaintAnswerBuffer<> answers;
      ProbeSource source;
      const PaintApplyVerdict verdict = CollectPaintAnswers(*self->composited_, query, answers, source);
      BoundaryLocalApplyInfo info;
      info.paintKind = LOCAL_APPLY_PAINT_COMPOSITED;
      const bool gate = verdict.canSkipBroadPaint(info);
      const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
      // The ZStack is not classified composited by the Boundary, so the write
      // is delivered exactly; renderDirty's ZStack branch re-renders the dirty
      // rect under a clip so the overlapping Text keeps its ink. Sample the
      // Text ink right of the moved sprite; the ZStack follows a 16px
      // Text seat and one gap.
      GrafPtr zPort;
      GetPort(&zPort);
      SetPort(native->window());
      bool textInk = false;
      for (short y = kRootY + 16 + kRootGap + 2; y < kRootY + 16 + kRootGap + 14; ++y)
        for (short x = kRootX + 32; x < kRootX + 46; ++x)
          textInk = textInk || GetPixel(x, y) != 0;
      SetPort(zPort);
      std::fprintf(self->log_, "zstack_gate=%d broad_requests=%d rect_requests=%d text_ink=%d\r", gate ? 1 : 0, broad, rects, textInk ? 1 : 0);
      self->recordArm("contained-text-history", verdict.refusedCount() == 0, COMPOSITED_CHECK);
      self->recordArm("zstack-overlap-replay", !gate && broad == 0 && rects >= 1 && textInk, EDIT_WRITE);
    }
    static void OnBoundsIdle(Window *window, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == BOUNDS_SHOW)
      {
        // Keep this window hidden until all earlier fixture arms have finished.
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = BOUNDS_CHECK;
        return;
      }
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      // Both root-relative samples lie in the sprite's
      // row; only the first lies inside the 60 x 40 surface.
      const bool insideBlack = GetPixel(kRootX + 56, kRootY + 36) != 0;
      const bool outsideWhite = GetPixel(kRootX + 64, kRootY + 36) == 0;
      // The second seat follows the 40px Box and one gap. Text starts
      // 48px into its 54px viewport; H has ink across the clip edge.
      bool textInsideBlack = false;
      bool textOutsideWhite = true;
      for (short x = kRootX; x < kRootX + 48; ++x)
      {
        textInsideBlack = GetPixel(x, kBoundsViewportY + 53) != 0 || textInsideBlack;
        textOutsideWhite = GetPixel(x, kBoundsViewportY + 55) == 0 && textOutsideWhite;
      }
      SetPort(previousPort);
      TextNode *text = 0;
      loka::dsl::FlowError error;
      loka::dsl::testing::LookupNodeById<TextNode>(
          window->scene(), "PaintDamage.ClippedText", text, error);
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      const PaintAnswer answer = text && text->getContext()
          ? static_cast<NativeNodeContext *>(text->getContext())->queryPaintDamage(query)
          : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
      const bool clippedHistory = answer.kind == PAINT_ANSWER_EXACT && answer.damage.y == kBoundsViewportY + 48;
      std::fprintf(self->log_, "text_inside_black=%d outside_white=%d clipped_history=%d\r",
                   textInsideBlack ? 1 : 0, textOutsideWhite ? 1 : 0, clippedHistory ? 1 : 0);
      self->recordArm("text-clipped-to-viewport",
                      textInsideBlack && textOutsideWhite && clippedHistory, BOUNDS_CHECK);
      std::fprintf(self->log_, "surface_inside_black=%d outside_white=%d\r",
                   insideBlack ? 1 : 0, outsideWhite ? 1 : 0);
      self->recordArm("surface-bounds-clip", insideBlack && outsideWhite, CELL_SHOW);
    }
    static void OnDrawerIdle(Window *window, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == CELL_SHOW || self->phase_ == POPUP_SHOW || self->phase_ == OVERFLOW_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = self->phase_ == CELL_SHOW ? CELL_REPLAY
                       : self->phase_ == POPUP_SHOW ? POPUP_REPLAY : OVERFLOW_REPLAY;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      if (!controller)
      {
        self->finish(false);
        return;
      }
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      if (self->phase_ == CELL_REPLAY || self->phase_ == POPUP_REPLAY)
      {
        const bool cell = self->phase_ == CELL_REPLAY;
        Node *viewportNode = 0;
        loka::dsl::FlowError error;
        loka::dsl::testing::LookupNodeById<Node>(window->scene(), "Drawer.Viewport", viewportNode, error);
        ScrollViewNode *viewport = viewportNode ? viewportNode->asScrollViewNode() : 0;
        if (!viewport)
        {
          SetPort(previousPort);
          self->finish(false);
          return;
        }
        // Isolate replay while c-1 retains the production viewport fallback.
        // Retire only the native scrollbar after full presentation; the
        // drawer's captured projection and clipped hit rows remain intact.
        controller->destroyViewportScrollBarControl(viewport, viewport->nativeLifetimeHint());
        Rect damage;
        SetRect(&damage, kRootX, cell ? kCellThirdY : kRootY,
                kRootX + (cell ? 100 : 144), cell ? kCellThirdY + 24 : kRootY + 20);
        if (!cell)
          self->popupRow_.capture();
        bool ink = cell ? GetPixel(kRootX, kCellThirdY + 8) != 0 && GetPixel(kRootX, kCellThirdY + 18) == 0
                        : GetPixel(kRootX, kRootY + 7) != 0;
        if (!cell)
        {
          bool labelInk = false;
          for (short x = kRootX + 4; x < kRootX + 43; ++x)
            labelInk = labelInk || GetPixel(x, kRootY + 7) != 0;
          ink = ink && labelInk;
        }
        self->initial_ = controller->debugStatsForTesting();
        native->requestInvalidateRect(damage);
        SetPort(previousPort);
        self->recordArm(cell ? "cell-replay-setup" : "popup-replay-setup", ink,
                        cell ? CELL_CHECK : POPUP_CHECK);
        return;
      }
      if (self->phase_ == CELL_CHECK || self->phase_ == POPUP_CHECK)
      {
        const bool cell = self->phase_ == CELL_CHECK;
        const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
        const bool replayed = stats.totalRenderCalls == self->initial_.totalRenderCalls
                              && stats.totalRenderDirtyCalls > self->initial_.totalRenderDirtyCalls
                              && stats.windowFlushDirtyCount > self->initial_.windowFlushDirtyCount;
        bool pass = cell ? GetPixel(kRootX, kCellThirdY + 8) != 0 && GetPixel(kRootX, kCellThirdY + 18) == 0
                         : self->popupRow_.matches();
        // The projection clip ends at root x + 96; the 16px scrollbar only
        // reduces the child layout width, not the viewport clip.
        // The retired scrollbar no longer contributes native ink here.
        if (!cell)
          for (short y = kRootY; y < kRootY + 18; ++y)
            for (short x = kRootX + 96; x < kRootX + 144; ++x)
              pass = pass && GetPixel(x, y) == 0;
        SetPort(previousPort);
        std::fprintf(self->log_, "drawer_dirty_replayed=%d pixels_match=%d\r", replayed ? 1 : 0, pass ? 1 : 0);
        self->recordArm(cell ? "cell-replay-clipped" : "popup-replay-geometry", replayed && pass,
                        cell ? POPUP_SHOW : OVERFLOW_SHOW);
        return;
      }
      if (self->phase_ == OVERFLOW_REPLAY)
      {
        // A Cell-only registry must reach the Cell replay loop.
        Rect damage;
        SetRect(&damage, kOverflowCellX, kRootY, kOverflowCellX + 8, kRootY + 24);
        self->initial_ = controller->debugStatsForTesting();
        native->requestInvalidateRect(damage);
        self->phase_ = OVERFLOW_CHECK;
        SetPort(previousPort);
        return;
      }
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      self->recordArm("cell-only-dirty-replay",
                      stats.totalRenderCalls == self->initial_.totalRenderCalls
                      && stats.totalRenderDirtyCalls > self->initial_.totalRenderDirtyCalls, OVERFLOW_CHECK);
      // The narrow Cell follows the 42px Box and one gap. Its seven-W label
      // is wider than the box in the same font used by the production drawer.
      const unsigned char label[] = {7, 'W', 'W', 'W', 'W', 'W', 'W', 'W'};
      bool outsideWhite = true;
      for (short y = kRootY + 1; y < kRootY + 23; ++y)
        for (short x = kRootX; x < kRootX + 108; ++x)
          if (x < kOverflowCellX || x >= kOverflowCellX + 8)
            outsideWhite = outsideWhite && GetPixel(x, y) == 0;
      const bool setup = StringWidth(label) > 8 && GetPixel(kOverflowCellX, kRootY) != 0;
      SetPort(previousPort);
      self->recordArm("cell-ink-clipped-to-rect", setup && outsideWhite, IMAGE_SHOW);
    }
    static void OnImageIdle(Window *window, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == IMAGE_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = IMAGE_WRITE;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      if (!controller)
      {
        self->finish(false);
        return;
      }
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      if (self->phase_ == IMAGE_WRITE)
      {
        Node *ownerNode = 0;
        Node *surfaceNode = 0;
        loka::dsl::FlowError error;
        loka::dsl::testing::LookupNodeById<Node>(
            window->scene(), "ImageDamage.Owner", ownerNode, error);
        loka::dsl::testing::LookupNodeById<Node>(
            window->scene(), "ImageDamage.Surface", surfaceNode, error);
        ImageSurfaceNode *owner = ownerNode && ownerNode->asBoundary()
            ? static_cast<ImageSurfaceNode *>(ownerNode) : 0;
        RectSurfaceNode *surface = surfaceNode ? surfaceNode->asRectSurfaceNode() : 0;
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        const PaintAnswer answer = surface && surface->getContext()
            ? static_cast<NativeNodeContext *>(surface->getContext())->queryPaintDamage(query)
            : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
        // (110,20) is inside the sprite, below the label baseline (+14),
        // and away from the frame. It must survive the placeholder draw.
        const bool spriteVisible = owner && answer.kind == PAINT_ANSWER_EXACT
            && GetPixel(answer.damage.x + 110, answer.damage.y + 20) != 0;
        self->recordArm("image-ground-transparent", spriteVisible, IMAGE_WRITE);
        if (!spriteVisible)
        {
          SetPort(previousPort);
          self->finish(false);
          return;
        }
        bool foundInk = false;
        if (owner && answer.kind == PAINT_ANSWER_EXACT)
        {
          // Sample the placeholder interior, excluding its frame. The old
          // sprite covers this area; its new position starts at local x=128.
          const int top = answer.damage.y + 2;
          const int bottom = answer.damage.y + 14;
          const int left = answer.damage.x + 6;
          const int right = answer.damage.x + 100;
          for (int y = top; y < bottom && !foundInk; ++y)
            for (int x = left; x < right && !foundInk; ++x)
              if (GetPixel(static_cast<short>(x), static_cast<short>(y)))
              {
                self->marker_.h = static_cast<short>(left);
                self->marker_.v = static_cast<short>(top);
                foundInk = true;
              }
        }
        std::fprintf(self->log_, "image_placeholder_setup=%d\r", foundInk ? 1 : 0);
        self->recordArm("image-overlap-replay-setup", foundInk, IMAGE_CHECK);
        if (foundInk)
        {
          self->initial_ = controller->debugStatsForTesting();
          owner->advance();
        }
        SetPort(previousPort);
        if (!foundInk)
          self->finish(false);
        return;
      }
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
      const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
      bool ink = false;
      // After the sprite moves away, only the replayed label can supply ink.
      for (int y = self->marker_.v; y < self->marker_.v + 12; ++y)
        for (int x = self->marker_.h; x < self->marker_.h + 94; ++x)
          ink = GetPixel(static_cast<short>(x), static_cast<short>(y)) != 0 || ink;
      // The ZStack makes this exact update a composition-order replay, whose
      // clipped render() is not a whole-window repaint; whole == 0 pins that.
      const bool replayed = stats.totalRenderDirtyCalls > self->initial_.totalRenderDirtyCalls
                            && stats.windowFlushDirtyCount > self->initial_.windowFlushDirtyCount;
      // Surface-local (140, 20): the moved sprite (x >= 128) under the
      // transparent placeholder, right of its label and inside its frame.
      const bool spriteThrough = GetPixel(static_cast<short>(self->marker_.h - 6 + 140),
                                          static_cast<short>(self->marker_.v - 2 + 20)) != 0;
      SetPort(previousPort);
      std::fprintf(self->log_, "image_whole_window=%d rect_requests=%d placeholder_ink=%d dirty_replayed=%d\r",
                   whole, rects, ink ? 1 : 0, replayed ? 1 : 0);
      self->recordArm("image-overlap-shows-surface", spriteThrough, IMAGE_CHECK);
      self->recordArm("image-overlap-replay", whole == 0 && rects >= 1 && ink && replayed, IMAGE_VACATED_SHOW);
    }
    static void OnImageVacatedIdle(Window *window, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == IMAGE_VACATED_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = IMAGE_VACATED_WRITE;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      Node *node = 0;
      loka::dsl::FlowError error;
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "ImageVacated.Image", node, error);
      ImageViewNode *image = node ? node->asImageViewNode() : 0;
      ToolboxImageViewContext *context = image
          ? static_cast<ToolboxImageViewContext *>(image->getContext()) : 0;
      if (!controller || !context)
      {
        self->recordArm("image-vacated-setup", false, EDIT_EXACT_SHOW);
        self->finish(false);
        return;
      }
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      const Rect rect = context->rect();
      // Image-local (30, 40): inside the frame and below the placeholder
      // label (baseline +14), so after a repaint it belongs to the ground.
      const short sampleH = static_cast<short>(rect.left + 30);
      const short sampleV = static_cast<short>(rect.top + 40);
      if (self->phase_ == IMAGE_VACATED_WRITE)
      {
        // Stand-in for what a larger, earlier image left behind: ink inside
        // the ImageView that its next draw does not cover.
        Rect stale;
        SetRect(&stale, static_cast<short>(rect.left + 20), static_cast<short>(rect.top + 30),
                static_cast<short>(rect.left + 60), static_cast<short>(rect.top + 60));
        PaintRect(&stale);
        const bool black = GetPixel(sampleH, sampleV) != 0;
        SetPort(previousPort);
        self->recordArm("image-vacated-setup", black, IMAGE_VACATED_CHECK);
        if (!black)
        {
          self->finish(false);
          return;
        }
        self->initial_ = controller->debugStatsForTesting();
        // An explicit dirty rect isolates the controller's image path from a
        // broad repaint.
        native->requestInvalidateRect(rect);
        return;
      }
      const bool black = GetPixel(sampleH, sampleV) != 0;
      SetPort(previousPort);
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      const bool dirtyOnly = stats.totalRenderCalls == self->initial_.totalRenderCalls
          && stats.totalRenderDirtyCalls > self->initial_.totalRenderDirtyCalls
          && stats.windowFlushDirtyCount > self->initial_.windowFlushDirtyCount
          && stats.windowFullRequestCount == self->initial_.windowFullRequestCount;
      self->recordArm("image-vacated-restores-window", !black && dirtyOnly, EDIT_EXACT_SHOW);
    }
    static void OnEditExactIdle(Window *window, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == EDIT_EXACT_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = EDIT_EXACT_WRITE;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      FindEdit edit;
      enumerateAttachedResidents(self->editExact_, edit);
      ToolboxScenePlatformController::EditTextGeometry geometry;
      if (!controller || !self->editExact_
          || !controller->queryEditTextGeometryForTesting(edit.context, geometry))
      {
        self->recordArm("edittext-exact-geometry", false, COMPLETE);
        self->finish(false);
        return;
      }
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      if (self->phase_ == EDIT_EXACT_WRITE)
      {
        Node *surface = 0;
        TextNode *text = 0;
        loka::dsl::FlowError error;
        loka::dsl::testing::LookupNodeById<Node>(window->scene(), "EditExact.Surface", surface, error);
        loka::dsl::testing::LookupNodeById<TextNode>(window->scene(), "EditExact.Text", text, error);
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        const PaintAnswer surfaceAnswer = surface && surface->getContext()
            ? static_cast<NativeNodeContext *>(surface->getContext())->queryPaintDamage(query)
            : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
        const PaintAnswer textAnswer = text && text->getContext()
            ? static_cast<NativeNodeContext *>(text->getContext())->queryPaintDamage(query)
            : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
        bool foundText = false;
        if (textAnswer.kind == PAINT_ANSWER_EXACT)
          for (int y = textAnswer.damage.y; y < textAnswer.damage.y + 14 && !foundText; ++y)
            for (int x = textAnswer.damage.x; x < textAnswer.damage.x + 60 && !foundText; ++x)
              if (GetPixel(static_cast<short>(x), static_cast<short>(y)))
              {
                self->scrollTextMarker_.h = static_cast<short>(x);
                self->scrollTextMarker_.v = static_cast<short>(y);
                foundText = true;
              }
        self->marker_.h = static_cast<short>(surfaceAnswer.damage.x + 8);
        self->marker_.v = static_cast<short>(surfaceAnswer.damage.y + 8);
        bool editWhite = true;
        for (int y = geometry.view.top; y < geometry.view.bottom; ++y)
          for (int x = geometry.view.left; x < geometry.view.right; ++x)
            editWhite = GetPixel(static_cast<short>(x), static_cast<short>(y)) == 0 && editWhite;
        const bool setup = surfaceAnswer.kind == PAINT_ANSWER_EXACT && foundText && editWhite
                           && GetPixel(self->marker_.h, self->marker_.v) != 0;
        SetPort(previousPort);
        self->recordArm("edittext-exact-setup", setup, EDIT_EXACT_CHECK);
        if (!setup)
        {
          self->finish(false);
          return;
        }
        self->editGeometry_ = geometry;
        self->initial_ = controller->debugStatsForTesting();
        self->editExact_->advance();
        return;
      }
      bool editInk = false;
      for (int y = geometry.view.top; y < geometry.view.bottom; ++y)
        for (int x = geometry.view.left; x < geometry.view.right; ++x)
          editInk = GetPixel(static_cast<short>(x), static_cast<short>(y)) != 0 || editInk;
      const bool sprite = GetPixel(self->marker_.h, self->marker_.v) != 0;
      const bool textInk = GetPixel(self->scrollTextMarker_.h, self->scrollTextMarker_.v) != 0;
      SetPort(previousPort);
      std::string value;
      const bool newValue = controller->queryEditTextValueForTesting(edit.context, value) && value == "HHHH";
      const bool sameGeometry = EqualRect(&geometry.view, &self->editGeometry_.view)
                                && EqualRect(&geometry.destination, &self->editGeometry_.destination);
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
      const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
      std::fprintf(self->log_, "edittext_whole_window=%d rect_requests=%d sprite_preserved=%d text_preserved=%d new_text_visible=%d\r",
                   whole, rects, sprite ? 1 : 0, textInk ? 1 : 0, editInk && newValue ? 1 : 0);
      self->recordArm("edittext-exact", whole == 0 && rects >= 1 && sprite && textInk
                      && editInk && newValue && sameGeometry, EDIT_EXACT_CHECK);
      const PaintQuery settled = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      const PaintAnswer unchanged = edit.context->queryPaintDamage(settled);
      self->recordArm("edittext-presented-empty", unchanged.kind == PAINT_ANSWER_EXACT
                      && unchanged.damage.width == 0 && unchanged.damage.height == 0, EDIT_EXACT_CHECK);
      PaintQuery pending = settled;
      pending.placement = PLACEMENT_PENDING;
      PaintQuery foreign = settled;
      ++foreign.scope.ownerKey;
      self->recordArm("edittext-placement-refuses",
                      edit.context->queryPaintDamage(pending).kind == PAINT_ANSWER_REFUSED
                      && edit.context->queryPaintDamage(foreign).kind == PAINT_ANSWER_REFUSED, EDIT_EXACT_CHECK);
      // Revoke the installed binding while the context still exists. History
      // alone must not keep granting EXACT after the native TE has retired.
      controller->retireNodeContext(edit.context, edit.context->lifetimeHint());
      self->recordArm("edittext-native-retired-refuses",
                      !controller->queryEditTextGeometryForTesting(edit.context, geometry)
                      && edit.context->queryPaintDamage(settled).kind == PAINT_ANSWER_REFUSED, EDIT_ZSTACK_SHOW);
    }
    static void OnEditZStackIdle(Window *window, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == EDIT_ZSTACK_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = EDIT_ZSTACK_WRITE;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      FindEdit edit;
      enumerateAttachedResidents(self->editZStack_, edit);
      ToolboxScenePlatformController::EditTextGeometry geometry;
      if (!controller || !self->editZStack_
          || !controller->queryEditTextGeometryForTesting(edit.context, geometry))
      {
        self->recordArm("edittext-zstack-order-geometry", false, COMPLETE);
        self->finish(false);
        return;
      }
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      if (self->phase_ == EDIT_ZSTACK_WRITE)
      {
        Node *surface = 0;
        loka::dsl::FlowError error;
        loka::dsl::testing::LookupNodeById<Node>(window->scene(), "EditZStack.Surface", surface, error);
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        const PaintAnswer answer = surface && surface->getContext()
            ? static_cast<NativeNodeContext *>(surface->getContext())->queryPaintDamage(query)
            : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
        // The later sprite occupies local [4,16) x [4,16). The narrow
        // surface leaves the right TE strip clear for the new H glyphs.
        self->marker_.h = static_cast<short>(answer.damage.x + 10);
        self->marker_.v = static_cast<short>(answer.damage.y + 10);
        // Leading spaces keep the sample free of glyph ink on a bad replay.
        const unsigned char prefix[] = {4, ' ', ' ', ' ', ' '};
        const bool blankPrefix = StringWidth(prefix) > self->marker_.h - geometry.view.left;
        const bool overlaps = geometry.view.left < answer.damage.x + 4
                              && geometry.view.right >= answer.damage.x + 16
                              && geometry.view.top <= answer.damage.y + 4
                              && geometry.view.bottom >= answer.damage.y + 16;
        bool uncoveredWhite = true;
        for (int y = geometry.view.top; y < geometry.view.bottom; ++y)
          for (int x = answer.damage.x + 16; x < geometry.view.right; ++x)
            uncoveredWhite = GetPixel(static_cast<short>(x), static_cast<short>(y)) == 0 && uncoveredWhite;
        const bool sprite = GetPixel(self->marker_.h, self->marker_.v) != 0;
        SetPort(previousPort);
        std::fprintf(self->log_, "edittext_zstack_setup_sprite_black=%d uncovered_white=%d overlaps_te=%d te=(%d,%d,%d,%d) sample=(%d,%d)\r",
                     sprite ? 1 : 0, uncoveredWhite ? 1 : 0, overlaps ? 1 : 0,
                     geometry.view.left, geometry.view.top, geometry.view.right, geometry.view.bottom,
                     self->marker_.h, self->marker_.v);
        const bool setup = answer.kind == PAINT_ANSWER_EXACT && overlaps && blankPrefix && uncoveredWhite && sprite;
        self->recordArm("edittext-zstack-order-setup", setup, EDIT_ZSTACK_CHECK);
        if (!setup)
        {
          self->finish(false);
          return;
        }
        self->editGeometry_ = geometry;
        self->initial_ = controller->debugStatsForTesting();
        self->editZStack_->advance();
        return;
      }
      bool newInk = false;
      // Reuse the setup's all-white strip, excluding the sprite and chrome.
      for (int y = geometry.view.top; y < geometry.view.bottom; ++y)
        for (int x = self->marker_.h + 6; x < geometry.view.right; ++x)
          newInk = GetPixel(static_cast<short>(x), static_cast<short>(y)) != 0 || newInk;
      const bool sprite = GetPixel(self->marker_.h, self->marker_.v) != 0;
      SetPort(previousPort);
      std::string value;
      const bool newValue = controller->queryEditTextValueForTesting(edit.context, value) && value == "    HHHH";
      const bool sameGeometry = EqualRect(&geometry.view, &self->editGeometry_.view)
                                && EqualRect(&geometry.destination, &self->editGeometry_.destination);
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
      const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
      const bool dirtyFlushed = stats.totalRenderDirtyCalls > self->initial_.totalRenderDirtyCalls
                                && stats.windowFlushDirtyCount > self->initial_.windowFlushDirtyCount;
      std::fprintf(self->log_, "edittext_zstack_whole_window=%d rect_requests=%d sprite_black=%d new_text_visible=%d dirty_flushed=%d same_geometry=%d\r",
                   whole, rects, sprite ? 1 : 0, newInk && newValue ? 1 : 0,
                   dirtyFlushed ? 1 : 0, sameGeometry ? 1 : 0);
      self->recordArm("edittext-zstack-order", whole == 0 && rects >= 1 && sprite
                      && newInk && newValue && sameGeometry && dirtyFlushed, POPUP_EXACT_SHOW);
    }
    static void OnPopupExactIdle(Window *window, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == POPUP_EXACT_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = POPUP_SIBLING_WRITE;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      Node *popup = 0;
      Node *surface = 0;
      loka::dsl::FlowError error;
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "PopupExact.Popup", popup, error);
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "PopupExact.Surface", surface, error);
      ToolboxPopupMenuContext *context = popup && popup->getContext()
          ? static_cast<ToolboxPopupMenuContext *>(popup->getContext()) : 0;
      if (!controller || !context || !surface || !surface->getContext() || !self->popupExact_)
      {
        self->recordArm("popup-exact-geometry", false, COMPLETE);
        self->finish(false);
        return;
      }
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      const Rect rect = context->rect();
      if (self->phase_ == POPUP_SIBLING_WRITE)
      {
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        const PaintAnswer answer = static_cast<NativeNodeContext *>(surface->getContext())->queryPaintDamage(query);
        self->marker_.h = static_cast<short>(answer.damage.x + 8);
        self->marker_.v = static_cast<short>(answer.damage.y + 8);
        const bool exact = answer.kind == PAINT_ANSWER_EXACT;
        const bool frameInk = GetPixel(rect.left, rect.top + 7) != 0;
        const bool markerInk = GetPixel(self->marker_.h, self->marker_.v) != 0;
        std::fprintf(self->log_, "popup_setup exact=%d width=%d frame_ink=%d marker_ink=%d\r",
                     exact ? 1 : 0, rect.right - rect.left, frameInk ? 1 : 0, markerInk ? 1 : 0);
        // Since #1015 the popup takes the width its Column offers: the 180-wide
        // Box less the ScrollView's 16-pixel bar.
        const bool setup = exact && rect.right - rect.left == 164 && frameInk && markerInk;
        self->popupFaceRows_.capture(rect);
        SetPort(previousPort);
        self->recordArm("popup-exact-setup", setup, POPUP_SIBLING_CHECK);
        if (!setup)
        {
          self->finish(false);
          return;
        }
        self->initial_ = controller->debugStatsForTesting();
        self->popupExact_->writeSibling();
        return;
      }
      const bool sibling = self->phase_ == POPUP_SIBLING_CHECK;
      const bool frameSame = self->popupFaceRows_.matches(rect, false);
      const bool labelSame = self->popupFaceRows_.matches(rect, true);
      const bool sprite = GetPixel(self->marker_.h, self->marker_.v) != 0;
      SetPort(previousPort);
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
      const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
      std::fprintf(self->log_, "popup_%s_whole_window=%d rect_requests=%d frame_row_same=%d label_row_same=%d sprite_preserved=%d\r",
                   sibling ? "sibling" : "selection", whole, rects, frameSame ? 1 : 0, labelSame ? 1 : 0, sprite ? 1 : 0);
      self->recordArm(sibling ? "popup-sibling-exact" : "popup-selection-exact",
                      whole == 0 && rects >= 1 && sprite && (sibling ? frameSame && labelSame : !labelSame),
                      sibling ? POPUP_SELECTION_CHECK : COMPLETE);
      if (sibling)
      {
        self->initial_ = controller->debugStatsForTesting();
        self->popupExact_->selectNext();
      }
      else
      {
        const PaintQuery settled = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        const PaintAnswer unchanged = context->queryPaintDamage(settled);
        self->recordArm("popup-presented-empty", unchanged.kind == PAINT_ANSWER_EXACT
                        && unchanged.damage.width == 0 && unchanged.damage.height == 0, COMPLETE);
        PaintQuery pending = settled;
        pending.placement = PLACEMENT_PENDING;
        PaintQuery foreign = settled;
        ++foreign.scope.ownerKey;
        self->recordArm("popup-placement-refuses",
                        context->queryPaintDamage(pending).kind == PAINT_ANSWER_REFUSED
                        && context->queryPaintDamage(foreign).kind == PAINT_ANSWER_REFUSED, COMPLETE);
        PopupMenuNode *popupNode = popup->asPopupMenuNode();
        loka::app::scene::WriteSeat<int> selection = popupNode->props.selectedIndex_;
        popupNode->props.selectedIndex_ = loka::app::scene::WriteSeat<int>();
        self->recordArm("popup-unreconciled-refuses",
                        context->queryPaintDamage(settled).kind == PAINT_ANSWER_REFUSED, COMPLETE);
        popupNode->props.selectedIndex_ = selection;
        SetPort(native->window());
        {
          Rect partial = rect;
          partial.right = partial.left + 2;
          ToolboxPaintClip clip(partial);
          context->repaint();
        }
        // #1027: a partial repaint of an unchanged value keeps the paint
        // history, so the next query answers exact instead of refusing.
        self->recordArm("popup-partial-unchanged-keeps-history",
                        context->queryPaintDamage(settled).kind == PAINT_ANSWER_EXACT, COMPLETE);
        context->repaint();
        self->recordArm("popup-full-history-restored",
                        context->queryPaintDamage(settled).kind == PAINT_ANSWER_EXACT, COMPLETE);
        context->updateRect(rect, 16);
        self->recordArm("popup-layout-history-refuses",
                        context->queryPaintDamage(settled).kind == PAINT_ANSWER_REFUSED, COMPLETE);
        context->repaint();
        // Show retains this branch: the borrows stay valid across its kernel detach.
        self->popupExact_->hidePopup();
        SettleTurn();
        window->flushSceneInvalidation();
        self->recordArm("popup-detached-refuses",
                        popup->lifecycleFact() == NODE_FACT_DETACHED_RETAINED
                        && popup->getContext() == context
                        && context->deliveredFact() == NODE_FACT_DETACHED_RETAINED
                        && context->queryPaintDamage(settled).kind == PAINT_ANSWER_REFUSED, COMPLETE);
        SetPort(previousPort);
        self->phase_ = HISTORY_SHOW;
      }
    }
    template <bool Popup>
    static void OnHistoryIdle(Window *window, HistoryNode<Popup> *node, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == (Popup ? POPUP_HISTORY_SHOW : HISTORY_SHOW))
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = Popup ? POPUP_HISTORY_FIRST : HISTORY_FIRST;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      Node *second = 0;
      Node *surface = 0;
      loka::dsl::FlowError error;
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "History.B", second, error);
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "History.Surface", surface, error);
      if (!controller || !node || !second || !second->getContext() || !surface || !surface->getContext())
      {
        self->finish(false);
        return;
      }
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      if (self->phase_ == (Popup ? POPUP_HISTORY_FIRST : HISTORY_FIRST))
      {
        const PaintAnswer b = static_cast<NativeNodeContext *>(second->getContext())->queryPaintDamage(query);
        const PaintAnswer sprite = static_cast<NativeNodeContext *>(surface->getContext())->queryPaintDamage(query);
        if (b.kind != PAINT_ANSWER_EXACT || sprite.kind != PAINT_ANSWER_EXACT)
        {
          self->finish(false);
          return;
        }
        SetRect(&self->editGeometry_.view, b.damage.x, b.damage.y,
                b.damage.x + 36, b.damage.y + 14);
        self->marker_.h = static_cast<short>(sprite.damage.x + 8);
        self->marker_.v = static_cast<short>(sprite.damage.y + 8);
        GrafPtr previousPort;
        GetPort(&previousPort);
        SetPort(native->window());
        bool blank = true;
        for (int y = b.damage.y + 3; y < b.damage.y + 11; ++y)
          for (int x = b.damage.x + 4; x < b.damage.x + 36; ++x)
            blank = GetPixel(static_cast<short>(x), static_cast<short>(y)) == 0 && blank;
        const bool spriteVisible = GetPixel(self->marker_.h, self->marker_.v) != 0;
        SetPort(previousPort);
        self->recordArm(Popup ? "popup-history-setup" : "history-setup", blank && spriteVisible,
                        Popup ? POPUP_HISTORY_SECOND : HISTORY_SECOND);
        self->initial_ = controller->debugStatsForTesting();
        node->writeFirst();
        self->phase_ = Popup ? POPUP_HISTORY_SECOND : HISTORY_SECOND;
        return;
      }
      if (self->phase_ == (Popup ? POPUP_HISTORY_SECOND : HISTORY_SECOND))
      {
        const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
        const bool flushed = stats.totalRenderDirtyCalls > self->initial_.totalRenderDirtyCalls
                             && stats.windowFullRequestCount == self->initial_.windowFullRequestCount;
        self->recordArm(Popup ? "popup-history-first-clipped" : "history-first-clipped", flushed,
                        Popup ? POPUP_HISTORY_CHECK : HISTORY_CHECK);
        self->initial_ = stats;
        node->writeSecond();
        return;
      }
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      const Rect &rect = self->editGeometry_.view;
      bool ink = false;
      for (int y = rect.top + 3; y < rect.bottom - 3; ++y)
        for (int x = rect.left + 4; x < rect.left + 36; ++x)
          ink = GetPixel(static_cast<short>(x), static_cast<short>(y)) != 0 || ink;
      const bool sprite = GetPixel(self->marker_.h, self->marker_.v) != 0;
      SetPort(previousPort);
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
      const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
      const char *name = Popup ? "popup-history-survives" : "history-survives-clipped-render";
      std::fprintf(self->log_, "%s whole_window=%d rect_requests=%d sprite_preserved=%d new_text_visible=%d\r",
                   name, whole, rects, sprite ? 1 : 0, ink ? 1 : 0);
      self->recordArm(name, whole == 0 && rects >= 1 && sprite && ink,
                      Popup ? BUTTON_ENABLED_SHOW : POPUP_HISTORY_SHOW);
    }
    static PaintAnswer offscreenAnswer(Window *window, const char *id, const PaintQuery &query)
    {
      Node *node = 0;
      loka::dsl::FlowError error;
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), id, node, error);
      return node && node->getContext()
          ? static_cast<NativeNodeContext *>(node->getContext())->queryPaintDamage(query)
          : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
    }

    static void OnOffscreenIdle(Window *window, PaintDamageConfig *self,
                                const PaintApplyVerdict &verdict, NodeKind refusingKind)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == OFFSCREEN_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = OFFSCREEN_WRITE;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      if (!controller || !self->offscreen_)
      {
        self->finish(false);
        return;
      }
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      if (self->phase_ == OFFSCREEN_WRITE)
      {
        const char *ids[] = {"Offscreen.Text", "Offscreen.Edit", "Offscreen.Popup", "Offscreen.Button"};
        for (int i = 0; i < 4; ++i)
        {
          const PaintAnswer answer = offscreenAnswer(window, ids[i], query);
          std::fprintf(self->log_, "%s answer=%d reason=%d\r", ids[i],
                       static_cast<int>(answer.kind), static_cast<int>(answer.reason));
          self->recordArm(ids[i], answer.kind == PAINT_ANSWER_EXACT
                          && answer.damage.width == 0 && answer.damage.height == 0, OFFSCREEN_WRITE);
          PaintQuery pending = query;
          pending.placement = PLACEMENT_PENDING;
          PaintQuery foreign = query;
          ++foreign.scope.ownerKey;
          self->recordArm("offscreen-placement-refuses",
                          offscreenAnswer(window, ids[i], pending).kind == PAINT_ANSWER_REFUSED
                          && offscreenAnswer(window, ids[i], foreign).kind == PAINT_ANSWER_REFUSED, OFFSCREEN_WRITE);
        }
        self->offscreen_->writeButton(true);
        // #1015: the Column offers its width, so a wider title keeps the
        // Button's rectangle and nothing in the Column refuses. The column-local
        // refusal itself is pinned on the host (PaintContractTests,
        // ToolboxPaintFoldHostTests).
        const PaintAnswer wider = offscreenAnswer(window, "Offscreen.Button", query);
        self->recordArm("offscreen-button-width-exact", wider.kind == PAINT_ANSWER_EXACT, OFFSCREEN_WRITE);
        {
          PaintAnswerBuffer<> answers;
          ProbeSource source;
          const PaintApplyVerdict verdict = CollectPaintAnswers(*self->offscreen_, query, answers, source);
          self->recordArm("column-container-button-no-refusal", verdict.refusedCount() == 0
                          && source.refusingKind == NODE_KIND_UNKNOWN, OFFSCREEN_WRITE);
        }
        self->offscreen_->writeButton(false);
        {
          PaintAnswerBuffer<> answers;
          ProbeSource source;
          const PaintApplyVerdict exact = CollectPaintAnswers(*self->offscreen_, query, answers, source);
          self->recordArm("column-local-refusal-cleared", exact.refusedCount() == 0
                          && source.refusingKind == NODE_KIND_UNKNOWN, OFFSCREEN_WRITE);
        }
        self->initial_ = controller->debugStatsForTesting();
        self->offscreen_->writeSibling();
        self->phase_ = OFFSCREEN_SIBLING_CHECK;
        return;
      }
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
      const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
      std::fprintf(self->log_, "column_phase=%d delivery=%s reason=%d kind=%d whole_window=%d rects=%d\r",
                   static_cast<int>(self->phase_), whole == 0 ? "EXACT" : "FULL",
                   verdict.refusedCount() ? static_cast<int>(verdict.refusalReason()) : -1,
                   verdict.refusedCount() ? static_cast<int>(refusingKind) : -1,
                   whole, rects);
      if (self->phase_ == OFFSCREEN_SIBLING_CHECK)
      {
        self->recordArm("column-offscreen-sibling-exact", whole == 0 && rects >= 1, OFFSCREEN_TEXT_CHECK);
        self->initial_ = stats;
        self->offscreen_->writeText(false);
        const PaintAnswer changed = offscreenAnswer(window, "Offscreen.VisibleText", query);
        self->recordArm("column-visible-text-damage", changed.kind == PAINT_ANSWER_EXACT
                        && changed.damage.width > 0 && changed.damage.height > 0, OFFSCREEN_TEXT_CHECK);
      }
      else if (self->phase_ == OFFSCREEN_TEXT_CHECK)
      {
        const PaintAnswer presented = offscreenAnswer(window, "Offscreen.VisibleText", query);
        self->recordArm("column-visible-text-exact", whole == 0 && rects >= 1
                        && presented.kind == PAINT_ANSWER_EXACT && presented.damage.width == 0, OFFSCREEN_HIDDEN_CHECK);
        self->initial_ = stats;
        self->offscreen_->writeText(true);
      }
      else if (self->phase_ == OFFSCREEN_HIDDEN_CHECK)
      {
        self->recordArm("column-offscreen-write-empty", whole == 0 && rects == 0, OFFSCREEN_REVEAL_CHECK);
        self->initial_ = stats;
        self->offscreen_->reveal();
      }
      else if (self->phase_ == OFFSCREEN_REVEAL_CHECK)
      {
        // Re-entry must establish history before another paint-only write.
        self->initial_ = stats;
        self->offscreen_->writeText(true, "MMMM");
        const PaintAnswer changed = offscreenAnswer(window, "Offscreen.Text", query);
        // The reveal is a scroll (layout) apply: the rail takes the broad
        // path and the debug counters reset at the global change, so the
        // whole-window delta is not observable here; the revealed Text's
        // current value and exact answer are the pin.
        self->recordArm("column-scroll-reveals-current-text",
                        changed.kind == PAINT_ANSWER_EXACT && changed.damage.width > 0
                        && changed.damage.height > 0 && changed.damage.y >= kRootY
                        && changed.damage.y + changed.damage.height <= kRootY + 110, OFFSCREEN_REVEALED_WRITE_CHECK);
      }
      else if (self->phase_ == OFFSCREEN_REVEALED_WRITE_CHECK)
      {
        const PaintAnswer presented = offscreenAnswer(window, "Offscreen.Text", query);
        self->recordArm("column-revealed-text-exact", whole == 0 && rects >= 1
                        && presented.kind == PAINT_ANSWER_EXACT && presented.damage.width == 0, OFFSCREEN_DETACH_CHECK);
        self->offscreen_->scrollToTop();
      }
      else
      {
        const char *ids[] = {"Offscreen.Text", "Offscreen.Edit", "Offscreen.Popup", "Offscreen.Button"};
        Node *drawers[4] = {};
        NativeNodeContext *contexts[4] = {};
        for (int i = 0; i < 4; ++i)
        {
          loka::dsl::FlowError error;
          loka::dsl::testing::LookupNodeById<Node>(window->scene(), ids[i], drawers[i], error);
          contexts[i] = drawers[i] ? static_cast<NativeNodeContext *>(drawers[i]->getContext()) : 0;
          if (!contexts[i])
          {
            self->recordArm("offscreen-detach-setup", false, COMPLETE);
            self->finish(false);
            return;
          }
          const PaintAnswer before = contexts[i]->queryPaintDamage(query);
          self->recordArm("offscreen-detach-setup", drawers[i]->lifecycleFact() == NODE_FACT_ATTACHED
                          && contexts[i]->deliveredFact() == NODE_FACT_ATTACHED
                          && before.kind == PAINT_ANSWER_EXACT
                          && before.damage.width == 0 && before.damage.height == 0, OFFSCREEN_DETACH_CHECK);
        }
        // Default Show parks all four drawers; borrows last only through this apply.
        self->offscreen_->hideDrawers();
        SettleTurn();
        window->flushSceneInvalidation();
        for (int i = 0; i < 4; ++i)
        {
          const PaintAnswer detached = contexts[i]->queryPaintDamage(query);
          std::fprintf(self->log_, "%s detached_fact=%d answer=%d reason=%d\r", ids[i],
                       static_cast<int>(contexts[i]->deliveredFact()), static_cast<int>(detached.kind),
                       static_cast<int>(detached.reason));
          self->recordArm("offscreen-detached-refuses", drawers[i]->lifecycleFact() == NODE_FACT_DETACHED_RETAINED
                          && drawers[i]->getContext() == contexts[i]
                          && contexts[i]->deliveredFact() == NODE_FACT_DETACHED_RETAINED
                          && detached.kind == PAINT_ANSWER_REFUSED, COMPLETE);
        }
        self->phase_ = RIBBON_SHOW;
      }
    }

    static void OnRibbonIdle(Window *window, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == RIBBON_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = RIBBON_CHECK;
        return;
      }
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      PaintAnswer edges[3];
      Node *ribbon = 0;
      loka::dsl::FlowError error;
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "Ribbon.Frame", ribbon, error);
      RibbonGroupNode *group =
          ribbon && ribbon->asNestable() ? ribbon->asNestable()->childrenHead()->asRibbonGroupNode() : 0;
      if (!group)
      {
        self->recordArm("ribbon-frame-setup", false, COMPLETE);
        self->finish(false);
        return;
      }
      loka::dsl::CompositionCursor<Node> children(group->childrenHead(), group->childrenCount());
      children.next(); // The content Column precedes the three closed leaves.
      for (unsigned i = 0; i < 3; ++i)
      {
        Node *child = children.next();
        RibbonGroupEdgeNode *edge = child ? child->asRibbonGroupEdgeNode() : 0;
        if (!edge || !edge->getContext())
        {
          self->recordArm("ribbon-frame-setup", false, COMPLETE);
          self->finish(false);
          return;
        }
        edges[edge->props.edge] = static_cast<NativeNodeContext *>(edge->getContext())->queryPaintDamage(query);
      }
      const short left = edges[RIBBON_GROUP_EDGE_LEFT].damage.x;
      const short top = edges[RIBBON_GROUP_EDGE_LEFT].damage.y;
      const short right = edges[RIBBON_GROUP_EDGE_RIGHT].damage.x;
      const short bottom = edges[RIBBON_GROUP_EDGE_BOTTOM].damage.y;
      GrafPtr saved;
      GetPort(&saved);
      SetPort(native->window());
      bool solid = right > left && bottom - top >= 32;
      for (short x = left; x <= right; ++x)
        solid = solid && GetPixel(x, bottom) != 0;
      unsigned density = 0;
      for (int y = top; y < top + 8; ++y)
        density += GetPixel(left, static_cast<short>(y)) != 0;
      const bool dark = GetPixel(left, bottom - 1) != 0;
      const bool inset = !GetPixel(left + 1, top + 1) && !GetPixel(right - 1, bottom - 1);
      // The caption and button occupy the inset interior, which still contains ink.
      bool buttonInk = false, captionInk = false;
      for (short x = left + RibbonGroupNode::kFrameInset; x <= right - RibbonGroupNode::kFrameInset; ++x)
      {
        for (int y = top + RibbonGroupNode::kFrameInset; y < top + 24; ++y)
          buttonInk = buttonInk || GetPixel(x, static_cast<short>(y)) != 0;
        for (short y = top + 26; y < bottom - 3; ++y)
          captionInk = captionInk || GetPixel(x, y) != 0;
      }
      if (self->phase_ == RIBBON_CHECK)
      {
        self->recordArm("ribbon-bottom-solid", solid, RIBBON_CHECK);
        self->recordArm("ribbon-left-bottom-black", dark, RIBBON_CHECK);
        self->recordArm("ribbon-left-top-density-2", density == 2, RIBBON_CHECK);
        self->recordArm("ribbon-inset-content-preserved", inset && buttonInk && captionInk, RIBBON_REPAINT);
        Rect damage;
        SetRect(&damage, left - 1, top, left + 3, bottom + 1);
        EraseRect(&damage);
        self->recordArm("ribbon-damage-erased-control", !GetPixel(left, bottom), RIBBON_REPAINT);
        native->requestInvalidateRect(damage);
      }
      else
      {
        self->recordArm("ribbon-partial-restores-edge",
                        solid && dark && density == 2 && inset && buttonInk && captionInk,
                        COMPLETE);
        self->checkCardPixels();
        self->phase_ = CARD_SHOW;
      }
      SetPort(saved);
    }

    static void OnCardIdle(Window *window, PaintDamageConfig *self)
    {
      Rect frame;
      SetRect(&frame, 0, 0, 220, 180);
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == CARD_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = CARD_CAPTURE;
        return;
      }
      GrafPtr saved;
      GetPort(&saved);
      SetPort(native->window());
      if (self->phase_ == CARD_CAPTURE)
      {
        Node *card = 0;
        loka::dsl::FlowError error;
        const bool found = loka::dsl::testing::LookupNodeById<Node>(window->scene(), "Card.Replay", card, error);
        const CardReplayPixels::Check failed =
            found && card && self->card_ ? self->cardReplay_.capture(card) : CardReplayPixels::CHECK_FOUND;
        // Each check passed is one PASS line; the first failure ends the list.
        static const char *const names[] = {"card-attached-replay-found",
                                            "card-attached-replay-bands-exact",
                                            "card-attached-replay-frame-96x72",
                                            "card-attached-replay-old-ink"};
        for (int check = CardReplayPixels::CHECK_FOUND; check < CardReplayPixels::READY && check <= failed; ++check)
          self->recordArm(names[check], check != failed, CARD_DISABLED);
        if (failed == CardReplayPixels::READY)
          self->card_->narrow(true);
        else
          self->finish(false);
      }
      else if (self->phase_ == CARD_DISABLED)
      {
        // No manual erase or invalidate: the live Box width drives the rail's broad delivery.
        self->recordArm("card-attached-disabled-erases-old-ink", self->cardReplay_.matches(true), CARD_RESTORED);
        self->card_->narrow(false);
      }
      else if (self->phase_ == CARD_RESTORED)
      {
        self->recordArm("card-attached-reenabled-restores-ink", self->cardReplay_.matches(false), CARD_SCROLLED);
        self->scrollStart_ = TickCount();
        Node *scrolled = 0;
        loka::dsl::FlowError error;
        if (!loka::dsl::testing::LookupNodeById<Node>(window->scene(), "Card.Scrolled", scrolled, error)
            || !self->cardReplay_.captureScrollRow(scrolled))
        {
          self->recordArm("card-scroll-row-capture", false, COMPLETE);
          self->finish(false);
          SetPort(saved);
          return;
        }
        self->card_->scroll();
      }
      else
      {
        const CardCapture incremental(frame);
        if (!incremental.ready())
        {
          self->recordArm("card-scroll-capture-allocation", false, COMPLETE);
          self->finish(false);
          SetPort(saved);
          return;
        }
        const bool captureCorrect = incremental.matchesProbe();
        Node *viewport = 0, *card = 0;
        loka::dsl::FlowError error;
        const bool foundViewport =
            loka::dsl::testing::LookupNodeById<Node>(window->scene(), "Card.Viewport", viewport, error);
        const bool foundCard = loka::dsl::testing::LookupNodeById<Node>(window->scene(), "Card.Scrolled", card, error);
        bool visibleWide = false;
        const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
        if (foundCard && card && card->asNestable())
          for (Node *child = card->asNestable()->childrenHead(); child; child = child->nextInComposition)
            if (child->asCardBandNode() && child->getContext()
                && child->asCardBandNode()->props.style.extent() == CARD_SHADOW_WIDE)
            {
              const PaintAnswer answer = static_cast<NativeNodeContext *>(child->getContext())->queryPaintDamage(query);
              visibleWide =
                  visibleWide
                  || (answer.kind == PAINT_ANSWER_EXACT && answer.damage.width > 0 && answer.damage.height > 0);
            }
        const loka::core::Frame origin = foundViewport && viewport && viewport->asScrollViewNode()
                                             ? viewport->asScrollViewNode()->requestedOrigin()
                                             : loka::core::Frame();
        self->recordArm("card-scroll-applied-visible-wide",
                        origin.x == 3 && origin.y == 5 && visibleWide && self->cardReplay_.scrollRowChanged(),
                        CARD_SCROLLED);
        // Synchronous production full draw keeps the capture within this arm.
        native->draw();
        self->recordArm(
            "card-scroll-incremental-equals-full", captureCorrect && incremental.differences() == 0, COMPLETE);
        std::FILE *timing = std::fopen("CARDTIME.TXT", "a");
        self->recordArm("card-scroll-timing-log-open", timing != 0, COMPLETE);
        if (timing)
        {
          std::fprintf(timing,
                       "card-scroll ticks=%lu (add to card-new-arms for 3600-tick budget)\r",
                       TickCount() - self->scrollStart_);
          if (!CloseCardTiming(timing))
            self->result_ = 1;
        }
        self->finish(true);
      }
      SetPort(saved);
    }

    void checkCardPixels()
    {
      // Append after the ribbon arms: all pre-existing arm lines and phase numbers stay fixed.
      static unsigned char reference[96 * 72], actual[96 * 72], ground[96 * 72];
      CardPixels card;
      PenState savedPen;
      GetPenState(&savedPen);
      PenNormal();
      loka::toolbox::ToolboxTextInkScope ink(blackColor);
      for (int size = 0; size < 2; ++size)
      {
        Rect frame;
        SetRect(&frame, 17, 19, 17 + (size ? 96 : 17), 19 + (size ? 72 : 17));
        ToolboxPaintClip clip(frame);
        EraseRect(&frame);
        const bool madeReference = CardPixels::tableReference(frame);
        CardPixels::capture(frame, reference);
        const int width = frame.right - frame.left, height = frame.bottom - frame.top;
        const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        bool seatClear = madeReference;
        for (int y = 3; y < height - 6; ++y)
          for (int x = 3; x < width - 6; ++x)
            seatClear = seatClear && !reference[y * width + x];
        this->recordArm(
            size ? "card-large-whole-ink-outside-seat" : "card-minimum-whole-ink-outside-seat", seatClear, COMPLETE);
        EraseRect(&frame);
        card.layout(frame);
        card.draw();
        CardPixels::capture(frame, actual);
        EraseRect(&frame);
        const bool madeV1 = CardPixels::reference(frame);
        CardPixels::capture(frame, ground);
        int differences = 0;
        for (std::size_t i = 0; i < pixelCount; ++i)
          differences += ground[i] != actual[i];
        std::fprintf(this->log_, "card-v1 size=%d available=%d differences=%d\r", size, madeV1, differences);
        if (differences)
        {
          CardPixels::dump(this->log_, frame, ground, "v1");
          CardPixels::dump(this->log_, frame, actual, "table");
        }
        this->recordArm(size ? "card-large-bands-equal-whole" : "card-minimum-bands-equal-whole",
                        std::memcmp(reference, actual, pixelCount) == 0,
                        COMPLETE);
        FillRect(&frame, &loka::toolbox::kToolboxGradientPatterns[2].pattern);
        CardPixels::capture(frame, ground);
        card.draw();
        CardPixels::capture(frame, actual);
        bool transparent = true;
        for (std::size_t i = 0; i < pixelCount; ++i)
          transparent = transparent && actual[i] == (ground[i] || reference[i]);
        this->recordArm(
            size ? "card-large-transparent-ground" : "card-minimum-transparent-ground", transparent, COMPLETE);
        // Damage wholly inside RIGHT, crossing its corner-to-straight transition.
        Rect damage = frame;
        damage.left = frame.right - 5;
        damage.top += 2;
        damage.bottom -= 7;
        {
          ToolboxPaintClip partial(damage);
          FillRect(&damage, &loka::toolbox::kToolboxGradientPatterns[2].pattern);
          card.draw(CARD_BAND_RIGHT);
        }
        CardPixels::capture(frame, ground);
        this->recordArm(size ? "card-large-partial-no-seam" : "card-minimum-partial-no-seam",
                        std::memcmp(actual, ground, pixelCount) == 0,
                        COMPLETE);
        // Broad layout-bearing replay reconstructs the ground before rendering the new placement.
        Rect narrow = frame;
        narrow.right = narrow.left + 14;
        card.layout(narrow);
        FillRect(&frame, &loka::toolbox::kToolboxGradientPatterns[2].pattern);
        CardPixels::capture(frame, ground);
        card.draw();
        CardPixels::capture(frame, actual);
        this->recordArm(size ? "card-large-disabled-no-stale-ink" : "card-minimum-disabled-no-stale-ink",
                        std::memcmp(actual, ground, pixelCount) == 0,
                        COMPLETE);
        card.layout(frame);
        card.draw();
        CardPixels::capture(frame, actual);
        bool restored = true;
        for (std::size_t i = 0; i < pixelCount; ++i)
          restored = restored && actual[i] == (ground[i] || reference[i]);
        this->recordArm(size ? "card-large-reenabled" : "card-minimum-reenabled", restored, COMPLETE);
      }
      const unsigned long stencilStart = TickCount();
      std::FILE *timing = std::fopen("CARDTIME.TXT", "w");
      this->recordArm("card-timing-log-open", timing != 0, COMPLETE);
      if (timing)
      {
        this->checkCardStencilMatrix(timing);
        this->timeCardStencil(timing);
        const unsigned long stencilTicks = TickCount() - stencilStart;
        std::fprintf(timing, "card-new-arms ticks=%lu budget_ticks=3600\r", stencilTicks);
        if (!CloseCardTiming(timing))
          this->result_ = 1;
        this->recordArm("card-stencil-runtime-budget", stencilTicks <= 3600, COMPLETE);
      }
      SetPenState(&savedPen);
    }

    void timeCardStencil(std::FILE *timing)
    {
      InitToolboxProfiler();
      Rect frame;
      SetRect(&frame, 17, 19, 177, 139);
      ToolboxPaintClip clip(frame);
      for (int extent = CARD_SHADOW_DROP; extent <= CARD_SHADOW_WIDE; ++extent)
      {
        CardPixels card(CardStyle(static_cast<CardShadowExtent>(extent)));
        card.layout(frame);
        for (int path = -2; path < CARD_BAND_COUNT; ++path)
        {
          Rect damage = frame;
          if (path == -2)
            SetRect(&damage, 165, 125, 177, 139);
          ToolboxPaintClip partial(damage);
          loka::core::ResetFuncProfile();
          std::fprintf(timing, "card-stack before-draw=%ld bytes\r", static_cast<long>(StackSpace()));
          const unsigned long start = TickCount();
          for (int repeat = 0; repeat < 16; ++repeat)
            card.draw(path < 0 ? -1 : path);
          std::fprintf(
              timing, "card-timing extent=%d path=%d repeats=16 ticks=%lu\r", extent, path, TickCount() - start);
#if LOKA_PROFILE_FUNC_TICKS
          for (int i = 0; i < loka::core::gProfileSlotCount; ++i)
          {
            const loka::core::FuncProfileSlot &slot = *loka::core::gProfileSlots[i];
            if (std::strncmp(slot.func, "card.", 5) == 0)
              std::fprintf(timing, "%s calls=%d ticks=%ld\r", slot.func, slot.callCount, slot.totalTicks);
          }
#else
          std::fprintf(timing, "card-breakdown requires LOKA_PROFILE_FUNC_TICKS=1\r");
#endif
        }
      }
      const unsigned long start = TickCount();
      for (int repeat = 0; repeat < 16; ++repeat)
        CardPixels::reference(frame);
      std::fprintf(timing, "card-v1-whole repeats=16 ticks=%lu\r", TickCount() - start);
    }

    void checkCardStencilMatrix(std::FILE *timing)
    {
      const unsigned long started = TickCount();
      for (int extent = CARD_SHADOW_NONE; extent <= CARD_SHADOW_WIDE; ++extent)
        for (int origin = 0; origin < 8; ++origin)
          for (int residue = 0; residue < 8; ++residue)
          {
            if (extent == CARD_SHADOW_NONE && origin != residue)
              continue;
            const CardStyle style(static_cast<CardShadowExtent>(extent));
            CardPixels card(style), alpha(CardStyle(static_cast<CardShadowExtent>(extent), CARD_SHADOW_ALPHA));
            const int minimumWidth =
                style.geometry().minimumWidth + style.geometry().allowance[0] + style.geometry().allowance[2];
            const int minimumHeight =
                style.geometry().minimumHeight + style.geometry().allowance[1] + style.geometry().allowance[3];
            Rect frame;
            SetRect(&frame,
                    16 + origin,
                    16 + (3 * origin) % 8,
                    16 + origin + minimumWidth + residue,
                    16 + (3 * origin) % 8 + minimumHeight + (5 * residue) % 8);
            ToolboxPaintClip clip(frame);
            EraseRect(&frame);
            const unsigned long oracleStart = TickCount();
            CardPixels::tableReference(frame, style);
            const unsigned long oracleTicks = TickCount() - oracleStart;
            const unsigned long captureStart = TickCount();
            const CardCapture expected(frame);
            const unsigned long captureTicks = TickCount() - captureStart;
            if (!expected.ready())
            {
              this->recordArm("card-matrix-capture-allocation", false, COMPLETE);
              continue;
            }
            const bool captureCorrect = expected.matchesProbe();
            EraseRect(&frame);
            card.layout(frame);
            const unsigned long paintStart = TickCount();
            card.draw();
            const unsigned long paintTicks = TickCount() - paintStart;
            std::fprintf(timing,
                         "card-matrix extent=%d origin=%d size=%d paint_ticks=%lu\r",
                         extent,
                         origin,
                         residue,
                         paintTicks);
            const unsigned long verificationStart = TickCount();
            this->recordArm(extent == CARD_SHADOW_NONE ? "card-none-border-only" : "card-stencil-matrix-whole",
                            captureCorrect && expected.differences() == 0,
                            COMPLETE);
            if (origin == residue)
            {
              EraseRect(&frame);
              alpha.layout(frame);
              alpha.draw();
              this->recordArm("card-alpha-equals-stencil", expected.differences() == 0, COMPLETE);
              FillRect(&frame, &loka::toolbox::kToolboxGradientPatterns[2].pattern);
              card.draw();
              this->recordArm("card-stencil-patterned-ground",
                              expected.differences(&loka::toolbox::kToolboxGradientPatterns[2].pattern) == 0,
                              COMPLETE);
              // Intersect both a corner/run join and the TOP/RIGHT band join.
              for (int seam = 0; seam < 2; ++seam)
              {
                Rect damage;
                const int x = seam ? frame.right - style.geometry().allowance[2] - CardStyle::kBorderBand
                                   : frame.left + style.geometry().allowance[0] + style.geometry().cut[0];
                SetRect(&damage, x - 2, frame.top, x + 2, frame.top + 6);
                {
                  ToolboxPaintClip partial(damage);
                  FillRect(&damage, &loka::toolbox::kToolboxGradientPatterns[2].pattern);
                  card.draw();
                }
                this->recordArm(seam ? "card-band-seam-partial" : "card-tile-run-seam-partial",
                                expected.differences(&loka::toolbox::kToolboxGradientPatterns[2].pattern) == 0,
                                COMPLETE);
              }
            }
            if (extent == CARD_SHADOW_DROP)
            {
              EraseRect(&frame);
              CardPixels::reference(frame);
              const int differences = expected.differences();
              std::fprintf(this->log_, "card-v1 origin=%d size=%d differences=%d\r", origin, residue, differences);
              if (differences)
              {
                expected.dump(this->log_, "v1", true);
                expected.dump(this->log_, "table", false);
              }
            }
            std::fprintf(timing,
                         "card-matrix-work extent=%d origin=%d size=%d oracle_ticks=%lu capture_ticks=%lu "
                         "verification_ticks=%lu\r",
                         extent,
                         origin,
                         residue,
                         oracleTicks,
                         captureTicks,
                         TickCount() - verificationStart);
          }
      const unsigned long ticks = TickCount() - started;
      std::fprintf(timing, "card-matrix total_ticks=%lu budget_ticks=3600\r", ticks);
    }

    void checkFontCursor(ToolboxScenePlatformController &controller, GrafPtr port)
    {
      // This front fixture contains Text, not EditText: hover resolves to arrow.
      controller.cursorOwner()->reconcile();
      const short savedFont = port->txFont;
      const short savedSize = port->txSize;
      TextSize(12);
      TextNode large((Text("MMMMMMMM") + FontSize<24>()).props);
      ToolboxTextContext largeContext(&large, &controller);
      LayoutState state;
      state.x = 12;
      state.y = 80;
      state.width = 64;
      state.lineHeight = 16;
      const char *names[] = {"font-busy-layout-24", "font-busy-layout-24-again",
                             "font-busy-layout-24-other-family"};
      for (int i = 0; i < 3; ++i)
      {
        if (i == 2)
          TextFont(savedFont == 1 ? 0 : 1);
        const ToolboxSceneDebugStats before = controller.debugStatsForTesting();
        LayoutState next = state;
        // An unchanged layout reuses its measurement (a84cf328); dirty props
        // make the repeat and the other-family layout measure again.
        if (i > 0)
          next.inputs = NODE_DIRTY_PROPS;
        largeContext.layout(&controller, next);
        const ToolboxSceneDebugStats after = controller.debugStatsForTesting();
        this->recordArm(names[i], after.cursorOuterEntries == before.cursorOuterEntries + 1
                        && after.cursorOuterExits == before.cursorOuterExits + 1
                        && after.cursorNativeApplies == before.cursorNativeApplies + 2
                        && after.cursorDepth == 0
                        && after.cursorLastApplied == CursorOwner::ARROW, TEXT_WIDTH_WRITE);
      }
      TextFont(savedFont);
      TextNode ordinary(Text("M").props);
      ToolboxTextContext ordinaryContext(&ordinary, &controller);
      TextNode explicitDefault((Text("M") + FontSize<12>()).props);
      ToolboxTextContext explicitDefaultContext(&explicitDefault, &controller);
      const char *defaultNames[] = {"font-busy-default-no-apply", "font-busy-zero-default-no-apply",
                                    "font-busy-resolved-default-no-apply"};
      for (int i = 0; i < 3; ++i)
      {
        if (i == 1)
          TextSize(0);
        const ToolboxSceneDebugStats before = controller.debugStatsForTesting();
        LayoutState next = state;
        if (i == 2)
          explicitDefaultContext.layout(&controller, next);
        else
          ordinaryContext.layout(&controller, next);
        const ToolboxSceneDebugStats after = controller.debugStatsForTesting();
        this->recordArm(defaultNames[i], after.cursorOuterEntries == before.cursorOuterEntries
                        && after.cursorOuterExits == before.cursorOuterExits
                        && after.cursorNativeApplies == before.cursorNativeApplies
                        && after.cursorDepth == 0, TEXT_WIDTH_WRITE);
      }
      TextSize(12);
      LayoutState next = state;
      largeContext.layout(&controller, next);
      Point pen;
      GetPen(&pen);
      const ToolboxSceneDebugStats before = controller.debugStatsForTesting();
      // render includes paint and the hit-width measurement, under one borrow.
      largeContext.render(&controller);
      const ToolboxSceneDebugStats after = controller.debugStatsForTesting();
      this->recordArm("font-busy-paint-24", after.cursorOuterEntries == before.cursorOuterEntries + 1
                      && after.cursorOuterExits == before.cursorOuterExits + 1
                      && after.cursorNativeApplies == before.cursorNativeApplies + 2
                      && after.cursorDepth == 0
                      && after.cursorLastApplied == CursorOwner::ARROW, TEXT_WIDTH_WRITE);
      // render registered a hit; retire it before this stack context disappears.
      largeContext.onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
      Rect scratch;
      SetRect(&scratch, 12, 80, 76, 148);
      EraseRect(&scratch);
      MoveTo(pen.h, pen.v);
      TextFont(savedFont);
      TextSize(savedSize);
    }

    /** Blocking Toolbox work with no path to the app borrows the owner that
        ToolboxApp registered around its run loop: a whole-file read and an
        image decode each enter and leave the watch once (#1066). */
    void checkBlockingWorkCursor(ToolboxScenePlatformController &controller, Phase next)
    {
      {
        const ToolboxSceneDebugStats before = controller.debugStatsForTesting();
        loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
        const bool read = loka::platform::file::ReadBytes(this->file_, bytes) == loka::platform::file::READ_OK;
        const ToolboxSceneDebugStats after = controller.debugStatsForTesting();
        this->recordArm("busy-file-read", read
                        && after.cursorOuterEntries == before.cursorOuterEntries + 1
                        && after.cursorOuterExits == before.cursorOuterExits + 1
                        && after.cursorDepth == 0, next);
      }
      {
        loka::core::resource::Blob blob = loka::core::resource::Blob::Create();
        // A refused allocation fails the arm instead of asserting in a Release run.
        const bool sized = blob.tryResize(64);
        if (sized)
        {
          std::memset(blob.mutableData(), 0, 64);
          blob.sealBytes();
        }
        ToolboxPlatformContext platform;
        loka::core::resource::Image image;
        const ToolboxSceneDebugStats before = controller.debugStatsForTesting();
        const bool decoded = sized && platform.createImageFromBlob(blob, 0, blob.size(), image);
        const ToolboxSceneDebugStats after = controller.debugStatsForTesting();
        // Not a picture: the decode refuses after its parse, still under one borrow.
        this->recordArm("busy-image-decode", sized && !decoded && !image.isValid()
                        && after.cursorOuterEntries == before.cursorOuterEntries + 1
                        && after.cursorOuterExits == before.cursorOuterExits + 1
                        && after.cursorDepth == 0, next);
      }
    }

    void checkTextInks(ToolboxScenePlatformController &controller)
    {
      Rect scratch;
      SetRect(&scratch, 12, 80, 100, 112);
      const loka::app::TextStyle inks[] = {
          loka::app::TextStyle(White), loka::app::TextStyle(Black), loka::app::TextStyle()};
      const char *names[] = {"ink-white-empty", "ink-black-visible", "ink-unset-visible"};
      for (unsigned i = 0; i < 3; ++i)
      {
        EraseRect(&scratch);
        TextNode text((Text("MMMM") + FontSize<12>() + inks[i]).props);
        ToolboxTextContext context(&text, &controller);
        LayoutState seat;
        seat.x = 12;
        seat.y = 80;
        seat.width = 88;
        seat.lineHeight = 16;
        context.layout(&controller, seat);
        context.repaint();
        const int pixels = CountTextStyleInk(scratch);
        this->recordArm(names[i], i == 0 ? pixels == 0 : pixels > 0, TEXT_WIDTH_WRITE);
      }
      EraseRect(&scratch);
      // Chicago's known M advance also gives a deterministic expected log.
      const AttributedString uniform = Styled("MMMM", FontSize<12>());
      const AttributedString split = Styled("M", FontSize<12>() + Red) + Styled("MMM", FontSize<12>());
      ToolboxAttributedTextTable whole, colored;
      const bool built =
          whole.build(uniform, BlockStyle(), 88, controller) && colored.build(split, BlockStyle(), 88, controller);
      this->recordArm("ink-additivity-build", built, TEXT_WIDTH_WRITE);
      if (!built)
        return;
      std::fprintf(
          this->log_, "ink-widths uniform=%d split=%d\r", whole.lines().line(0).width, colored.lines().line(0).width);
      this->recordArm(
          "ink-additivity-width", whole.lines().line(0).width == colored.lines().line(0).width, TEXT_WIDTH_WRITE);
      const BlockStyle wrap = BlockStyle().wrap(TEXT_WRAP_CHAR);
      const bool wrapped = whole.build(uniform, wrap, 18, controller) && colored.build(split, wrap, 18, controller);
      bool same = wrapped && whole.lines().lineCount() == colored.lines().lineCount();
      if (same)
        for (std::size_t i = 0; i < whole.lines().lineCount(); ++i)
        {
          const TextLineRecord &a = whole.lines().line(i), &b = colored.lines().line(i);
          const TextFragment &ae = whole.lines().fragment(a.firstFragment + a.fragmentCount - 1);
          const TextFragment &be = colored.lines().fragment(b.firstFragment + b.fragmentCount - 1);
          same = same && a.width == b.width && ae.end == be.end;
        }
      this->recordArm("ink-additivity-wrap", same && whole.lines().lineCount() == 2, TEXT_WIDTH_WRITE);
    }

    void checkTextFonts(ToolboxScenePlatformController &controller, GrafPtr port)
    {
      this->checkFontCursor(controller, port);
      const short portFont = port->txFont;
      const short portSize = port->txSize;
      const Style portFace = port->txFace;
      const loka::app::TextStyle normal = loka::app::TextStyle().weight(TEXT_WEIGHT_NORMAL).italic(false);
      const loka::app::TextStyle styles[] = {
          FontSize<9>() + normal, FontSize<12>() + normal,
          FontSize<18>() + normal, FontSize<24>() + normal,
          FontSize<12>() + normal + Bold, FontSize<12>() + normal + Italic};
      const char *restoreNames[] = {
          "font-9-restores-port", "font-12-restores-port",
          "font-18-restores-port", "font-24-restores-port",
          "font-bold-restores-port", "font-italic-restores-port"};
      const char *selectionNames[] = {
          "font-9-selects-descriptor", "font-12-selects-descriptor",
          "font-18-selects-descriptor", "font-24-selects-descriptor",
          "font-bold-selects-descriptor", "font-italic-selects-descriptor"};
      const short sizes[] = {9, 12, 18, 24, 12, 12};
      const Style faces[] = {0, 0, 0, 0, bold, italic};
      short widths[6];
      for (int i = 0; i < 6; ++i)
      {
        {
          const ToolboxTextFontDescriptor descriptor(styles[i]);
          ToolboxTextMeasureScope scope(controller, descriptor);
          widths[i] = scope.measure(loka::core::String::Literal("MMMMMMMM"));
          this->recordArm(selectionNames[i], port->txFont == (portFont == 0 ? GetSysFont() : portFont)
                          && port->txSize == sizes[i]
                          && port->txFace == static_cast<Style>((portFace & ~(bold | italic)) | faces[i]),
                          TEXT_WIDTH_WRITE);
        }
        GrafPtr restored = 0;
        GetPort(&restored);
        this->recordArm(restoreNames[i], restored == port && port->txFont == portFont
                        && port->txSize == portSize && port->txFace == portFace, TEXT_WIDTH_WRITE);
      }
      std::fprintf(this->log_, "font-widths 9=%d 12=%d 18=%d 24=%d bold=%d italic=%d\r",
                   widths[0], widths[1], widths[2], widths[3], widths[4], widths[5]);
      this->recordArm("font-width-9-lt-12", widths[0] < widths[1], TEXT_WIDTH_WRITE);
      this->recordArm("font-width-12-lt-18", widths[1] < widths[2], TEXT_WIDTH_WRITE);
      this->recordArm("font-width-18-lt-24", widths[2] < widths[3], TEXT_WIDTH_WRITE);
      this->recordArm("font-bold-12-gt-normal", widths[4] > widths[1], TEXT_WIDTH_WRITE);
      this->recordArm("font-italic-12-ge-normal", widths[5] >= widths[1], TEXT_WIDTH_WRITE);

      loka::app::TextStyle unsnapped;
      unsnapped.size_ = loka::app::TextSizeValue::points(11);
      this->recordArm("font-size-tie-snaps-down", ToolboxTextFontDescriptor(unsnapped).size(12) == 10,
                      TEXT_WIDTH_WRITE);
      {
        ToolboxTextMeasureScope scope(controller, ToolboxTextFontDescriptor(SmallLabel));
        this->recordArm(
            "font-small-label-geneva-9", port->txFont == kFontIDGeneva && port->txSize == 9, TEXT_WIDTH_WRITE);
      }
      this->recordArm("font-small-label-restores-port",
                      port->txFont == portFont && port->txSize == portSize && port->txFace == portFace,
                      TEXT_WIDTH_WRITE);
      const ToolboxTextFontDescriptor inherited;
      this->recordArm("font-unset-inherits-port", inherited.font(portFont) == portFont
                      && inherited.size(17) == 17 && inherited.face(bold | italic) == (bold | italic),
                      TEXT_WIDTH_WRITE);
      this->recordArm("font-explicit-face-clears-only-own-bits",
                      ToolboxTextFontDescriptor(normal).face(bold | italic | underline) == underline,
                      TEXT_WIDTH_WRITE);

      TextNode small((Text("MMMMMMMM") + FontSize<12>() + normal).props);
      TextNode large((Text("MMMMMMMM") + FontSize<24>() + normal).props);
      ToolboxTextContext smallContext(&small, &controller);
      ToolboxTextContext largeContext(&large, &controller);
      LayoutState smallState;
      smallState.lineHeight = 16;
      LayoutState largeState = smallState;
      smallContext.layout(&controller, smallState);
      largeContext.layout(&controller, largeState);
      std::fprintf(this->log_, "font-layout-height 12=%d 24=%d\r", smallState.y, largeState.y);
      this->recordArm("font-text-24-taller-than-12", largeState.y > smallState.y, TEXT_WIDTH_WRITE);
      this->recordArm("font-text-visible-width-matches-descriptor",
                      smallContext.visibleWidth() == widths[1] && largeContext.visibleWidth() == widths[3],
                      TEXT_WIDTH_WRITE);
      this->recordArm("font-text-24-height-34", largeState.y == 34, TEXT_WIDTH_WRITE);

      // Pre-PR Chicago 12 contract: unset size uses height 16, baseline 10,
      // and line pitch 12. Exercise actual layout and paint, not a test getter.
      // The fixture's Row ends above y=80; this scratch area contains no sibling.
      Point previousPen;
      GetPen(&previousPen);
      TextNode legacy(Text("M").props);
      ToolboxTextContext legacyContext(&legacy, &controller);
      LayoutState legacyState;
      legacyState.x = 12;
      legacyState.y = 80;
      legacyState.width = 64;
      legacyState.lineHeight = 12;
      LayoutState wrappedState = legacyState;
      legacyContext.layout(&controller, legacyState);
      legacyContext.repaint();
      Point baseline;
      GetPen(&baseline);
      std::fprintf(this->log_, "font-unset-geometry height=%d baseline=%d\r",
                   legacyState.y - 80, baseline.v - 80);
      this->recordArm("font-unset-height-16", legacyState.y - 80 == 16, TEXT_WIDTH_WRITE);
      this->recordArm("font-unset-baseline-10", baseline.v - 80 == 10, TEXT_WIDTH_WRITE);

      TextNode wrapped((Text("M\nM") + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
      ToolboxTextContext wrappedContext(&wrapped, &controller);
      wrappedContext.layout(&controller, wrappedState);
      wrappedContext.repaint();
      GetPen(&baseline);
      this->recordArm("font-unset-wrap-pitch-12", wrappedState.y - 80 == 28, TEXT_WIDTH_WRITE);
      this->recordArm("font-unset-wrap-baseline-22", baseline.v - 80 == 22, TEXT_WIDTH_WRITE);
      Rect scratch;
      SetRect(&scratch, 12, 80, 76, 148);
      EraseRect(&scratch);
      this->checkTextInks(controller);
      MoveTo(previousPen.h, previousPen.v);
    }

    static void OnTextWidthIdle(Window *window, TextWidthNode *node, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == TEXT_WIDTH_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = TEXT_WIDTH_WRITE;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      Node *first = 0;
      Node *second = 0;
      Node *surface = 0;
      loka::dsl::FlowError error;
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "TextWidth.First", first, error);
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "TextWidth.Second", second, error);
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "TextWidth.Surface", surface, error);
      if (!controller || !node || !first || !first->getContext() || !second || !second->getContext()
          || !surface || !surface->getContext())
      {
        self->finish(false);
        return;
      }
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      NativeNodeContext *firstContext = static_cast<NativeNodeContext *>(first->getContext());
      NativeNodeContext *secondContext = static_cast<NativeNodeContext *>(second->getContext());
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      const PaintAnswer sibling = secondContext->queryPaintDamage(query);
      if (self->phase_ == TEXT_WIDTH_WRITE)
      {
        self->checkTextFonts(*controller, reinterpret_cast<GrafPtr>(native->window()));
        const PaintAnswer before = firstContext->queryPaintDamage(query);
        const PaintAnswer sprite = static_cast<NativeNodeContext *>(surface->getContext())->queryPaintDamage(query);
        GrafPtr ambientPort = self->paintWindow_
            ? reinterpret_cast<GrafPtr>(self->paintWindow_->asToolboxWindow()->window()) : 0;
        GrafPtr targetPort = reinterpret_cast<GrafPtr>(native->window());
        const short targetFont = targetPort->txFont;
        const short targetSize = targetPort->txSize;
        const Style targetFace = targetPort->txFace;
        bool measureScopeRestored = false;
        short wideWidth = 0;
        short narrowWidth = 0;
        if (ambientPort && ambientPort != targetPort)
        {
          SetPort(ambientPort);
          const short ambientFont = ambientPort->txFont;
          const short ambientSize = ambientPort->txSize;
          const Style ambientFace = ambientPort->txFace;
          TextFont(4);
          TextSize(24);
          TextFace(bold);
          wideWidth = controller->measureTextWidth(loka::core::String::Literal("MMMMMMMM"));
          // Equal byte counts are the mutation control: the deleted
          // utf8.size() heuristic reports these two strings as equal.
          narrowWidth = controller->measureTextWidth(loka::core::String::Literal("IIIIIIII"));
          GrafPtr restoredPort = 0;
          GetPort(&restoredPort);
          measureScopeRestored = restoredPort == ambientPort
              && ambientPort->txFont == 4
              && ambientPort->txSize == 24
              && ambientPort->txFace == bold
              && targetPort->txFont == targetFont
              && targetPort->txSize == targetSize
              && targetPort->txFace == targetFace;
          TextFont(ambientFont);
          TextSize(ambientSize);
          TextFace(ambientFace);
          SetPort(native->window());
        }
        const bool setup = before.kind == PAINT_ANSWER_EXACT && sibling.kind == PAINT_ANSWER_EXACT
                           && sprite.kind == PAINT_ANSWER_EXACT
                           && GetPixel(sprite.damage.x + 8, sprite.damage.y + 8)
                           && node->captureSibling(sibling.damage)
                           && measureScopeRestored
                           && wideWidth > narrowWidth;
        std::fprintf(self->log_, "row-text-seat-setup sibling_x=%d sibling_y=%d\r",
                     sibling.damage.x, sibling.damage.y);
        self->recordArm("row-text-seat-setup", setup, TEXT_WIDTH_CHECK);
        self->initial_ = controller->debugStatsForTesting();
        node->advance();
        // The Row seats a Text by flex allocation, not by its measured value,
        // so a wider value is exact paint inside the same seat: no refusal,
        // and the damage stops before the sibling's column.
        const PaintAnswer wider = firstContext->queryPaintDamage(query);
        std::fprintf(self->log_, "row-text-wider-exact answer=%d reason=%d right=%d sibling_x=%d\r",
                     static_cast<int>(wider.kind), static_cast<int>(wider.reason),
                     wider.damage.x + wider.damage.width, static_cast<int>(node->siblingColumn()));
        self->recordArm("row-text-wider-exact", wider.kind == PAINT_ANSWER_EXACT && wider.damage.width > 0
                        && wider.damage.x + wider.damage.width <= node->siblingColumn(), TEXT_WIDTH_CHECK);
        SetPort(previousPort);
        return;
      }
      if (self->phase_ == TEXT_WIDTH_CHECK)
      {
        const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
        const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
        const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
        const bool seated = sibling.kind == PAINT_ANSWER_EXACT && node->siblingSeated(sibling.damage);
        SetPort(previousPort);
        std::fprintf(self->log_, "row-text-sibling-seated whole_window=%d rect_requests=%d sibling_x=%d seated=%d\r",
                     whole, rects, sibling.damage.x, seated ? 1 : 0);
        self->recordArm("row-text-sibling-seated", whole == 0 && rects >= 1 && seated, TEXT_WIDTH_RELAYOUT);
        // The dirty replay above ran no Row layout, so the sibling's column was
        // still its pre-write placement. Force a full render, which lays the
        // Row out again with the wider value, and check the seat once more.
        native->requestInvalidate();
        return;
      }
      const bool relaid = sibling.kind == PAINT_ANSWER_EXACT && node->siblingSeated(sibling.damage);
      const PaintAnswer wider = firstContext->queryPaintDamage(query);
      SetPort(previousPort);
      std::fprintf(self->log_, "row-text-relayout-keeps-seat sibling_x=%d seated=%d first_right=%d\r",
                   sibling.damage.x, relaid ? 1 : 0, wider.damage.x + wider.damage.width);
      self->recordArm("row-text-relayout-keeps-seat", relaid && wider.kind == PAINT_ANSWER_EXACT
                      && wider.damage.x + wider.damage.width <= node->siblingColumn(), TEXT_STYLE_SHOW);
    }

    static void OnTextStyleIdle(Window *window, TextStyleChangeNode *node, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == TEXT_STYLE_SHOW)
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = TEXT_STYLE_WRITE;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      TextNode *text = 0;
      TextNode *sibling = 0;
      loka::dsl::FlowError error;
      loka::dsl::testing::LookupNodeById<TextNode>(window->scene(), "TextStyleChange.Text", text, error);
      loka::dsl::testing::LookupNodeById<TextNode>(window->scene(), "TextStyleChange.Sibling", sibling, error);
      if (!controller || !node || !text || !text->getContext() || !sibling || !sibling->getContext())
      {
        self->recordArm("font-live-style-setup", false, COMPLETE);
        self->finish(false);
        return;
      }
      ToolboxTextContext *textContext = static_cast<ToolboxTextContext *>(text->getContext());
      ToolboxTextContext *siblingContext = static_cast<ToolboxTextContext *>(sibling->getContext());
      const Rect textRect = loka::testing::ToolboxTextContextAccess::rect(*textContext);
      const Rect siblingRect = loka::testing::ToolboxTextContextAccess::rect(*siblingContext);
      const short baseline = loka::testing::ToolboxTextContextAccess::baseline(*textContext);
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      FontInfo largeFont;
      short largeWidth;
      {
        const ToolboxTextFontDescriptor descriptor((FontSize<24>()));
        ToolboxTextMeasureScope measure(*controller, descriptor);
        GetFontInfo(&largeFont);
        largeWidth = measure.measure(loka::core::String::Literal("M"));
      }
      if (self->phase_ == TEXT_STYLE_WRITE)
      {
        Rect oldInkRow;
        SetRect(&oldInkRow, 0, 0, 0, 0);
        // Outside the wider M's horizontal ink span, old sibling ink must
        // become background when the sibling moves down. Prove ink exists first.
        for (short y = siblingRect.top; y < siblingRect.bottom; ++y)
        {
          const Rect row = {y, static_cast<short>(textRect.left + largeWidth + 4),
                            static_cast<short>(y + 1), siblingRect.right};
          if (CountTextStyleInk(row) > 0)
            oldInkRow = row;
        }
        int points = 0;
        const bool setup = textRect.bottom - textRect.top == 16 && text->props.textStyleState_
                           && text->props.resolvedTextStyle().size_.queryPoints(points) && points == 12
                           && !EmptyRect(&oldInkRow) && CountTextStyleInk(textRect) > 0;
        self->textStyleBefore_ = TextStyleChangeBefore(textRect, siblingRect, oldInkRow, baseline);
        SetPort(previousPort);
        self->recordArm("font-live-style-setup", setup, TEXT_STYLE_CHECK);
        self->initial_ = controller->debugStatsForTesting();
        node->enlarge();
        // Only DispatchIdle's normal scene/native flush runs before CHECK.
        return;
      }
      const short expectedBaseline = static_cast<short>(textRect.top + largeFont.ascent);
      const Rect glyphBand = {static_cast<short>(expectedBaseline - 3), textRect.left,
                              expectedBaseline, static_cast<short>(textRect.left + largeWidth)};
      const int glyphInk = CountTextStyleInk(glyphBand);
      const int oldInk = CountTextStyleInk(self->textStyleBefore_.oldInkRow());
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      // begin() resets per-change request counts; totalRenderCalls survives it.
      // Pair the render delta with new geometry to prove layout completed.
      const int renders = stats.totalRenderCalls - self->initial_.totalRenderCalls;
      std::fprintf(self->log_,
                   "font-live-style height=%d sibling_dy=%d baseline=%d expected=%d glyph_ink=%d old_ink=%d renders=%d\r",
                   textRect.bottom - textRect.top, siblingRect.top - self->textStyleBefore_.sibling().top,
                   baseline, expectedBaseline, glyphInk, oldInk, renders);
      SetPort(previousPort);
      self->recordArm("font-live-style-height-34", textRect.bottom - textRect.top == 34, OFFSCREEN_SHOW);
      self->recordArm("font-live-style-sibling-down-18",
                      siblingRect.top - self->textStyleBefore_.sibling().top == 18, OFFSCREEN_SHOW);
      self->recordArm("font-live-style-new-baseline-ink",
                      baseline == expectedBaseline && baseline > self->textStyleBefore_.baseline()
                      && textRect.top == self->textStyleBefore_.text().top
                      && glyphBand.top >= textRect.top && glyphBand.bottom <= textRect.bottom && glyphInk > 0,
                      OFFSCREEN_SHOW);
      self->recordArm("font-live-style-old-sibling-row-erased",
                      !EmptyRect(&self->textStyleBefore_.oldInkRow())
                      && self->textStyleBefore_.oldInkRow().bottom <= siblingRect.top && oldInk == 0,
                      OFFSCREEN_SHOW);
      self->recordArm("font-live-style-layout-rendered",
                      renders > 0 && textRect.bottom - textRect.top == 34, OFFSCREEN_SHOW);
    }

    static void OnButtonIdle(Window *window, ButtonExactNode *node, bool label, PaintDamageConfig *self)
    {
      ToolboxWindow *native = window->asToolboxWindow();
      if (self->phase_ == (label ? BUTTON_LABEL_SHOW : BUTTON_ENABLED_SHOW))
      {
        ShowWindow(native->window());
        SelectWindow(native->window());
        native->requestInvalidate();
        self->phase_ = label ? BUTTON_LABEL_WRITE : BUTTON_ENABLED_WRITE;
        return;
      }
      ToolboxScenePlatformController *controller = window->scene()
          ? static_cast<ToolboxScenePlatformController *>(
              loka::dsl::testing::SceneTestAccess::platformController(*window->scene())) : 0;
      Node *button = 0;
      Node *surface = 0;
      loka::dsl::FlowError error;
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "ButtonExact.Button", button, error);
      loka::dsl::testing::LookupNodeById<Node>(window->scene(), "ButtonExact.Surface", surface, error);
      if (!controller || !node || !button || !button->getContext() || !surface || !surface->getContext())
      {
        self->finish(false);
        return;
      }
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      NativeNodeContext *context = static_cast<NativeNodeContext *>(button->getContext());
      GrafPtr previousPort;
      GetPort(&previousPort);
      SetPort(native->window());
      if (self->phase_ == (label ? BUTTON_LABEL_WRITE : BUTTON_ENABLED_WRITE))
      {
        const PaintAnswer face = context->queryPaintDamage(query);
        const PaintAnswer sprite = static_cast<NativeNodeContext *>(surface->getContext())->queryPaintDamage(query);
        // Since #1015 the Button takes its Column's width and centres the title,
        // so the 64-pixel title sample sits at the native control's centre.
        SetRect(&self->editGeometry_.view, face.damage.x, face.damage.y, face.damage.x + 64, face.damage.y + 14);
        if (ControlHandle control = reinterpret_cast<ControlHandle>(
                reinterpret_cast<WindowPeek>(native->window())->controlList))
        {
          const Rect bounds = (**control).contrlRect;
          const short width = static_cast<short>(bounds.right - bounds.left);
          const short left = static_cast<short>(bounds.left + (width > 64 ? (width - 64) / 2 : 0));
          SetRect(&self->editGeometry_.view, left, bounds.top, left + 64, bounds.top + 14);
        }
        std::fprintf(self->log_, "button-title-sample=(%d,%d,%d,%d)\r", self->editGeometry_.view.left,
                     self->editGeometry_.view.top, self->editGeometry_.view.right, self->editGeometry_.view.bottom);
        self->marker_.h = static_cast<short>(sprite.damage.x + 8);
        self->marker_.v = static_cast<short>(sprite.damage.y + 8);
        self->buttonTitlePixels_.capture(self->editGeometry_.view);
        const bool setup = face.kind == PAINT_ANSWER_EXACT && sprite.kind == PAINT_ANSWER_EXACT
                           && GetPixel(self->marker_.h, self->marker_.v);
        SetPort(previousPort);
        self->recordArm(label ? "button-label-setup" : "button-enabled-setup", setup,
                        label ? BUTTON_LABEL_CHECK : BUTTON_ENABLED_CHECK);
        if (label)
        {
          const short beforeWidth = controller->measureTextWidth(loka::core::String::Literal("MMMMIIII"));
          const short afterWidth = controller->measureTextWidth(loka::core::String::Literal("IIIIMMMM"));
          std::fprintf(self->log_, "button-label-widths before=%d after=%d\r", beforeWidth, afterWidth);
          self->recordArm("button-label-equal-width", beforeWidth == afterWidth, BUTTON_LABEL_CHECK);
        }
        self->initial_ = controller->debugStatsForTesting();
        node->advance(label);
        return;
      }
      // Disabled CDEF text removes alternating black title pixels. Permuting
      // M/I preserves width but moves ink; neither sample includes the frame.
      const int erased = self->buttonTitlePixels_.erasedInk(self->editGeometry_.view);
      const bool sprite = GetPixel(self->marker_.h, self->marker_.v) != 0;
      SetPort(previousPort);
      const ToolboxSceneDebugStats &stats = controller->debugStatsForTesting();
      const int whole = stats.windowFullRequestCount - self->initial_.windowFullRequestCount;
      const int rects = stats.windowRectRequestCount - self->initial_.windowRectRequestCount;
      const char *name = label ? "button-label-exact" : "button-enabled-exact";
      std::fprintf(self->log_, "%s whole_window=%d rect_requests=%d title_ink_removed=%d sprite_preserved=%d\r",
                   name, whole, rects, erased, sprite ? 1 : 0);
      self->recordArm(name, whole == 0 && rects >= 1 && erased > 0 && sprite,
                      label ? COMPLETE : BUTTON_LABEL_SHOW);
      const PaintAnswer unchanged = context->queryPaintDamage(query);
      self->recordArm(label ? "button-label-presented-empty" : "button-enabled-presented-empty",
                      unchanged.kind == PAINT_ANSWER_EXACT && unchanged.damage.width == 0,
                      label ? COMPLETE : BUTTON_LABEL_SHOW);
      if (label)
      {
        PaintQuery unsettled = query;
        unsettled.placement = PLACEMENT_PENDING;
        self->recordArm("button-placement-refuses",
                        context->queryPaintDamage(unsettled).kind == PAINT_ANSWER_REFUSED, COMPLETE);
        GetPort(&previousPort);
        SetPort(native->window());
        Rect partial = self->editGeometry_.view;
        partial.right = static_cast<short>(partial.left + 24);
        {
          ToolboxPaintClip clip(partial);
          controller->drawControlsInRect(partial);
        }
        // #1027: the same rule for the Button's partial native redraw.
        self->recordArm("button-partial-unchanged-keeps-history",
                        context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT, COMPLETE);
        controller->drawControlsInRect(native->window()->portRect);
        self->recordArm("button-full-history-restored",
                        context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT, COMPLETE);
        SetPort(previousPort);
        node->widenTitle();
        const PaintAnswer widerTitle = context->queryPaintDamage(query);
        std::fprintf(self->log_, "button-label-wider answer=%d reason=%d\r",
                     static_cast<int>(widerTitle.kind), static_cast<int>(widerTitle.reason));
        // #1015: a container-sized Button keeps its rectangle when the title
        // widens and answers exact; only a text-sized one refuses (host pins).
        self->recordArm("button-label-wider-exact", widerTitle.kind == PAINT_ANSWER_EXACT, COMPLETE);
        controller->destroyButtonControl(911, NATIVE_HINT_DEFAULT);
        self->recordArm("button-native-retired-refuses",
                        context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED, COMPLETE);
        self->phase_ = TEXT_WIDTH_SHOW;
      }
    }

    static void OnEditIdle(Window *window, double, void *data)
    {
      PaintDamageConfig *self = static_cast<PaintDamageConfig *>(data);
      if (self->phase_ != EDIT_WRITE && self->phase_ != EDIT_CHECK)
        return;
      ToolboxWindow *native = window ? window->asToolboxWindow() : 0;
      if (!native || !window->scene() || !self->edit_)
      {
        self->finish(false);
        return;
      }
      ToolboxScenePlatformController *controller = static_cast<ToolboxScenePlatformController *>(
          loka::dsl::testing::SceneTestAccess::platformController(*window->scene()));
      FindEdit edit;
      enumerateAttachedResidents(self->edit_, edit);
      ToolboxScenePlatformController::EditTextGeometry geometry;
      if (!controller || !controller->queryEditTextGeometryForTesting(edit.context, geometry))
      {
        // A missing native geometry is a fixture failure, not a silent end.
        self->recordArm("edit-geometry", false, COMPLETE);
        self->finish(false);
        return;
      }
      if (self->phase_ == EDIT_WRITE)
      {
        const Rect chrome = edit.context->chromeRect();
        const bool clipped = geometry.view.bottom < chrome.bottom - 1;
        std::fprintf(self->log_, "edit_initially_clipped=%d\r", clipped ? 1 : 0);
        if (!clipped)
        {
          self->finish(false);
          return;
        }
        self->editGeometry_ = geometry;
        native->requestInvalidateRect(chrome);
        self->phase_ = EDIT_CHECK;
        return;
      }
      const bool same = EqualRect(&geometry.destination, &self->editGeometry_.destination)
                        && EqualRect(&geometry.view, &self->editGeometry_.view);
      std::fprintf(self->log_, "edit_replay_preserves_projection=%d\r", same ? 1 : 0);
      self->recordArm("edit-replay", same, BOUNDS_SHOW);
    }
  };
}

int main(int, char **)
{
  return loka::standalone_tests::RunStandaloneFlowWithConfig<PaintDamageConfig>(0, 0);
}
