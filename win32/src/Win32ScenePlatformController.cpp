#include "Win32PaintGround.hpp"
#include "Win32Window.hpp"
#include "Win32InputDoor.hpp"
#include "Win32ScenePlatformController.hpp"
#include "Win32BitmapCapture.hpp"
#include "platform/Win32String.hpp"
#include "context/Win32FocusParticipant.hpp"
#include "context/Win32EditTextBridge.hpp"
#include "app/layout/CanvasLayout.hpp"
#include "Win32BuiltInSupport.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/scene/boundary/Boundary.hpp"
#include "app/scene/projection/CollectPaintAnswers.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/nodes/ImageView.hpp"
#include <cassert>
#include <climits>
#include <windows.h>
#include <cstdio>
#include <map>
#include <vector>
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/RectSurface.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/layout/PlatformBuiltinLayoutHandlers.hpp"
#include "app/layout/BoxLayout.hpp"
#include "app/layout/ColumnLayout.hpp"
#include "app/layout/GridLayout.hpp"
#include "app/layout/RowLayout.hpp"
#include "app/layout/ZStackLayout.hpp"
#include "app/scene/Node.hpp"
#include "core/Profiler.hpp"
#include "context/Win32ButtonContext.hpp"
#include "context/Win32EditTextContext.hpp"
#include "context/Win32TextEditorContext.hpp"
#include "context/Win32CellContext.hpp"
#include "context/Win32PopupMenuContext.hpp"
#include "context/Win32ImageViewContext.hpp"
#include "context/Win32OpenFileDialogContext.hpp"
#include "context/Win32RectSurfaceContext.hpp"
#include "context/Win32ScrollViewContext.hpp"

namespace
{
  // Deliberate Toolbox twin: eight logical display units per title side.
  const int kPushButtonTitleInsetDips = 8;

  /** One installation contract for registration protection and paint lookup.
      Unknown handler contexts are never cast by the paint walk. */
  struct PaintContextKind
  {
    loka::app::scene::NodeKind kind;
    const void *typeKey;
  };
  const PaintContextKind kPaintContextKinds[] = {
      {loka::app::scene::NODE_KIND_ATTRIBUTED_TEXT, loka::app::scene::NodeTypeToken<loka::app::AttributedTextNode>()},
      {loka::app::scene::NODE_KIND_RECT_SURFACE, loka::app::scene::NodeTypeToken<loka::app::RectSurfaceNode>()},
      {loka::app::scene::NODE_KIND_TEXT, loka::app::scene::NodeTypeToken<loka::app::TextNode>()},
      {loka::app::scene::NODE_KIND_BUTTON, loka::app::scene::NodeTypeToken<loka::app::ButtonNode>()},
      {loka::app::scene::NODE_KIND_EDIT_TEXT, loka::app::scene::NodeTypeToken<loka::app::EditTextNode>()},
      {loka::app::scene::NODE_KIND_TEXT_EDITOR, loka::app::scene::NodeTypeToken<loka::app::TextEditorNode>()},
      {loka::app::scene::NODE_KIND_POPUP_MENU, loka::app::scene::NodeTypeToken<loka::app::PopupMenuNode>()},
      {loka::app::scene::NODE_KIND_CELL, loka::app::scene::NodeTypeToken<loka::app::CellNode>()},
      {loka::app::scene::NODE_KIND_IMAGE_VIEW, loka::app::scene::NodeTypeToken<loka::app::ImageViewNode>()},
      {loka::app::scene::NODE_KIND_SCROLL_VIEW, loka::app::scene::NodeTypeToken<loka::app::ScrollViewNode>()}};

  /** Read-only adapter: one query per attached native resident. The table is
      fixed rail metadata, not a walk through another owner's live rows. */
  struct Win32PaintAnswerSource
  {
    /** Exact damage may share a conservative, non-erasing destination only when true. */
    enum { kMergesExactDamage = 0 };
    bool queryPaintAnswer(loka::app::scene::Node *node,
                          loka::app::scene::NodeContext *context,
                          const loka::app::scene::PaintQuery &query,
                          loka::app::scene::PaintAnswer &answer)
    {
      using namespace loka::app::scene;
      for (size_t i = 0; i < sizeof(kPaintContextKinds) / sizeof(kPaintContextKinds[0]); ++i)
      {
        if (node->kind() == kPaintContextKinds[i].kind)
        {
          if (node->nodeTypeKey() != kPaintContextKinds[i].typeKey)
            answer = PaintAnswer::refused(PAINT_REFUSED_UNSUPPORTED_KIND);
          else
            answer = context ? static_cast<Win32RetirableContext *>(context)->queryPaintDamage(query)
                             : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
          return true;
        }
      }
      switch (node->kind())
      {
      case NODE_KIND_OPEN_FILE_DIALOG:
      case NODE_KIND_BOX:
      case NODE_KIND_ZSTACK:
      case NODE_KIND_GRID:
      case NODE_KIND_STACK:
      case NODE_KIND_CANVAS:
        return false;
      case NODE_KIND_ATTRIBUTED_TEXT:
        break;
      case NODE_KIND_UNKNOWN:
        if (!node->asProjectedLayoutNode())
          return false;
        break;
      case NODE_KIND_SCROLL_BAR:
      case NODE_KIND_RECT_SURFACE:
      case NODE_KIND_TEXT:
      case NODE_KIND_BUTTON:
      case NODE_KIND_EDIT_TEXT:
      case NODE_KIND_TEXT_EDITOR:
      case NODE_KIND_POPUP_MENU:
      case NODE_KIND_CELL:
      case NODE_KIND_IMAGE_VIEW:
      case NODE_KIND_SCROLL_VIEW:
        break;
      }
      answer = PaintAnswer::refused(context ? PAINT_REFUSED_UNSUPPORTED_KIND : PAINT_REFUSED_NO_CONTEXT);
      return true;
    }
  };

  typedef std::map<HWND, Win32ScenePlatformController *> Win32ControllerMap;
  Win32ControllerMap gControllersByRootHwnd;

  struct DisplayFontReplacement
  {
    const loka::win32::Win32DisplayFont &previous;
    const loka::win32::Win32DisplayFont &next;
  };

  BOOL CALLBACK ApplyDisplayFont(HWND hwnd, LPARAM fontValue)
  {
    const DisplayFontReplacement &fonts = *reinterpret_cast<const DisplayFontReplacement *>(fontValue);
    const HFONT previous = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(fonts.previous.replacementFor(previous, fonts.next)), TRUE);
    return TRUE;
  }

} // namespace

/** Stack owner for one native layout presentation. Contexts keep their usual
    relayout doors, while the controller borrows the outermost pass during the
    synchronous traversal. Reentrant layout scopes join that pass. The borrow
    is removed before RedrawWindow can re-enter native message handling. */
class Win32NativeLayoutPass
{
public:
  explicit Win32NativeLayoutPass(Win32ScenePlatformController *controller)
      : controller_(controller)
  {
    assert(this->controller_);
    if (!this->controller_->activeNativeLayoutPass_)
    {
      this->controller_->activeNativeLayoutPass_ = this;
    }
  }

