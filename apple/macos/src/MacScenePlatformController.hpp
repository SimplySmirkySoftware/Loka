#ifndef LOKA_MAC_SCENE_PLATFORM_CONTROLLER_HPP
#define LOKA_MAC_SCENE_PLATFORM_CONTROLLER_HPP

#include <vector>
#include "app/layout/RowLayout.hpp"
#include "core/resource/Image.hpp"
#include "app/layout/TextShaping.hpp"
#include "app/layout/NaturalWidthSource.hpp"
#include "platform/MacProjection.hpp"
#include "app/RectSurface.hpp"
#include "app/style/StyleVocab.hpp"
#include "app/scene/projection/ProjectionParentScope.hpp"
#include "app/scene/projection/PlatformController.hpp"
#include "app/scene/projection/PlatformLayoutHandler.hpp"
#include "app/scene/projection/PlatformNodeHandler.hpp"

class PlatformContext;

namespace loka
{
  namespace macos
  {
    /** Candidate rail values; shared by scene and pre-scene window projection. */
    app::RailMetrics DefaultRailMetrics();
  }
}

namespace loka
{
  namespace dsl
  {
    namespace testing
    {
      class MacScenePlatformTestAccess;
    }
  } // namespace dsl
} // namespace loka

namespace loka
{
  namespace core
  {
    namespace scene
    {
      class Node;
    }
  } // namespace core

  namespace app
  {
    struct TextStyle;
    class RectSurfaceNode;
    class ScrollViewNode;

    namespace scene
    {
      class MacPlatformLayoutTraversal;
    }
  } // namespace app
} // namespace loka

class MacScenePlatformController : public loka::app::scene::IPlatformController,
    public loka::app::layout::INaturalWidthSource
{
public:
  virtual bool queryNaturalWidth(loka::app::scene::Node *child, short &width) const;
  /** Push-button cell measurement in Row allocation units; zero declines. */
  short measurePushButtonNaturalWidth(const loka::core::String &label) const;
  /** Row metrics measured once at construction: the native push-button cell
      height, or kButtonHeight when that measurement declined. Row alignment
      and Button layout both read this one value. */
  const loka::app::layout::RowLayoutMetrics &rowLayoutMetrics() const
  {
    return this->rowLayoutMetrics_;
  }
  /** Retire door for a RectSurface context: takes back that surface's pending
      seat rows so a surface reclaimed during delivery publishes nothing. */
  void cancelRectSurfaceExtent(loka::app::RectSurfaceNode *surface)
  {
    this->rectSurfaceExtentLedger_.cancel(surface);
  }
  struct LayoutState
  {
    typedef int Coordinate;

    int x;
    int y;
    int width;
    int height;
    int spacing;
  };

  struct LayoutNodeResult
  {
    LayoutNodeResult()
        : boundaryWidth(0),
          resultY(0)
    {
    }
    LayoutNodeResult(int width, int y)
        : boundaryWidth(width),
          resultY(y)
    {
    }

    int boundaryWidth;
    int resultY;
  };

  MacScenePlatformController(void *rootView, const loka::app::RailMetrics &metrics,
                             PlatformContext *platformContext = 0);
  /** Borrows the Window's ancestor context; an ownerless controller refuses. */
  bool captureViewBitmap(void *view, loka::core::resource::Image &out) const;
  virtual ~MacScenePlatformController();
  loka::app::TextShaping textShaping() const { return this->textShaping_; }

  virtual void onChange(loka::app::scene::Node *rootNode, loka::app::scene::NodeDirtyFlags flags, bool fullRebuild);
  virtual void onBoundaryApply(loka::app::scene::Node *rootNode,
                               loka::app::scene::BoundaryNode *boundary,
                               const loka::app::scene::BoundaryLocalApplyInfo &info,
                               const loka::app::scene::PlatformApplyPlan &plan);
  virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const
  {
    return true;
  }
  virtual bool readNativeFocus(loka::app::scene::NodeContext *&out);
  virtual bool applyNativeFocus(loka::app::scene::NodeContext &ctx);
  virtual void synchronize();
  virtual bool hasPendingSync() const;
  virtual void drainNativeRetirements();
  virtual void destroy();
  virtual void releaseMenu();
  virtual void releaseNodeContexts(loka::app::scene::Node *node);
  virtual bool prepareProjectedLayout(loka::app::scene::Node *node, loka::app::scene::LayoutState &state);
  virtual bool registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler);

  const loka::macos::MacProjection &projection() const { return this->projection_; }

  void relayout(int clientWidth, int clientHeight);
  void requestRelayout();
  /** Borrowed NSFont, retained by this controller until destruction. */
  void *textFont(const loka::app::TextStyle &style, bool fixedPitch = false) const;
  bool hasPendingRelayout() const
  {
    return relayoutPending_;
  }
  static MacScenePlatformController *findForRootView(void *rootView);
  static void flushPendingRelayouts();
  void *rootView() const { return rootView_; }
  /** Native parent for a projected child. The root remains the default;
      an active ScrollView scope supplies its document view. */
  void *projectionParentView() const
  {
    return this->projectionParentScopes_.current().nativeParent;
  }
  void queueNativeRetirement(void *primary, void *auxiliary = 0);

