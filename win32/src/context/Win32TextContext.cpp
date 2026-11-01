#include "Win32TextContext.hpp"
#include "Win32TextColorProperty.hpp"
#include "Win32AttributedTextTable.hpp"
#include <cassert>
#include <climits>
#include "../Win32ScenePlatformController.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/scene/projection/RetainedNodeHandler.hpp"
#include "app/nodes/Text.hpp"
#include "core/resource/Image.hpp"
#include "core/State.hpp"
#include "platform/Win32String.hpp"

namespace
{
  const wchar_t kTextContext[] = L"Loka.TextContext";
  class Win32TextNodeHandler
      : public loka::app::scene::RetainedNodeHandler<Win32TextNodeHandler,
                                                     loka::app::TextNode,
                                                     Win32TextContext>
  {
  public:
    static loka::app::TextNode *cast(loka::app::scene::Node *node)
    {
      return node ? node->asTextNode() : 0;
    }

    static Win32TextContext *create(loka::app::TextNode *text,
                                    loka::app::scene::IPlatformController *controller,
                                    const loka::app::scene::LayoutState &state)
    {
      Win32ScenePlatformController *win32 = static_cast<Win32ScenePlatformController *>(controller);
      return new Win32TextContext(
          win32, win32->projectionParentHwnd(), state.x, state.y, state.width, state.height, text);
    }

    static void refresh(Win32TextContext *ctx, const loka::app::scene::LayoutState &state)
    {
      ctx->relayout(state.x, state.y, state.width, state.height);
    }
  };

  Win32TextNodeHandler gWin32TextNodeHandler;

  /** STATIC text types are alternatives, not combinable alignment flags. */
  DWORD TextControlType(const loka::app::BlockStyle &block)
  {
    switch (block.hasAlign_ ? block.align_ : loka::app::TEXT_ALIGN_LEFT)
    {
    case loka::app::TEXT_ALIGN_LEFT:
      return block.hasTruncation_ && block.truncation_ == loka::app::TEXT_TRUNCATION_ELLIPSIS
                     && block.hasWrap_ && block.wrap_ != loka::app::TEXT_WRAP_NONE
                 ? SS_LEFT : SS_LEFTNOWORDWRAP;
    case loka::app::TEXT_ALIGN_CENTER:
      return SS_CENTER;
    case loka::app::TEXT_ALIGN_RIGHT:
      return SS_RIGHT;
    }
    return SS_LEFT;
  }

  /** The STATIC type bits a props value asks for; undeclared style keeps the
      creation default (SS_LEFT), so a retained style removal restores it. */
  DWORD TextControlTypeFor(const loka::app::TextProps &props)
  {
    return props.hasDeclaredLayoutStyle() ? TextControlType(props.blockStyle_) : SS_LEFT;
  }

  HFONT ResolveTextFont(const loka::app::TextNode *text,
                        const Win32ScenePlatformController *controller)
  {
    if (!text || !controller || !text->props.hasDeclaredLayoutStyle())
      return 0;
    const loka::app::TextStyle style = text->props.resolvedTextStyle();
    return style.declaresFont() ? controller->fallbackFont(style) : 0;
  }

  /** A null selected font preserves the historical unstyled height. */
  bool MinimumTextHeight(HWND hwnd, HFONT font, const Win32ScenePlatformController *controller, int &height)
  {
    const int fallback = loka::app::layout::FallbackControlMetrics::kTextHeight;
    height = fallback;
    if (!hwnd || !controller)
      return false;
    if (!font)
      return true;
    HDC hdc = GetDC(hwnd);
    if (!hdc)
      return false;
    HGDIOBJ previous = SelectObject(hdc, font);
    TEXTMETRICW metrics;
    ZeroMemory(&metrics, sizeof(metrics)); // MSVC C4701: the success flag, not the struct, gates the read
    const bool measured = previous && previous != HGDI_ERROR && GetTextMetricsW(hdc, &metrics) != FALSE;
    if (previous && previous != HGDI_ERROR)
      SelectObject(hdc, previous);
    ReleaseDC(hwnd, hdc);
    if (!measured)
      return false;
    const int measuredHeight = controller->displayScale().measurementToLu(metrics.tmHeight + metrics.tmExternalLeading);
    height = measuredHeight > fallback ? measuredHeight : fallback;
    return true;
  }