  ~Win32NativeLayoutPass()
  {
    assert(this->controller_);
    if (this->controller_->activeNativeLayoutPass_ != this)
    {
      assert(this->controller_->activeNativeLayoutPass_
             && "a nested Win32 native layout pass must share an outer owner");
      return;
    }
    this->controller_->activeNativeLayoutPass_ = 0;
    this->controller_->rectSurfaceExtentLedger_.flush();
    if (this->controller_->rootHwnd_)
    {
      RedrawWindow(this->controller_->rootHwnd_,
                   NULL,
                   NULL,
                   RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
  }

  void positionNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
  {
    const RECT &nativeRect = geometry.r;
    MoveWindow(hwnd,
               nativeRect.left,
               nativeRect.top,
               nativeRect.right - nativeRect.left,
               nativeRect.bottom - nativeRect.top,
               FALSE);
  }

private:
  Win32ScenePlatformController *controller_;

  Win32NativeLayoutPass(const Win32NativeLayoutPass &);
  Win32NativeLayoutPass &operator=(const Win32NativeLayoutPass &);
};

namespace loka
{
  namespace app
  {
    namespace scene
    {
      class Win32PlatformLayoutTraversal : public IPlatformLayoutTraversal
      {
      public:
        explicit Win32PlatformLayoutTraversal(Win32ScenePlatformController *controller)
            : controller_(controller),
              layoutResultY_(0)
        {
        }

        virtual bool queryNaturalWidth(Node *child, short &width) const
        {
          return this->controller_ && this->controller_->queryNaturalWidth(child, width);
        }

        virtual int layoutChild(Node *child, const LayoutState &state)
        {
          if (!this->controller_)
          {
            return state.y;
          }
          const int result = this->controller_->layoutNodeFromSceneState(child, state);
          if (this->controller_->refuseNarrowingInScrollScope(result))
          {
            return state.y;
          }
          this->layoutResultY_ = static_cast<short>(result);
          return result;
        }

        virtual void setLayoutResultY(short y)
        {
          this->layoutResultY_ = y;
        }

        virtual short layoutResultY() const
        {
          return this->layoutResultY_;
        }

      private:
        Win32ScenePlatformController *controller_;
        short layoutResultY_;
      };
    } // namespace scene
  } // namespace app
} // namespace loka

bool Win32ScenePlatformController::measurePushButtonNaturalWidth(
    const loka::core::String &label, short &width) const
{
  const HFONT font = this->displayFont_.get();
  if (!font)
    return false;
  std::wstring wide;
  if (!loka::win32::MaterializeWideString(label, wide)
      || wide.size() > static_cast<std::size_t>(INT_MAX))
    return false;
  HDC dc = GetDC(this->rootHwnd_);
  if (!dc)
    return false;
  HGDIOBJ previousFont = SelectObject(dc, font);
  SIZE extent = {0, 0};
  const bool selected = previousFont && previousFont != HGDI_ERROR;
  const BOOL measured = selected
      ? GetTextExtentPoint32W(dc, wide.c_str(), static_cast<int>(wide.size()), &extent) : FALSE;
  if (selected)
    SelectObject(dc, previousFont);
  ReleaseDC(this->rootHwnd_, dc);
  if (!measured || extent.cx < 0)
    return false;
  const int inset = this->displayScale_.nativeLength(0, kPushButtonTitleInsetDips).px;
  if (inset < 0 || inset > (INT_MAX - extent.cx) / 2)
    return false;
  // GDI reports pixels; Row seats are logical, and HWND relayout projects edges.
  const int logicalWidth = this->displayScale_.measurementToLu(static_cast<int>(extent.cx) + 2 * inset);
  if (logicalWidth <= 0 || logicalWidth > SHRT_MAX)
    return false;
  width = static_cast<short>(logicalWidth);
  return true;
}

bool Win32ScenePlatformController::queryNaturalWidth(loka::app::scene::Node *child, short &width) const
{
  loka::app::ButtonNode *button = child ? child->asButtonNode() : 0;
  // Win32ButtonContext leaves its initial empty title alone without a text State.
  if (!button || !button->props.text_)
    return false;
  return this->measurePushButtonNaturalWidth(button->props.text_->get(), width);
}

/** Measured 2026-09-19 (#818): the 9 pt message font is 12 px at 96 dpi,
    approximately 12 lu. The 75x23 px standard button and native margins
    motivate the candidate space ratio. Fixed controls keep their native font.
    These are candidate values for visual review, not golden-baked values. */
loka::app::RailMetrics loka::win32::DefaultRailMetrics()
{
  return app::RailMetrics(app::Ratio(1, 1), app::Ratio(5, 4));
}

Win32ScenePlatformController::Win32ScenePlatformController(
    HWND rootHwnd,
    const loka::win32::Win32DisplayScale &displayScale,
    PlatformContext *platformContext)
    : platformContext_(platformContext),
      rootHwnd_(rootHwnd),
      activeNativeLayoutPass_(0),
      rectSurfaceExtentLedger_(),
      projectionParentScopes_(rootHwnd),
      rootNode_(0),
      clientWidth_(0),
      clientHeight_(0),
      railMetrics_(displayScale.railMetrics()),
      textShaping_(loka::app::PER_RUN),
      displayScale_(displayScale.dpi(), this->railMetrics_),
      displayFont_()
{
  RegisterWin32BuiltInSupport(*this);
  this->displayFont_.create(this->displayScale_);
  if (rootHwnd_)
  {
    gControllersByRootHwnd[rootHwnd_] = this;
  }
}

bool Win32ScenePlatformController::captureWindowClientBitmap(HWND hwnd, loka::core::resource::Image &out) const
{
  return loka::win32::CaptureWindowClientBitmap(this->platformContext_, hwnd, out);
}

Win32ScenePlatformController::~Win32ScenePlatformController()
{
  assert(!this->activeNativeLayoutPass_
         && "a Win32 controller must outlive its stack-owned native layout pass");
  if (rootHwnd_)
  {
    Win32ControllerMap::iterator it = gControllersByRootHwnd.find(rootHwnd_);
    if (it != gControllersByRootHwnd.end() && it->second == this)
    {
      gControllersByRootHwnd.erase(it);
    }
  }
  clearContexts();
  this->drainNativeRetirements();
}

void Win32ScenePlatformController::requestDirtyRect(HWND targetHwnd, const RECT *rect, BOOL eraseBackground)
{
  if (!targetHwnd)
  {
    return;
  }
  HWND root = GetAncestor(targetHwnd, GA_ROOT);
  if (!root)
  {
    root = targetHwnd;
  }
  Win32ControllerMap::iterator it = gControllersByRootHwnd.find(root);
  if (it == gControllersByRootHwnd.end() || !it->second)
  {
    InvalidateRect(targetHwnd, rect, eraseBackground);
    return;
  }
  if (it->second->activeNativeLayoutPass_)
  {
    return;
  }
  const RECT empty = {0, 0, 0, 0};
  const loka::win32::NativeRect native = loka::win32::Win32DisplayScale::fromDevicePixels(rect ? *rect : empty);
  it->second->queueDirtyRect(targetHwnd, rect ? &native : 0, eraseBackground, false);
}

bool Win32ScenePlatformController::registerNodeHandler(loka::app::scene::IPlatformNodeHandler *handler)
{
  if (!handler)
    return false;
  // Built-ins register privately through RegisterWin32BuiltInSupport. Like
  // Null's owned drawers, these contexts cannot be replaced by a foreign type:
  // the paint adapter and EXACT translator rely on this always-on wall.
  for (size_t i = 0; i < sizeof(kPaintContextKinds) / sizeof(kPaintContextKinds[0]); ++i)
    if (handler->nodeTypeKey() == kPaintContextKinds[i].typeKey)
      return false;
  return this->nodeHandlerRegistry_.registerHandler(handler);
}

bool Win32ScenePlatformController::prepareProjectedLayout(loka::app::scene::Node *node,
                                                          loka::app::scene::LayoutState &state)
{
  if (!node)
  {
    return false;
  }
  loka::app::scene::LayoutState handlerState = state;
  if (handlerState.height <= 0)
  {
    handlerState.height = static_cast<short>(loka::app::layout::FallbackControlMetrics::kTextHeight);
  }
  loka::app::scene::IPlatformNodeHandler *handler = this->nodeHandlerRegistry_.find(node);
  if (!handler)
  {
    assert(
        false
        && "no node handler registered for this node type -- register the handler or an explicit RefusedNodeHandler");
    return false;
  }
  loka::app::scene::NodeContext *context = handler->ensureContext(node, this, handlerState);
  if (!context)
  {
    return false;
  }
  return true;
}

int Win32ScenePlatformController::layoutNodeFromSceneState(loka::app::scene::Node *node,
                                                           const loka::app::scene::LayoutState &state)
{
  LayoutState localState;
  localState.x = state.x;
  localState.y = state.y;
  localState.width = state.width;
  localState.height = state.height;
  return this->layoutNode(node, localState);
}

void Win32ScenePlatformController::requestDirtySubtree(HWND targetHwnd, const RECT *rect, BOOL eraseBackground)
{
  if (!targetHwnd)
  {
    return;
  }
  HWND root = GetAncestor(targetHwnd, GA_ROOT);
  if (!root)
  {
    root = targetHwnd;
  }
  Win32ControllerMap::iterator it = gControllersByRootHwnd.find(root);
  if (it == gControllersByRootHwnd.end() || !it->second)
  {
    UINT flags = RDW_INVALIDATE | (eraseBackground ? RDW_ERASE : 0) | RDW_ALLCHILDREN;
    RedrawWindow(targetHwnd, rect, NULL, flags);
    return;
  }
  if (it->second->activeNativeLayoutPass_)
  {
    return;
  }
  const RECT empty = {0, 0, 0, 0};
  const loka::win32::NativeRect native = loka::win32::Win32DisplayScale::fromDevicePixels(rect ? *rect : empty);
  it->second->queueDirtyRect(targetHwnd, rect ? &native : 0, eraseBackground, true);
}

bool Win32ScenePlatformController::requestTransparentChildRepaint(HWND child)
{
  HWND parent = child ? GetParent(child) : 0;
  RECT rect;
  if (!parent || !GetWindowRect(child, &rect))
    return false;
  MapWindowPoints(NULL, parent, reinterpret_cast<POINT *>(&rect), 2);
  requestDirtySubtree(parent, &rect, FALSE);
  return true;
}

void Win32ScenePlatformController::paintWindowGround(HDC dc, const RECT &rect)
{
  loka::win32::Win32PaintGround(dc, loka::app::SURFACE_GROUND_WINDOW, rect);
}

void Win32ScenePlatformController::redrawDirtySubtreeNow(HWND targetHwnd, const RECT *rect, BOOL eraseBackground)
{
  if (!targetHwnd)
  {
    return;
  }
  HWND root = GetAncestor(targetHwnd, GA_ROOT);
  if (!root)
  {
    root = targetHwnd;
  }
  Win32ControllerMap::iterator it = gControllersByRootHwnd.find(root);
  if (it != gControllersByRootHwnd.end() && it->second
      && it->second->activeNativeLayoutPass_)
  {
    return;
  }
  UINT flags = RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW;
  if (eraseBackground)
  {
    flags |= RDW_ERASE;
  }
  RedrawWindow(targetHwnd, rect, NULL, flags);
}

void Win32ScenePlatformController::noteNativePaint(HWND targetHwnd, NativePaintKind kind, bool eraseBackground)
{
  if (!targetHwnd)
  {
    return;
  }
  HWND root = GetAncestor(targetHwnd, GA_ROOT);
  if (!root)
  {
    root = targetHwnd;
  }
  Win32ControllerMap::iterator it = gControllersByRootHwnd.find(root);
  if (it == gControllersByRootHwnd.end() || !it->second)
  {
    return;
  }
  RedrawStats &stats = it->second->redrawStats_;
  switch (kind)
  {
  case NATIVE_PAINT_ROOT:
    if (eraseBackground)
    {
      ++stats.rootEraseCount;
    }
    else
    {
      ++stats.rootPaintCount;
    }
    break;
  case NATIVE_PAINT_CELL:
    if (eraseBackground)
    {
      ++stats.cellEraseCount;
    }
    else
    {
      ++stats.cellPaintCount;
    }
    break;
  case NATIVE_PAINT_IMAGE:
    if (eraseBackground)
    {
      ++stats.imageEraseCount;
    }
    else
    {
      ++stats.imagePaintCount;
    }
    break;
  case NATIVE_PAINT_RECT_SURFACE:
    if (eraseBackground)
    {
      ++stats.rectSurfaceEraseCount;
    }
    else
    {
      ++stats.rectSurfacePaintCount;
    }
    break;
  default:
    break;
  }
}

void Win32ScenePlatformController::onChange(loka::app::scene::Node *rootNode,
                                            loka::app::scene::NodeDirtyFlags flags,
                                            bool fullRebuild)
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  (void)fullRebuild;
  ++this->redrawStats_.onChangeCalls;
  this->redrawStats_.lastOnChangeFlags = flags;
  this->redrawStats_.lastOnChangeFullRebuild = fullRebuild;
  rootNode_ = rootNode;
  if (!rootHwnd_ || !rootNode_)
  {
    this->redrawStats_.lastOnChangeRequiredLayout = false;
    return;
  }

  const bool requiresLayout = (flags & loka::app::scene::NODE_DIRTY_INITIAL) != 0
                              || (flags & loka::app::scene::NODE_DIRTY_LAYOUT) != 0
                              || (flags & loka::app::scene::NODE_DIRTY_CHILD) != 0;
  this->redrawStats_.lastOnChangeRequiredLayout = requiresLayout;
  if (!requiresLayout)
  {
    return;
  }

  RECT rc;
  if (GetClientRect(rootHwnd_, &rc))
  {
    clientWidth_ = this->displayScale_.clientCapacityToLu(rc.right - rc.left);
    clientHeight_ = this->displayScale_.clientCapacityToLu(rc.bottom - rc.top);
  }
  performLayout(clientWidth_, clientHeight_);
}

void Win32ScenePlatformController::onBoundaryApply(loka::app::scene::Node *rootNode,
                                                   loka::app::scene::BoundaryNode *boundary,
                                                   const loka::app::scene::BoundaryLocalApplyInfo &info,
                                                   const loka::app::scene::PlatformApplyPlan &plan)
{
  ++this->redrawStats_.onBoundaryApplyCalls;
  if (rootNode)
  {
    rootNode_ = rootNode;
  }
  if (!rootHwnd_ || !rootNode_ || !boundary || !plan.hasBoundaryApplyWork(boundary))
  {
    return;
  }
  if (info.hasStructureWork || info.hasLayoutWork || !info.hasPaintWork())
  {
    return;
  }

  using namespace loka::app::scene;
  const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
  PaintAnswerBuffer<> answers;
  Win32PaintAnswerSource source;
  const PaintApplyVerdict verdict = CollectPaintAnswers(*boundary, query, answers, source);
  if (verdict.canSkipBroadPaint(info))
  {
    // Borrowed residents end here. The existing native queue stores HWNDs and
    // device-pixel rectangles only; it coalesces with observer-owned requests.
    for (unsigned i = 0; i < answers.count(); ++i)
    {
      const PaintAnswerRecord &record = answers.entry(i);
      Win32RetirableContext *context = static_cast<Win32RetirableContext *>(record.resident->getContext());
      const PaintDamage &damage = record.damage;
      const RECT pixels = {damage.x, damage.y, damage.x + damage.width, damage.y + damage.height};
      const loka::win32::NativeRect rect = this->displayScale_.fromDevicePixels(pixels);
      this->queueDirtyRect(
          context->paintHwnd(), &rect, damage.coverage == PAINT_COVERAGE_ERASE_AND_PAINT ? TRUE : FALSE, false);
    }
    return;
  }

  const bool eraseBackground = !info.paintIsOpaque;
  // #727: the root WM_PAINT fills its ground even without WM_ERASEBKGND.
  // Every broad fallback must restore child pixels, including under viewports.
  const bool includeChildren = true;
  if (info.hasCompositedPaintWork())
  {
    ++this->redrawStats_.queuedCompositedInvalidates;
  }
  else if (info.hasOpaquePaintWork())
  {
    ++this->redrawStats_.queuedOpaquePaintInvalidates;
  }
  else if (info.hasPaintWork())
  {
    ++this->redrawStats_.queuedGenericPaintInvalidates;
  }
  if (!info.hasBoundsHint())
  {
    ++this->redrawStats_.queuedFullWindowInvalidates;
    ++this->redrawStats_.queuedMissingBoundsInvalidates;
    queueDirtyRect(rootHwnd_, 0, eraseBackground ? TRUE : FALSE, includeChildren);
    return;
  }

  ++this->redrawStats_.queuedRectInvalidates;
  if (info.hasPaintBoundsHint())
  {
    ++this->redrawStats_.queuedPaintBoundsInvalidates;
  }
  else if (info.hasLayoutBoundsHint())
  {
    ++this->redrawStats_.queuedLayoutBoundsInvalidates;
  }
  const loka::win32::NativeRect rect = this->displayScale_.damageToNative(
      loka::core::Frame(info.bounds->x, info.bounds->y, info.bounds->width, info.bounds->height));
  queueDirtyRect(rootHwnd_, &rect, eraseBackground ? TRUE : FALSE, includeChildren);
}

void Win32ScenePlatformController::beginApplyCycle()
{
  this->redrawStats_.reset();
}

UINT Win32ScenePlatformController::pendingInvalidationFlags(const PendingInvalidate &entry)
{
  UINT flags = RDW_INVALIDATE;
  if (entry.eraseBackground)
  {
    flags |= RDW_ERASE;
  }
  if (entry.includeChildren)
  {
    flags |= RDW_ALLCHILDREN;
  }
  return flags;
}

void Win32ScenePlatformController::flushPendingInvalidations(bool updateNow)
{
  for (size_t i = 0; i < this->pendingInvalidations_.size(); ++i)
  {
    const PendingInvalidate &entry = this->pendingInvalidations_[i];
    if (!IsWindow(entry.hwnd))
    {
      continue;
    }
    RedrawWindow(entry.hwnd, entry.fullWindow ? NULL : &entry.rect, NULL,
                 pendingInvalidationFlags(entry));
    if (!(GetWindowLongPtrW(entry.hwnd, GWL_STYLE) & WS_CHILD))
    {
      continue;
    }
    RECT damage;
    if (!GetClientRect(entry.hwnd, &damage))
    {
      continue;
    }
    if (!entry.fullWindow && !IntersectRect(&damage, &damage, &entry.rect))
    {
      continue;
    }
    MapWindowPoints(entry.hwnd, NULL, reinterpret_cast<POINT *>(&damage), 2);
    // A transparent sibling needs the freshly painted ground as well as its
    // own foreground. Clipping the source by the sibling's full bounds would
    // preserve stale ground instead. Queue all damage before any paint runs.
    for (HWND sibling = GetWindow(entry.hwnd, GW_HWNDNEXT); sibling;
         sibling = GetWindow(sibling, GW_HWNDNEXT))
    {
      RECT bounds;
      RECT overlap;
      if (GetWindowRect(sibling, &bounds) && IntersectRect(&overlap, &damage, &bounds))
      {
        MapWindowPoints(NULL, sibling, reinterpret_cast<POINT *>(&overlap), 2);
        RedrawWindow(sibling, &overlap, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
      }
    }
  }
  this->pendingInvalidations_.clear();
  if (updateNow)
  {
    RedrawWindow(this->rootHwnd_, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN);
  }
}

void Win32ScenePlatformController::synchronize()
{
  this->dumpRedrawStatsIfNeeded();
  this->flushPendingInvalidations(true);
  this->redrawStats_.reset();
}

bool Win32ScenePlatformController::hasPendingSync() const
{
  return !pendingInvalidations_.empty() || !retiredWindows_.empty();
}

void Win32ScenePlatformController::queueNativeRetirement(HWND hwnd)
{
  if (hwnd)
  {
    this->retiredWindows_.push_back(hwnd);
  }
}

void Win32ScenePlatformController::drainNativeRetirements()
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  for (size_t i = 0; i < this->retiredWindows_.size(); ++i)
  {
    if (this->retiredWindows_[i])
    {
      DestroyWindow(this->retiredWindows_[i]);
    }
  }
  this->retiredWindows_.clear();
}

void Win32ScenePlatformController::destroy()
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  pendingInvalidations_.clear();
  clearContexts();
  this->drainNativeRetirements();
  rootNode_ = 0;
  clientWidth_ = 0;
  clientHeight_ = 0;
}