private:
  friend class MacInputDoor;
  void handlePendingRelayout();
  friend class ::loka::dsl::testing::MacScenePlatformTestAccess;
  friend class ::loka::app::scene::MacPlatformLayoutTraversal;
  friend void RegisterMacBuiltInSupport(MacScenePlatformController &controller);

  static LayoutNodeResult DispatchProjectedLayout(
      MacScenePlatformController *controller,
      loka::app::scene::Node *node,
      const LayoutState &state);

  static int layoutContainerChild(void *context, loka::app::scene::Node *child, const LayoutState &state);
  int layoutNodeFromSceneState(loka::app::scene::Node *node, const loka::app::scene::LayoutState &state);
  int layoutNode(loka::app::scene::Node *node, const LayoutState &state);
  LayoutNodeResult computeLayoutResult(loka::app::scene::Node *node, const LayoutState &state);
  LayoutNodeResult layoutScrollViewNode(loka::app::ScrollViewNode *scrollView,
                                        const LayoutState &state);
  int applyBoundaryLayoutResult(loka::app::scene::BoundaryNode *boundary, int x, int y, const LayoutNodeResult &result);
  LayoutNodeResult layoutRectSurfaceNode(loka::app::RectSurfaceNode *surface, const LayoutState &state);
  bool narrowLayoutState(const LayoutState &state,
                         loka::app::scene::LayoutState &narrowed,
                         bool applyProjection);
  bool refuseNarrowingInScrollScope(int resultY);
  void refuseScrollViewShortRange();
  void performLayout(int clientWidth, int clientHeight, bool rebuildContexts);
  void clearContexts();
  void clearNodeContexts(loka::app::scene::Node *node);
  int measureClientWidth(int requestedWidth) const;
  void registerEditField(void *field);
  void finalizeKeyLoop();
  void captureFocusedEditField();
  void restoreFocusedEditField();
  void *findFocusedEditTextState(loka::app::scene::Node *node) const;
  void *findFieldForFocusedEdit(loka::app::scene::Node *node) const;

  /** Finite admission-time font storage; no lazy update-cycle allocation. */
  class TextFontTable
  {
  public:
    explicit TextFontTable(const loka::app::Ratio &fontScale);
    ~TextFontTable();
    void *find(const loka::app::TextStyle &style, bool fixedPitch) const;

  private:
    // Sized from the generated vocabulary so a size or size role added through
    // tools/style-vocab.json cannot fall outside the table. Rows: one per
    // vocabulary size, one per size role, then the system default row.
    enum
    {
      kSizeCount = loka::app::detail::kStyleVocabularySizeCount,
      kRoleRowBase = kSizeCount,
      kDefaultSizeRow = kRoleRowBase + loka::app::TEXT_SIZE_ROLE_COUNT,
      kFontRowCount = kDefaultSizeRow + 1
    };
    void *fonts_[kFontRowCount][2][2][2];
    TextFontTable(const TextFontTable &);
    TextFontTable &operator=(const TextFontTable &);
  };

  const loka::macos::MacProjection projection_;
  const loka::app::layout::RowLayoutMetrics rowLayoutMetrics_;
  const loka::app::TextShaping textShaping_;
  TextFontTable textFonts_;
  PlatformContext *const platformContext_; // Ancestor outlives controller and captured Images.
  void *rootView_;
  loka::app::scene::ProjectionParentScopeStack projectionParentScopes_;
  loka::app::scene::PlatformLayoutHandlerRegistry layoutHandlerRegistry_;
  loka::app::scene::PlatformNodeHandlerRegistry nodeHandlerRegistry_;
  loka::app::scene::Node *rootNode_;
  loka::app::RectSurfaceExtentLedger rectSurfaceExtentLedger_;
  loka::app::scene::NodeDirtyFlags lastChangeFlags_;
  int clientWidth_;
  int clientHeight_;
  void *firstEditField_;
  void *lastEditField_;
  void *focusedEditTextState_;
  int focusedEditTextControlTag_;
  bool relayoutPending_;
  struct NativeRetirement
  {
    NativeRetirement(void *primaryObject, void *auxiliaryObject)
        : primary(primaryObject),
          auxiliary(auxiliaryObject)
    {
    }

    void *primary;
    void *auxiliary;
  };
  std::vector<NativeRetirement> nativeRetirements_;
};

#endif // LOKA_MAC_SCENE_PLATFORM_CONTROLLER_HPP