  /** Whether the window text equals `wide`, compared by content at every
      length. Short strings read into a stack buffer; longer ones borrow one
      temporary the size of the string being verified. */
  bool NativeTextIs(HWND hwnd, const std::wstring &wide)
  {
    const int length = GetWindowTextLengthW(hwnd);
    if (length < 0 || static_cast<size_t>(length) != wide.size())
      return false;
    wchar_t inlineBuffer[256];
    std::wstring heapBuffer;
    wchar_t *buffer = inlineBuffer;
    int capacity = static_cast<int>(sizeof(inlineBuffer) / sizeof(inlineBuffer[0]));
    if (wide.size() >= static_cast<size_t>(capacity))
    {
      heapBuffer.resize(wide.size() + 1);
      buffer = &heapBuffer[0];
      capacity = static_cast<int>(wide.size() + 1);
    }
    return GetWindowTextW(hwnd, buffer, capacity) == length
           && wide.compare(0, wide.size(), buffer, wide.size()) == 0;
  }

  bool GeneratesLines(const loka::app::TextProps &props)
  {
    const loka::app::BlockStyle &block = props.blockStyle_;
    return block.hasWrap_ && block.wrap_ != loka::app::TEXT_WRAP_NONE
           && !(block.hasTruncation_ && block.truncation_ == loka::app::TEXT_TRUNCATION_ELLIPSIS);
  }

  bool MeasureTextHeightForWidth(HWND hwnd,
                                const Win32ScenePlatformController *controller,
                                const std::wstring &wide,
                                int nativeWidth,
                                HFONT selectedFont,
                                int &height)
  {
    if (wide.empty())
      return true;
    RECT rc = {0, 0, nativeWidth, 0};
    if (!Win32TextContext::measureTextRect(hwnd, selectedFont, wide, rc))
      return false;
    const int measuredWithPadding = controller->displayScale().measurementToLu(rc.bottom - rc.top) + 8;
    if (measuredWithPadding > height)
      height = measuredWithPadding;
    return true;
  }

} // namespace

/** Shared native rectangle measurement for Text height and one-line width. */
bool Win32TextContext::measureTextRect(HWND hwnd, HFONT font, const std::wstring &wide, RECT &rect)
{
  HDC hdc = GetDC(hwnd);
  if (!hdc)
    return false;
  HGDIOBJ previousFont = font ? SelectObject(hdc, font) : 0;
  const bool selected = !font || (previousFont && previousFont != HGDI_ERROR);
  const UINT flags = DT_LEFT | DT_NOPREFIX | DT_CALCRECT;
  const bool measured = selected && DrawTextW(hdc, wide.c_str(), -1, &rect, flags) != 0;
  if (previousFont && previousFont != HGDI_ERROR)
    SelectObject(hdc, previousFont);
  ReleaseDC(hwnd, hdc);
  return measured;
}

bool Win32TextContext::queryNaturalWidth(const loka::app::TextNode *node,
                                         const Win32ScenePlatformController *controller,
                                         short &width)
{
  if (!node || !controller || !node->props.text_)
    return false;
  std::wstring wide;
  if (!loka::win32::MaterializeWideString(node->props.text_->get(), wide) || wide.empty()
      || wide.find_first_of(L"\r\n") != std::wstring::npos)
    return false;
  HFONT font = ResolveTextFont(node, controller);
  if (!font)
    font = controller->displayFont();
  // Unlike the height path, the answer needs the font the Text presents with.
  if (!font)
    return false;
  RECT rect = {0, 0, 0, 0};
  if (!measureTextRect(controller->rootHwnd(), font, wide, rect))
    return false;
  const int logicalWidth = controller->displayScale().measurementToLu(rect.right - rect.left);
  if (logicalWidth <= 0 || logicalWidth > SHRT_MAX)
    return false;
  width = static_cast<short>(logicalWidth);
  return true;
}