void Win32ScenePlatformController::releaseNodeContexts(loka::app::scene::Node *node)
{
#ifdef LOKA_LIFECYCLE_AUDIT
    assert(!this->borrowPhase().open());
#endif
  clearNodeContexts(node);
}

void Win32ScenePlatformController::queueDirtyRect(HWND targetHwnd,
                                                  const loka::win32::NativeRect *geometry,
                                                  BOOL eraseBackground,
                                                  bool includeChildren)
{
  const RECT *rect = geometry ? &geometry->r : 0;
  if (!targetHwnd)
  {
    return;
  }
  for (size_t i = 0; i < pendingInvalidations_.size(); ++i)
  {
    PendingInvalidate &entry = pendingInvalidations_[i];
    if (entry.hwnd != targetHwnd)
    {
      continue;
    }
    entry.eraseBackground = (entry.eraseBackground || eraseBackground) ? TRUE : FALSE;
    entry.includeChildren = entry.includeChildren || includeChildren;
    if (!rect)
    {
      entry.fullWindow = true;
      return;
    }
    if (entry.fullWindow)
    {
      return;
    }
    if (rect->left < entry.rect.left)
    {
      entry.rect.left = rect->left;
    }
    if (rect->top < entry.rect.top)
    {
      entry.rect.top = rect->top;
    }
    if (rect->right > entry.rect.right)
    {
      entry.rect.right = rect->right;
    }
    if (rect->bottom > entry.rect.bottom)
    {
      entry.rect.bottom = rect->bottom;
    }
    return;
  }

  PendingInvalidate entry;
  entry.hwnd = targetHwnd;
  entry.eraseBackground = eraseBackground;
  entry.includeChildren = includeChildren;
  if (!rect)
  {
    entry.fullWindow = true;
  }
  else
  {
    entry.rect = *rect;
  }
  pendingInvalidations_.push_back(entry);
}