bool Win32TextContext::queryNaturalHeight(const loka::app::TextNode *node,
                                          const Win32ScenePlatformController *controller,
                                          short &height)
{
  if (!node || !controller || !node->props.text_)
    return false;
  const loka::app::BlockStyle &block = node->props.blockStyle_;
  if (block.hasWrap_ && block.wrap_ != loka::app::TEXT_WRAP_NONE)
    return false;
  std::wstring wide;
  if (!loka::win32::MaterializeWideString(node->props.text_->get(), wide)
      || wide.find_first_of(L"\r\n") != std::wstring::npos)
    return false;
  int local = 0;
  // Keep the leaf's null-font argument: unstyled Text has a 20-lu minimum.
  // MinimumTextHeight writes its fallback even on failure; commit only success.
  if (!MinimumTextHeight(controller->rootHwnd(), ResolveTextFont(node, controller), controller, local) || local <= 0
      || local > SHRT_MAX)
    return false;
  height = static_cast<short>(local);
  return true;
}

Win32TextContext::Win32TextContext(Win32ScenePlatformController *controller,
                                   HWND parent,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   loka::app::TextNode *node)
    : Win32RetirableContext(controller),
      textEnvironmentSubscription_(controller->textEnvironment_, *this),
      node_(node),
      textState_(0),
      didInitialApply_(false),
      textDelivery_(loka::app::scene::PaintAnswer::refused(loka::app::scene::PAINT_REFUSED_HISTORY_UNKNOWN))
{
  DWORD style = WS_VISIBLE | WS_CHILD | SS_LEFT | SS_NOPREFIX;
  if (node_ && node_->props.hasDeclaredLayoutStyle())
  {
    const loka::app::BlockStyle &attr = node_->props.blockStyle_;
    const bool truncEllipsis = attr.hasTruncation_ && attr.truncation_ == loka::app::TEXT_TRUNCATION_ELLIPSIS;
    style |= TextControlType(attr);
    if (truncEllipsis)
    {
      style |= SS_ENDELLIPSIS;
    }
  }
  // Unicode window: keeps WM_SETTEXT/paint in UTF-16 so the displayed text
  // matches what MeasureTextHeightForWidth measures with DrawTextW.
  this->createNativeChildWindow(
      0,
      L"STATIC",
      L"",
      style,
      this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)),
      parent,
      NULL,
      GetModuleHandleW(NULL),
      NULL);
  if (this->hwnd())
  {
    if (!SetPropW(this->hwnd(), kTextContext, this))
    {
      this->retireWindow();
      return;
    }
    HDC hdc = GetDC(this->hwnd());
    if (hdc)
    {
      SetBkMode(hdc, TRANSPARENT);
      ReleaseDC(this->hwnd(), hdc);
    }
  }
  this->applyStyle();
  bindText();
}

Win32TextContext *Win32TextContext::fromWindow(HWND window)
{
  return static_cast<Win32TextContext *>(GetPropW(window, kTextContext));
}

bool Win32TextContext::queryFontStyle(loka::app::TextStyle &style) const
{
  if (!this->node_ || !this->node_->props.hasDeclaredLayoutStyle())
    return false;
  style = this->node_->props.resolvedTextStyle();
  return style.declaresFont();
}

Win32TextContext::~Win32TextContext()
{
  assert(!this->hwnd() && "terminal fact delivery must queue the HWND before context reclaim");
}

/** A changed STATIC text has already requested its parent rectangle with
    erase=false, children=true. An unchanged native string owes no new damage. */
loka::app::scene::PaintAnswer Win32TextContext::queryPaintDamage(const loka::app::scene::PaintQuery &query) const
{
  using namespace loka::app::scene;
  if (!this->hwnd())
    return PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
  RECT placement;
  if (query.placement != PLACEMENT_ELIGIBLE || !GetClientRect(this->hwnd(), &placement) || IsRectEmpty(&placement))
    return PaintAnswer::refused(PAINT_REFUSED_PLACEMENT_UNSETTLED);
  if (!this->node_ || (!this->node_->props.ownsText() && this->node_->props.text_ != this->textState_))
    return PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  PaintAnswer answer = this->textDelivery_;
  if (answer.kind == PAINT_ANSWER_EXACT)
    answer.damage.scope = query.scope;
  return answer;
}