void Win32ScenePlatformController::dumpRedrawStatsIfNeeded()
{
#if defined(LOKA_DEBUG_RECOMPOSE) && !defined(LOKA_RETRO68)
  if (redrawStats_.onChangeCalls == 0 && redrawStats_.onBoundaryApplyCalls == 0
      && redrawStats_.queuedFullWindowInvalidates == 0 && redrawStats_.queuedRectInvalidates == 0
      && redrawStats_.rootEraseCount == 0 && redrawStats_.rootPaintCount == 0 && redrawStats_.cellEraseCount == 0
      && redrawStats_.cellPaintCount == 0 && redrawStats_.imageEraseCount == 0 && redrawStats_.imagePaintCount == 0
      && redrawStats_.rectSurfaceEraseCount == 0 && redrawStats_.rectSurfacePaintCount == 0)
  {
    return;
  }

  char buffer[512];
  ::snprintf(buffer,
             sizeof(buffer),
             "[win32-redraw] onChange=%d localApply=%d changeFlags=0x%X changeNeedsLayout=%d changeFullRebuild=%d "
             "full=%d rect=%d layoutBounds=%d paintBounds=%d noBounds=%d comp=%d opaque=%d generic=%d root(e=%d p=%d) "
             "cell(e=%d p=%d) image(e=%d p=%d) rect(e=%d p=%d)\n",
             redrawStats_.onChangeCalls,
             redrawStats_.onBoundaryApplyCalls,
             static_cast<unsigned int>(redrawStats_.lastOnChangeFlags),
             redrawStats_.lastOnChangeRequiredLayout ? 1 : 0,
             redrawStats_.lastOnChangeFullRebuild ? 1 : 0,
             redrawStats_.queuedFullWindowInvalidates,
             redrawStats_.queuedRectInvalidates,
             redrawStats_.queuedLayoutBoundsInvalidates,
             redrawStats_.queuedPaintBoundsInvalidates,
             redrawStats_.queuedMissingBoundsInvalidates,
             redrawStats_.queuedCompositedInvalidates,
             redrawStats_.queuedOpaquePaintInvalidates,
             redrawStats_.queuedGenericPaintInvalidates,
             redrawStats_.rootEraseCount,
             redrawStats_.rootPaintCount,
             redrawStats_.cellEraseCount,
             redrawStats_.cellPaintCount,
             redrawStats_.imageEraseCount,
             redrawStats_.imagePaintCount,
             redrawStats_.rectSurfaceEraseCount,
             redrawStats_.rectSurfacePaintCount);
  OutputDebugStringA(buffer);
#endif
}

bool Win32ScenePlatformController::handleCommand(WPARAM wParam, LPARAM lParam)
{
  HWND target = reinterpret_cast<HWND>(lParam);
  WORD code = HIWORD(wParam);
  if (code == BN_CLICKED)
  {
    Win32ButtonContext *button = reinterpret_cast<Win32ButtonContext *>(GetWindowLongPtr(target, GWLP_USERDATA));
    if (!button)
    {
      return false;
    }
    return Win32InputDoor::buttonCommand(*button, wParam, lParam);
  }
  if (code == EN_CHANGE)
  {
    Win32TextEditorContext *editor = Win32TextEditorContext::fromWindow(target);
    if (editor)
      return Win32InputDoor::textEditorCommand(*editor, wParam, lParam);
    Win32EditTextContext *edit = reinterpret_cast<Win32EditTextContext *>(GetWindowLongPtr(target, GWLP_USERDATA));
    if (!edit)
    {
      return false;
    }
    return Win32InputDoor::editTextCommand(*edit, wParam, lParam);
  }
  if (code == CBN_SELCHANGE)
  {
    Win32PopupMenuContext *popup = reinterpret_cast<Win32PopupMenuContext *>(GetWindowLongPtr(target, GWLP_USERDATA));
    if (!popup)
    {
      return false;
    }
    return Win32InputDoor::popupMenuCommand(*popup, wParam, lParam);
  }
  return false;
}

void Win32ScenePlatformController::relayout(int clientWidth, int clientHeight)
{
  if (!rootNode_)
  {
    return;
  }
  if (clientWidth <= 0 || clientHeight <= 0)
  {
    RECT rc;
    if (rootHwnd_ && GetClientRect(rootHwnd_, &rc))
    {
      clientWidth = this->displayScale_.clientCapacityToLu(rc.right - rc.left);
      clientHeight = this->displayScale_.clientCapacityToLu(rc.bottom - rc.top);
    }
  }
  clientWidth_ = clientWidth;
  clientHeight_ = clientHeight;
  performLayout(clientWidth_, clientHeight_);
}

void Win32ScenePlatformController::relayoutNativeClientPixels(int clientWidth,
                                                              int clientHeight)
{
  this->relayout(this->displayScale_.clientCapacityToLu(clientWidth),
                 this->displayScale_.clientCapacityToLu(clientHeight));
}

void Win32ScenePlatformController::requestRelayout()
{
  // Posting WM_SIZE here re-enters the native message drain on every refusal.
  this->requestSceneRelayout(this->rootNode_);
  // Wake an idle-none loop without doing layout inside its message drain.
  if (this->rootHwnd_)
    PostMessageW(this->rootHwnd_, WM_NULL, 0, 0);
}

void Win32ScenePlatformController::updateDisplayScale(const loka::win32::Win32DisplayScale &displayScale)
{
  this->displayScale_ = loka::win32::Win32DisplayScale(displayScale.dpi(), this->railMetrics_);
  this->textEnvironment_.changed();
  this->ensureDisplayFont();
}

void Win32ScenePlatformController::ensureDisplayFont()
{
  if (this->displayFont_.matches(this->displayScale_))
  {
    return;
  }
  loka::win32::Win32DisplayFont replacement;
  if (replacement.create(this->displayScale_))
  {
    this->textEnvironment_.changed();
    this->applyDisplayFontToNativeSubtree(replacement);
    this->displayFont_.swap(replacement);
  }
}

void Win32ScenePlatformController::applyDisplayFontToNativeSubtree(const loka::win32::Win32DisplayFont &replacement)
{
  if (!this->rootHwnd_ || !replacement.get())
  {
    return;
  }
  const DisplayFontReplacement fonts = {this->displayFont_, replacement};
  EnumChildWindows(this->rootHwnd_, &ApplyDisplayFont, reinterpret_cast<LPARAM>(&fonts));
}

void Win32ScenePlatformController::positionNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
{
  if (!hwnd)
  {
    return;
  }
  if (this->activeNativeLayoutPass_)
  {
    this->activeNativeLayoutPass_->positionNativeWindow(hwnd, geometry);
    return;
  }
  const RECT &nativeRect = geometry.r;
  MoveWindow(hwnd,
             nativeRect.left,
             nativeRect.top,
             nativeRect.right - nativeRect.left,
             nativeRect.bottom - nativeRect.top,
             TRUE);
}