void Win32TextContext::readLifecycleFactOnAttach()
{
  if (this->node_ && this->node_->lifecycleFact() == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
}

void Win32TextContext::onNativeFact(loka::app::scene::NodeLifecycleFact previous,
                                    loka::app::scene::NodeLifecycleFact next)
{
  (void)previous;
  if (next == loka::app::scene::NODE_FACT_ATTACHED)
  {
    this->applyAttachedPresentation();
  }
  else
  {
    // DETACHED_RETAINED hides; terminal RETIRED keeps the same policy
    // (hide before the ritual destroys the native pair).
    this->textDelivery_ = loka::app::scene::PaintAnswer::refused(loka::app::scene::PAINT_REFUSED_HISTORY_UNKNOWN);
    this->applyDetachedPresentation();
    if (next == loka::app::scene::NODE_FACT_RETIRED)
    {
      if (this->hwnd())
        RemovePropW(this->hwnd(), kTextContext);
      this->clearMeasurement();
      this->unbindText();
      this->textEnvironmentSubscription_.disconnectTextEnvironment();
      this->node_ = 0;
    }
  }
}

bool Win32TextContext::applyStyle()
{
  if (!this->hwnd() || !this->node_ || !this->controller())
    return false;
  if (Win32TextColorProperty::apply(this->hwnd(), this->node_->props.resolvedTextStyle().color_))
  {
    this->textDelivery_ =
        Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd())
            ? loka::app::scene::PaintAnswer::nativeScheduled()
            : loka::app::scene::PaintAnswer::refused(loka::app::scene::PAINT_REFUSED_PROPS_UNRECONCILED);
  }
  bool changed = false;
  {
    // Reconciled on every apply, declared or not: a retained change back to an
    // undeclared style restores the creation default instead of keeping the
    // old SS_CENTER/SS_RIGHT type. Keep the HWND and unrelated flags.
    // CENTER/RIGHT use native STATIC wrapping; the NONE + CLIP overflow limit
    // is documented in the guide.
    const LONG_PTR style = GetWindowLongPtrW(this->hwnd(), GWL_STYLE);
    const LONG_PTR alignedStyle = (style & ~static_cast<LONG_PTR>(SS_TYPEMASK | SS_ENDELLIPSIS))
                                  | TextControlTypeFor(this->node_->props)
                                  | (this->node_->props.blockStyle_.hasTruncation_
                                     && this->node_->props.blockStyle_.truncation_ == loka::app::TEXT_TRUNCATION_ELLIPSIS
                                         ? SS_ENDELLIPSIS : 0);
    if (style != alignedStyle)
    {
      SetWindowLongPtrW(this->hwnd(), GWL_STYLE, alignedStyle);
      changed = true;
    }
  }
  HFONT font = ResolveTextFont(this->node_, this->controller());
  if (!font)
    font =
        this->controller()->fallbackFont(this->node_ ? this->node_->props.resolvedTextStyle() : loka::app::TextStyle());
  // WM_GETFONT is the native truth; no cached handle can outlive a DPI table.
  if (font && reinterpret_cast<HFONT>(SendMessageW(this->hwnd(), WM_GETFONT, 0, 0)) != font)
  {
    SendMessageW(this->hwnd(), WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    changed = true;
  }
  if (changed)
    InvalidateRect(this->hwnd(), NULL, TRUE);
  return changed;
}

void Win32TextContext::onPropsApplied()
{
  // Platform twin: MacTextContext::onPropsApplied uses the same literal/live
  // source decision, with deferred binding delivery on macOS.
  this->measurement_.invalidate();
  if (!this->node_)
  {
    return;
  }
  if (this->node_->props.ownsText())
  {
    // Props-owned text is not a live source (TextNode::declareDirtySources
    // classifies ownsText as non-live): a literal-to-literal apply rewrites the
    // same owned State without notifying. Treat it as an applied snapshot.
    this->unbindText();
    this->applyText();
  }
  else if (this->node_->props.text_ != this->textState_)
  {
    this->unbindText();
    this->bindText();
  }
  else
    this->applyText();
  // Reconcile style last: unchanged text must not replace a color repaint's
  // native-scheduled answer with exact empty damage.
  if (this->applyStyle())
    this->controller()->requestRelayout();
}

void Win32TextContext::applyAttachedPresentation()
{
  this->measurement_.invalidate();
  if (this->hwnd())
  {
    // Detached presentation removed the color property; restore it only.
    Win32TextColorProperty::apply(
        this->hwnd(), this->node_ ? this->node_->props.resolvedTextStyle().color_ : loka::app::TEXT_COLOR_UNSET);
    ShowWindow(this->hwnd(), SW_SHOW);
  }
}

void Win32TextContext::applyDetachedPresentation()
{
  if (this->hwnd())
  {
    Win32TextColorProperty::detach(this->hwnd());
    ShowWindow(this->hwnd(), SW_HIDE);
  }
}

bool Win32TextContext::captureBitmap(loka::core::resource::Image &out) const
{
  return this->controller()->captureWindowClientBitmap(this->hwnd(), out);
}

void Win32TextContext::clearMeasurement()
{
  this->measurement_.invalidate();
  this->textDelivery_ = loka::app::scene::PaintAnswer::refused(loka::app::scene::PAINT_REFUSED_HISTORY_UNKNOWN);
  if (this->hwnd())
    this->relayout(0, 0, 0, 0);
}

void Win32TextContext::onTextEnvironmentChanged()
{
  this->clearMeasurement();
}

short Win32TextContext::layout(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
{
  this->applyStyle();
  const HFONT font = ResolveTextFont(this->node_, this->controller());
  // Generated lines are broken for the native width the STATIC receives:
  // edges projected at the placement, like the attributed rail (#1008: an
  // origin-zero width can be one pixel wider). Text that generates no lines
  // measures only its font height, so its key carries no width at all and a
  // moving label at a fractional scale keeps hitting.
  const int keyWidth = this->node_ && GeneratesLines(this->node_->props)
                           ? this->controller()->displayScale().nativeLength(state.x, state.x + state.width).px
                           : 0;
  const Constraint constraint(keyWidth,
                              font ? font : this->controller()->displayFont());
  if (state.inputs != loka::app::scene::NODE_DIRTY_NONE || !this->measurement_.reusable(constraint))
  {
    // A synchronous WM_SETTEXT reentry must not reuse the previous snapshot.
    this->measurement_.invalidate();
    int height = 0;
    bool measured = MinimumTextHeight(this->hwnd(), font, this->controller(), height);
    if (measured && this->node_ && GeneratesLines(this->node_->props))
    {
      const loka::app::scene::PaintAnswer styleDelivery = this->textDelivery_;
      this->textDelivery_ = loka::app::scene::PaintAnswer::refused(loka::app::scene::PAINT_REFUSED_HISTORY_UNKNOWN);
      Win32AttributedTextTable table;
      std::wstring wide;
      HDC dc = GetDC(this->hwnd());
      measured = dc && this->node_->props.text_
                 && table.build(loka::app::Styled(this->node_->props.text_->get(), this->node_->props.resolvedTextStyle()),
                                this->node_->props.blockStyle_, constraint.width, dc, *this->controller())
                 && table.joinLines(wide);
      if (dc)
        ReleaseDC(this->hwnd(), dc);
      measured = measured && MeasureTextHeightForWidth(this->hwnd(), this->controller(), wide,
                                                       constraint.width, constraint.font, height)
                 && this->writeText(wide);
      // Generated lines can be unchanged while style has queued native paint.
      if (measured && styleDelivery.kind == loka::app::scene::PAINT_ANSWER_NATIVE_SCHEDULED)
        this->textDelivery_ = styleDelivery;
    }
    if (!measured)
    {
      this->controller()->refuseTextMeasurement(this->node_, state);
      this->clearMeasurement();
      state.height = 0;
      return static_cast<short>(state.y);
    }
    this->measurement_.commit(constraint, height);
  }
  const int textHeight = this->measurement_.extent();
  this->relayout(state.x, state.y, state.width, textHeight);
  state.height = static_cast<short>(textHeight);
  return static_cast<short>(state.y + textHeight);
}

void Win32TextContext::relayout(int x, int y, int width, int height)
{
  if (!this->hwnd())
  {
    return;
  }
  this->positionNativeWindow(this->hwnd(),
                             this->controller()->displayScale().projectFrame(loka::core::Frame(x, y, width, height)));
}

void Win32TextContext::bindText()
{
  if (!node_)
  {
    return;
  }
  // Subscribe only to a borrowed live State; props-owned text is applied as a
  // snapshot (see onPropsApplied) and never subscribed to.
  textState_ = node_->props.ownsText() ? 0 : static_cast<loka::core::State<loka::core::String> *>(node_->props.text_);
  if (textState_)
  {
    textState_->bind(&Win32TextContext::TextChangedThunk, this, false);
  }
  this->applyText();
}

void Win32TextContext::unbindText()
{
  if (textState_)
  {
    textState_->unbind(&Win32TextContext::TextChangedThunk, this);
    textState_ = 0;
  }
}

void Win32TextContext::applyText()
{
  using namespace loka::app::scene;
  this->textDelivery_ = PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  // Always read the node's current props: for a borrowed State this is the
  // subscribed one, for owned text it is the latest applied literal.
  if (!this->hwnd() || !node_ || !node_->props.text_)
  {
    return;
  }
  if (GeneratesLines(this->node_->props))
  {
    this->measurement_.invalidate();
    this->controller()->requestRelayout();
    return;
  }
  std::wstring wide;
  if (!loka::win32::MaterializeWideString(node_->props.text_->get(), wide))
    wide.clear();
  this->writeText(wide);
  requestRelayoutIfNeeded();
}

bool Win32TextContext::writeText(const std::wstring &wide)
{
  using namespace loka::app::scene;
  this->textDelivery_ = PaintAnswer::refused(PAINT_REFUSED_PROPS_UNRECONCILED);
  // Only short strings are checked for "unchanged": the apply path never
  // allocates a previous-text buffer, so a long string counts as changed.
  const bool unchanged = this->didInitialApply_ && wide.size() < 256 && NativeTextIs(this->hwnd(), wide);
  // SetWindowTextW reports success whatever the window procedure answered to
  // WM_SETTEXT, so the write is verified by reading the text back: a refused
  // write leaves the previous text, which differs by content.
  const bool applied = SetWindowTextW(this->hwnd(), wide.c_str()) != FALSE && NativeTextIs(this->hwnd(), wide);
  if (Win32ScenePlatformController::requestTransparentChildRepaint(this->hwnd()) && applied)
  {
    const PaintDamage empty = {paintScope(), 0, 0, 0, 0, PAINT_COVERAGE_PAINT_ONLY};
    this->textDelivery_ = unchanged ? PaintAnswer::exact(empty) : PaintAnswer::nativeScheduled();
  }
  if (!didInitialApply_)
  {
    didInitialApply_ = true;
  }
  return applied;
}

void Win32TextContext::requestRelayoutIfNeeded()
{
  if (!didInitialApply_ || !node_ || !node_->props.blockStyle_.hasWrap_)
  {
    return;
  }
  if (node_->props.blockStyle_.wrap_ == loka::app::TEXT_WRAP_NONE)
  {
    return;
  }
  if (this->controller())
    this->controller()->requestRelayout();
}

void Win32TextContext::TextChangedThunk(void *userData)
{
  Win32TextContext *self = static_cast<Win32TextContext *>(userData);
  if (self)
  {
    self->applyText();
  }
}

void RegisterWin32TextNodeHandler(loka::app::scene::PlatformNodeHandlerRegistry &registry)
{
  registry.registerHandler(&gWin32TextNodeHandler);
}