void Win32ScenePlatformController::resizeNativeWindow(HWND hwnd, const loka::win32::NativeRect &geometry)
{
  SetWindowPos(hwnd,
               0,
               0,
               0,
               geometry.r.right - geometry.r.left,
               geometry.r.bottom - geometry.r.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

HWND Win32ScenePlatformController::createNativeChildWindow(DWORD exStyle,
                                                           LPCWSTR className,
                                                           LPCWSTR windowName,
                                                           DWORD style,
                                                           const loka::win32::NativeRect &geometry,
                                                           HWND parent,
                                                           HMENU menu,
                                                           HINSTANCE instance,
                                                           void *createParameter)
{
  const RECT &nativeRect = geometry.r;
  HWND hwnd = CreateWindowExW(exStyle,
                              className,
                              windowName,
                              style,
                              nativeRect.left,
                              nativeRect.top,
                              nativeRect.right - nativeRect.left,
                              nativeRect.bottom - nativeRect.top,
                              parent,
                              menu,
                              instance,
                              createParameter);
  if (hwnd && this->displayFont_.get())
  {
    SendMessageW(hwnd,
                 WM_SETFONT,
                 reinterpret_cast<WPARAM>(this->displayFont_.get()),
                 FALSE);
  }
  return hwnd;
}

void Win32ScenePlatformController::performLayout(int clientWidth, int clientHeight)
{
  // A failed DPI-derived font allocation leaves the owned font and its scale
  // paired, so every later layout remains a safe retry point.
  this->ensureDisplayFont();
  pendingInvalidations_.clear();
  if (!rootNode_ || !rootHwnd_)
  {
    return;
  }
  LayoutState state;
  state.x = 20;
  state.y = 20;
  state.width = measureClientWidth(clientWidth) - 40;
  if (state.width < 0)
  {
    state.width = 0;
  }
  state.height = clientHeight > 0 ? clientHeight - 40 : 0;
  assert(this->projectionParentScopes_.activeDepth() == 0 &&
         "a Win32 projection pass must begin at the root scope");
  const loka::core::Frame rootClip(0, 0, clientWidth, clientHeight);
  if (!this->projectionParentScopes_.resetRoot(this->rootHwnd_, rootClip))
  {
    return;
  }
  {
    Win32NativeLayoutPass nativeLayoutPass(this);
    PROFILE_SECTION("layout");
    this->layoutNode(this->rootNode_, state);
    assert(this->projectionParentScopes_.activeDepth() == 0 &&
           "a Win32 projection pass must restore the root scope");
  }
}

namespace
{
}

int Win32ScenePlatformController::applyBoundaryLayoutResult(loka::app::scene::BoundaryNode *boundary,
                                                            int x,
                                                            int y,
                                                            const LayoutNodeResult &result)
{
  if (boundary)
  {
    boundary->setLayoutBounds(x, y, result.boundaryWidth, result.resultY - y);
  }
  return result.resultY;
}

Win32ScenePlatformController::LayoutNodeResult
Win32ScenePlatformController::layoutRectSurfaceNode(loka::app::RectSurfaceNode *surface, const LayoutState &state)
{
  loka::app::scene::LayoutState projectedState;
  if (!this->narrowLayoutState(state, projectedState, true))
  {
    return LayoutNodeResult(state.width, state.y);
  }
  const int width = surface->props.width_ > 0 ? surface->props.width_ : state.width;
  const int height = surface->props.height_ > 0 ? surface->props.height_ : state.height;
  Win32RectSurfaceContext *ctx = static_cast<Win32RectSurfaceContext *>(surface->getContext());
  if (ctx)
  {
    ctx->relayout(projectedState.x,
                  projectedState.y,
                  width,
                  height);
  }
  else
  {
    ctx = new Win32RectSurfaceContext(
        this,
        this->projectionParentHwnd(),
        projectedState.x,
        projectedState.y,
        width,
        height,
        surface);
    if (!ctx)
    {
      return LayoutNodeResult(state.width, state.y);
    }
    if (!ctx->hasNativeSurface())
    {
      // No native surface was placed: nothing is installed and nothing is
      // published; the next layout pass retries creation.
      delete ctx;
      return LayoutNodeResult(state.width, state.y);
    }
    surface->setContext(ctx);
    ctx->readLifecycleFactOnAttach();
  }
  // The seat is a fact only once the surface was actually placed: a refused
  // projection or a failed native context records nothing.
  this->rectSurfaceExtentLedger_.record(
      surface, loka::core::Frame(state.x, state.y, width, height));
  return LayoutNodeResult(
      state.width, state.y + height + loka::app::layout::FallbackControlMetrics::kVerticalSpacing);
}

Win32ScenePlatformController::LayoutNodeResult
Win32ScenePlatformController::layoutScrollViewNode(loka::app::ScrollViewNode *scrollView,
                                                   const LayoutState &state)
{
  if (!scrollView || this->projectionParentScopes_.activeDepth() != 0)
  {
    // V1 admits one active viewport. Refuse before creating the inner HWND so
    // the outer scope remains the only structural clipping parent.
    return LayoutNodeResult(state.width, state.y);
  }
  if (state.x < SHRT_MIN || state.x > SHRT_MAX ||
      state.y < SHRT_MIN || state.y > SHRT_MAX ||
      state.width < 0 || state.width > SHRT_MAX ||
      state.height < 0 || state.height > SHRT_MAX ||
      (state.height > 0 && state.y > SHRT_MAX - state.height))
  {
    // The shared traversal above consumes a short result. Refuse the viewport
    // itself before its bottom or any child coordinate would narrow.
    return LayoutNodeResult(state.width, state.y);
  }

  Win32ScrollViewContext *ctx =
      static_cast<Win32ScrollViewContext *>(scrollView->getContext());
  if (ctx)
  {
    ctx->relayout(state.x, state.y, state.width, state.height);
  }
  else
  {
    ctx = new Win32ScrollViewContext(this,
                                     this->projectionParentHwnd(),
                                     state.x,
                                     state.y,
                                     state.width,
                                     state.height,
                                     scrollView);
    if (!ctx || !ctx->isValid())
    {
      delete ctx;
      return LayoutNodeResult(state.width, state.y);
    }
    scrollView->setContext(ctx);
    ctx->readLifecycleFactOnAttach();
  }

  int requestedOffset = scrollView->props.offset_.isValid()
                            ? scrollView->props.offset_.state()->get()
                            : 0;
  if (requestedOffset < 0)
  {
    requestedOffset = 0;
  }
  else if (requestedOffset > SHRT_MAX)
  {
    // A fact far beyond the projected range must not push every child out of
    // range and wedge the layout in a refusal loop before setScrollMetrics
    // (the only precise clamp) can run; the exact maximum is republished
    // after measurement.
    requestedOffset = SHRT_MAX;
  }
  const loka::core::Frame viewportClip(0, 0, state.width, state.height);
  loka::app::scene::ProjectionParentScope viewportScope;
  const loka::app::scene::ProjectionParentScope &parentScope =
      this->projectionParentScopes_.current();
  if (!parentScope.deriveScrolled(
          static_cast<void *>(ctx->hwnd()),
          state.x,
          state.y,
          viewportClip,
          viewportScope))
  {
    return LayoutNodeResult(state.width, state.y);
  }
  loka::app::scene::ProjectionParentScope childScope;
  if (!viewportScope.deriveScrolled(viewportScope.nativeParent,
                                    0,
                                    requestedOffset,
                                    viewportClip,
                                    childScope))
  {
    return LayoutNodeResult(state.width, state.y);
  }

  LayoutState childBase = state;
  {
    RECT viewportClient;
    if (GetClientRect(ctx->hwnd(), &viewportClient))
    {
      // The visible scrollbar occupies non-client width, so the viewport's
      // client width is smaller than the seat width; children lay out
      // against the client, or they extend underneath the scrollbar.
      childBase.width = this->displayScale_.clientCapacityToLu(viewportClient.right - viewportClient.left);
    }
  }

  int currentY = state.y;
  int contentHeight = 0;
  bool shortRangeRefused = false;
  {
    loka::app::scene::ProjectionParentScopeGuard scopeGuard(
        this->projectionParentScopes_, childScope);
    if (!scopeGuard.isActive())
    {
      return LayoutNodeResult(state.width, state.y);
    }

    loka::app::scene::INestable *nestable = scrollView->asNestable();
    const std::size_t ledgerMark = this->rectSurfaceExtentLedger_.mark();
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(
        nestable ? nestable->childrenHead() : 0,
        nestable ? nestable->childrenCount() : 0);
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      if (currentY < SHRT_MIN || currentY > SHRT_MAX)
      {
        this->refuseScrollViewShortRange();
        break;
      }
      LayoutState childState = childBase;
      childState.y = currentY;
      const int nextY = this->layoutNode(child, childState);
      if (this->projectionParentScopes_.current().hasShortRangeRefusal())
      {
        break;
      }
      if (!this->projectionParentScopes_.current().tryAccumulateContentHeight(
              currentY, nextY))
      {
        this->refuseScrollViewShortRange();
        break;
      }
      currentY = nextY;
    }
    contentHeight = this->projectionParentScopes_.current().contentHeight();
    shortRangeRefused =
        this->projectionParentScopes_.current().hasShortRangeRefusal();
      if (shortRangeRefused)
    {
      // Seats recorded under a refused scope are not facts.
      this->rectSurfaceExtentLedger_.discardSince(ledgerMark);
    }
  }

  if (!shortRangeRefused)
  {
    const int clampedOffset =
        ctx->setScrollMetrics(contentHeight, state.height, requestedOffset);
    if (scrollView->props.offset_.isValid() &&
        scrollView->props.offset_.state()->get() != clampedOffset)
    {
      // Publish through the complete NodeState door after the scope has
      // popped. A dirty notification may schedule/re-enter projection, and
      // every projection pass must start from the root frame.
      scrollView->props.offset_.set(clampedOffset);
    }
  }

  if (state.height > 0)
  {
    return LayoutNodeResult(state.width, state.y + state.height);
  }
  if (currentY < SHRT_MIN || currentY > SHRT_MAX)
  {
    return LayoutNodeResult(state.width, state.y);
  }
  return LayoutNodeResult(state.width, currentY);
}

int Win32ScenePlatformController::layoutNode(loka::app::scene::Node *node, const LayoutState &state)
{
  if (!node)
  {
    return state.y;
  }
  if (this->projectionParentScopes_.activeDepth() != 0 &&
      this->projectionParentScopes_.current().hasShortRangeRefusal())
  {
    // A container above may already have narrowed its next coordinate. Once
    // this frame refuses, nothing else in the subtree may materialize from
    // that poisoned value.
    return state.y;
  }
  return this->applyBoundaryLayoutResult(node->asBoundary(), state.x, state.y, this->computeLayoutResult(node, state));
}

Win32ScenePlatformController::LayoutNodeResult
Win32ScenePlatformController::computeLayoutResult(loka::app::scene::Node *node, const LayoutState &state)
{
  if (loka::app::ScrollViewNode *scrollView = node->asScrollViewNode())
  {
    return this->layoutScrollViewNode(scrollView, state);
  }
  // Canvas shares geometry with Null; this rail only adapts its result channel.
  if (node->asCanvasNode())
  {
    loka::app::scene::LayoutState handlerState;
    if (!this->narrowLayoutState(state, handlerState, false))
    {
      node->asCanvasNode()->recordLayoutStatus(loka::app::CANVAS_LAYOUT_SHORT_RANGE_REFUSED);
      return LayoutNodeResult(state.width, state.y);
    }
    loka::app::scene::Win32PlatformLayoutTraversal traversal(this);
    loka::app::layout::CanvasPlatformLayoutHandler handler;
    return LayoutNodeResult(state.width, handler.layoutNode(node, handlerState, &traversal));
  }
  if (loka::app::StackNode *stack = node->asStackNode())
  {
    int resultY = state.y;
    loka::app::scene::IPlatformLayoutHandler *handler = this->layoutHandlerRegistry_.find(stack);
    if (handler)
    {
      loka::app::scene::LayoutState handlerState;
      if (!this->narrowLayoutState(state, handlerState, false))
      {
        return LayoutNodeResult(state.width, state.y);
      }
      loka::app::scene::Win32PlatformLayoutTraversal traversal(this);
      resultY = handler->layoutNode(stack, handlerState, &traversal);
    }
    else if (stack->props.effectiveAxis() == loka::app::STACK_AXIS_COLUMN)
    {
      resultY = loka::app::layout::computeColumnLayoutResultY(
          stack, state, this, &Win32ScenePlatformController::layoutContainerChild);
    }
    else
    {
      const loka::app::layout::RowLayoutMetrics metrics =
          loka::app::layout::FallbackControlMetrics::rowLayout();
      resultY = loka::app::layout::computeRowLayoutResultY(
          stack, state, metrics, this,
          this, &Win32ScenePlatformController::layoutContainerChild);
    }
    return LayoutNodeResult(state.width, resultY);
  }

  if (loka::app::GridNode *grid = node->asGridNode())
  {
    int maxY = state.y;
    loka::app::scene::IPlatformLayoutHandler *handler = this->layoutHandlerRegistry_.find(grid);
    if (handler)
    {
      loka::app::scene::LayoutState handlerState;
      if (!this->narrowLayoutState(state, handlerState, false))
      {
        return LayoutNodeResult(state.width, state.y);
      }
      loka::app::scene::Win32PlatformLayoutTraversal traversal(this);
      maxY = handler->layoutNode(grid, handlerState, &traversal);
    }
    else
    {
      loka::app::layout::GridLayoutMetrics metrics;
      metrics.gapX = 0;
      metrics.gapY = 0;
      maxY = loka::app::layout::computeGridLayoutResultY(
          grid, state, metrics, this, &Win32ScenePlatformController::layoutContainerChild);
    }
    return LayoutNodeResult(state.width, maxY);
  }

  if (loka::app::BoxNode *box = node->asBoxNode())
  {
    int resultY = state.y;
    loka::app::scene::IPlatformLayoutHandler *handler = this->layoutHandlerRegistry_.find(box);
    if (handler)
    {
      loka::app::scene::LayoutState handlerState;
      if (!this->narrowLayoutState(state, handlerState, false))
      {
        return LayoutNodeResult(state.width, state.y);
      }
      loka::app::scene::Win32PlatformLayoutTraversal traversal(this);
      resultY = handler->layoutNode(box, handlerState, &traversal);
    }
    else
    {
      resultY = loka::app::layout::computeBoxLayoutResultY(
          box, state, this, &Win32ScenePlatformController::layoutContainerChild);
    }
    return LayoutNodeResult(state.width, resultY);
  }

  if (loka::app::ZStackNode *stack = node->asZStackNode())
  {
    int maxY = state.y;
    loka::app::scene::IPlatformLayoutHandler *handler = this->layoutHandlerRegistry_.find(stack);
    if (handler)
    {
      loka::app::scene::LayoutState handlerState;
      if (!this->narrowLayoutState(state, handlerState, false))
      {
        return LayoutNodeResult(state.width, state.y);
      }
      loka::app::scene::Win32PlatformLayoutTraversal traversal(this);
      maxY = handler->layoutNode(stack, handlerState, &traversal);
    }
    else
    {
      maxY = loka::app::layout::computeZStackLayoutResultY(
          stack, state, this, &Win32ScenePlatformController::layoutContainerChild);
    }
    return LayoutNodeResult(state.width, maxY);
  }

  if (loka::app::scene::INestable *nestable = node->asNestable())
  {
    LayoutState childState = state;
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      const int nextY = this->layoutNode(child, childState);
      if (this->refuseNarrowingInScrollScope(nextY))
      {
        break;
      }
      childState.y = nextY;
    }
    return LayoutNodeResult(state.width, childState.y);
  }

  if (node->asProjectedLayoutNode())
  {
    return DispatchProjectedLayout(this, node, state);
  }

  LeafLayoutHandlerFn leafLayoutHandler = this->leafLayoutHandlerRegistry_.find(node);
  if (leafLayoutHandler)
  {
    return leafLayoutHandler(this, node, state);
  }

  LeafLayoutHandlerFn hostActionHandler = this->hostActionHandlerRegistry_.find(node);
  if (hostActionHandler)
  {
    return hostActionHandler(this, node, state);
  }

  if (loka::app::RectSurfaceNode *surface = node->asRectSurfaceNode())
  {
    return this->layoutRectSurfaceNode(surface, state);
  }

  return LayoutNodeResult(state.width, state.y);
}

Win32ScenePlatformController::LayoutNodeResult
Win32ScenePlatformController::DispatchProjectedLayout(
    Win32ScenePlatformController *controller,
    loka::app::scene::Node *node,
    const LayoutState &state)
{
  if (!controller || !node)
  {
    return LayoutNodeResult(state.width, state.y);
  }
  loka::app::scene::IProjectedLayoutNode *projected =
      node->asProjectedLayoutNode();
  if (!projected)
  {
    return LayoutNodeResult(state.width, state.y);
  }
  loka::app::scene::LayoutState projectedState;
  if (!controller->narrowLayoutState(state, projectedState, true))
  {
    return LayoutNodeResult(state.width, state.y);
  }
  const int projectedResult =
      projected->layoutProjected(controller, projectedState);
  if (controller->projectionParentScopes_.activeDepth() == 0)
  {
    return LayoutNodeResult(state.width, projectedResult);
  }
  int contentResult = state.y;
  if (!controller->projectionParentScopes_.current().restoreContentY(
          projectedResult, contentResult))
  {
    controller->refuseScrollViewShortRange();
    return LayoutNodeResult(state.width, state.y);
  }
  return LayoutNodeResult(state.width, contentResult);
}

bool Win32ScenePlatformController::narrowLayoutState(
    const LayoutState &state,
    loka::app::scene::LayoutState &narrowed,
    bool applyProjection)
{
  if (state.x < SHRT_MIN || state.x > SHRT_MAX ||
      state.y < SHRT_MIN || state.y > SHRT_MAX ||
      state.width < SHRT_MIN || state.width > SHRT_MAX ||
      state.height < SHRT_MIN || state.height > SHRT_MAX)
  {
    this->refuseScrollViewShortRange();
    return false;
  }

  loka::app::scene::LayoutState content;
  content.x = static_cast<short>(state.x);
  content.y = static_cast<short>(state.y);
  content.width = static_cast<short>(state.width);
  content.height = static_cast<short>(state.height);
  content.lineHeight = 0;
  content.spacing = 0;
  if (applyProjection && this->projectionParentScopes_.activeDepth() != 0)
  {
    if (!this->projectionParentScopes_.current().project(content, narrowed))
    {
      this->refuseScrollViewShortRange();
      return false;
    }
    narrowed.lineHeight = content.lineHeight;
    narrowed.spacing = content.spacing;
    return true;
  }
  narrowed = content;
  return true;
}

void Win32ScenePlatformController::refuseScrollViewShortRange()
{
  if (this->projectionParentScopes_.activeDepth() != 0)
  {
    this->projectionParentScopes_.current().markShortRangeRefused();
  }
}

bool Win32ScenePlatformController::refuseNarrowingInScrollScope(int resultY)
{
  if (this->projectionParentScopes_.activeDepth() == 0)
  {
    return false;
  }
  loka::app::scene::ProjectionParentScope &scope =
      this->projectionParentScopes_.current();
  if (scope.hasShortRangeRefusal())
  {
    return true;
  }
  if (resultY >= SHRT_MIN && resultY <= SHRT_MAX)
  {
    return false;
  }
  scope.markShortRangeRefused();
  return true;
}

int Win32ScenePlatformController::layoutContainerChild(void *context,
                                                       loka::app::scene::Node *child,
                                                       const LayoutState &state)
{
  Win32ScenePlatformController *controller = static_cast<Win32ScenePlatformController *>(context);
  if (!controller)
  {
    return state.y;
  }
  return controller->layoutNode(child, state);
}

void Win32ScenePlatformController::clearContexts()
{
  clearNodeContexts(rootNode_);
}

void Win32ScenePlatformController::clearNodeContexts(loka::app::scene::Node *node)
{
  if (!node)
  {
    return;
  }
  // Parked retained branches (Conditional slots) hand their native pairs
  // over here too — the retire door, not the reclaim drain.
  for (unsigned i = 0; loka::app::scene::Node *branch = node->retainedLifecycleBranch(i); ++i)
  {
    clearNodeContexts(branch);
  }
  if (loka::app::scene::INestable *nestable = node->asNestable())
  {
    loka::dsl::CompositionCursor<loka::app::scene::Node> it(nestable->childrenHead(), nestable->childrenCount());
    for (loka::app::scene::Node *child = it.next(); child; child = it.next())
    {
      clearNodeContexts(child);
    }
  }
  node->setContext(0);
}

int Win32ScenePlatformController::measureClientWidth(int requestedWidth) const
{
  if (requestedWidth > 0)
  {
    return requestedWidth;
  }
  RECT rc;
  if (rootHwnd_ && GetClientRect(rootHwnd_, &rc))
  {
    return this->displayScale_.clientCapacityToLu(rc.right - rc.left);
  }
  return 260;
}

HWND loka::win32::CreateEditTextControl(HWND parent, const NativeRect &geometry)
{
  return CreateWindowExW(EditTextControlExStyle(),
                         L"EDIT",
                         L"",
                         EditTextControlStyle(),
                         geometry.r.left,
                         geometry.r.top,
                         geometry.r.right - geometry.r.left,
                         geometry.r.bottom - geometry.r.top,
                         parent,
                         NULL,
                         GetModuleHandleW(NULL),
                         NULL);
}

bool Win32ScenePlatformController::readNativeFocus(loka::app::scene::NodeContext *&out)
{
  out = 0;
  if (!this->rootHwnd_ || GetActiveWindow() != this->rootHwnd_)
    return false;
  const HWND focused = GetFocus();
  if (focused && IsChild(this->rootHwnd_, focused))
    out = Win32FocusParticipant::read(focused);
  return true;
}

bool Win32ScenePlatformController::applyNativeFocus(loka::app::scene::NodeContext &ctx)
{
  if (!this->rootHwnd_ || GetActiveWindow() != this->rootHwnd_)
    return false;
  loka::app::scene::Node *owner = ctx.owner();
  if (!owner || owner->getContext() != &ctx || owner->lifecycleFact() != loka::app::scene::NODE_FACT_ATTACHED)
    return false;
  const HWND hwnd = Win32FocusParticipant::target(&ctx);
  if (!hwnd || !IsChild(this->rootHwnd_, hwnd) || Win32FocusParticipant::read(hwnd) != &ctx)
    return false;
  // IME commit on kill-focus can run the Scene and free ctx: after SetFocus only
  // the copied HWND is compared. Its return is the previous handle, not success.
  SetFocus(hwnd);
  return GetFocus() == hwnd;
}

void Win32ScenePlatformController::releaseMenu()
{
  Win32Window *window = this->rootHwnd_
      ? reinterpret_cast<Win32Window *>(GetWindowLongPtrW(this->rootHwnd_, GWLP_USERDATA)) : 0;
  if (window)
    window->menuAttachment().releaseFrom(window->scene());
}
