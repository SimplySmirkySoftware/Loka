#include "support/TextBufferFixture.hpp"
#include "app/nodes/controls/Card.hpp"
#include "support/Win32ImportRefusal.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "context/Win32EditTextContext.hpp"
#include <cstdlib>
#include "support/LifecycleFactTestAccess.hpp"
#include "context/Win32RibbonGroupEdgeContext.hpp"
#include "context/Win32CardBandContext.hpp"
#include "support/TextEditorStateOwner.hpp"
#include "app/nodes/controls/TextEditor.hpp"
#include "Win32NodeHandlerEnsureTests.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "platform/Win32String.hpp"
#include "platform/String.hpp"
#include <cwchar>

#include "support/TestVerify.hpp"
#include "support/RailTextLayoutFixture.hpp"
#include <cassert>
#include <climits>
#include <cstdio>
#include <string>
#include <windows.h>
#include "Win32BuiltInSupport.hpp"
#include "Win32ScenePlatformController.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "Win32SceneLayout.hpp"
#include "core/StateTracker.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/ScrollBar.hpp"
#include "context/Win32ButtonContext.hpp"
#include "context/Win32TextEditorContext.hpp"
#include "context/Win32TextContext.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/nodes/controls/Cell.hpp"
#include "app/nodes/controls/PopupMenu.hpp"
#include "app/RectSurface.hpp"
#include "context/Win32RetirableContext.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
// After Win32TextEditorContext.hpp: MSVC reads RequestSettlement's 1u << FollowUp as
// ambiguous with Show's operator<<(const State<bool> &, ...) when Show.hpp comes first.
#include "app/nodes/nestable/Show.hpp"

namespace
{
  BOOL CALLBACK CountChildThunk(HWND, LPARAM lParam)
  {
    ++*reinterpret_cast<int *>(lParam);
    return TRUE;
  }

  int countChildWindows(HWND root)
  {
    int count = 0;
    EnumChildWindows(root, CountChildThunk, reinterpret_cast<LPARAM>(&count));
    return count;
  }

  HWND nthChildWindow(HWND parent, int index)
  {
    HWND child = GetWindow(parent, GW_CHILD);
    for (int i = 0; child && i < index; ++i)
      child = GetWindow(child, GW_HWNDNEXT);
    return child;
  }

  RECT childRectInParent(HWND child, HWND parent)
  {
    RECT r;
    GetWindowRect(child, &r);
    POINT tl = {r.left, r.top};
    POINT br = {r.right, r.bottom};
    ScreenToClient(parent, &tl);
    ScreenToClient(parent, &br);
    RECT out = {tl.x, tl.y, br.x, br.y};
    return out;
  }
  void verifyFixedColumnFitsAtBothScales()
  {
    using namespace loka::app;
    const int count = 3;
    const RailMetrics metrics[] = {RailMetrics(), loka::win32::DefaultRailMetrics()};
    for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    {
      const loka::win32::Win32DisplayScale scale(96, metrics[scaleIndex]);
      HWND root = CreateWindowExW(0, L"STATIC", L"column-fit", WS_POPUP, 0, 0,
                                  scale.clientLengthToNative(257).px,
                                  scale.clientLengthToNative(200).px,
                                  NULL, NULL, GetModuleHandle(NULL), NULL);
      LOKA_VERIFY(root != NULL);
      {
        Win32ScenePlatformController controller(root, scale);
        const int height = 2 * Win32SceneLayout::kRootInset + count * controller.rowLayoutMetrics().buttonHeight
                           + (count - 1) * Win32SceneLayout::kGap;
        LOKA_VERIFY(SetWindowPos(root, NULL, 0, 0, scale.clientLengthToNative(257).px,
                                scale.clientLengthToNative(height).px, SWP_NOMOVE | SWP_NOZORDER));
        RECT client;
        LOKA_VERIFY(GetClientRect(root, &client));
        LOKA_VERIFY(scale.clientCapacityToLu(client.right) == 257);
        LOKA_VERIFY(scale.clientCapacityToLu(client.bottom) == height);
        StackNode column((StackProps(STACK_AXIS_COLUMN)));
        ButtonNode *last = 0;
        for (int i = 0; i < count; ++i)
        {
          last = new ButtonNode(ButtonProps());
          column.addChild(last);
        }
        controller.onChange(&column, loka::app::scene::NODE_DIRTY_NONE, false);
        controller.relayoutNativeClientPixels(client.right, client.bottom);
        LOKA_VERIFY(countChildWindows(root) == count);
        Win32ButtonContext *context = static_cast<Win32ButtonContext *>(last->getContext());
        LOKA_VERIFY(context != 0);
        const RECT frame = childRectInParent(context->hwnd(), root);
        LOKA_VERIFY(frame.bottom == scale.projectEdge(height - Win32SceneLayout::kRootInset));
        LOKA_VERIFY(frame.bottom <= client.bottom);
        controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
      }
      LOKA_VERIFY(DestroyWindow(root));
    }
  }
  // Native-measurement twin of the Mac pin: the shared fixture owns the
  // composition; this rail independently checks GDI measurements and HWNDs.
  void verifyTextColumnExtents()
  {
    using namespace loka::app;
    const RailMetrics defaults = loka::win32::DefaultRailMetrics();
    const RailMetrics metrics[] = {RailMetrics(), defaults};
    const char *strings[] = {"First", "First\nSecond"};
    const wchar_t *wideStrings[] = {L"First", L"First\nSecond"};
    for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
      for (int boxed = 0; boxed < 2; ++boxed)
        for (int sample = 0; sample < 2; ++sample)
        {
          const loka::win32::Win32DisplayScale scale(96, metrics[scaleIndex]);
          HWND root = CreateWindowExW(0, L"STATIC", L"text-column", WS_POPUP, 0, 0,
                                      scale.clientLengthToNative(340).px,
                                      scale.clientLengthToNative(250).px,
                                      NULL, NULL, GetModuleHandleW(NULL), NULL);
          LOKA_VERIFY(root != NULL);
          {
            Win32ScenePlatformController controller(root, scale);
            RailTextLayoutFixture fixture(boxed != 0, strings[sample]);
            controller.onChange(&fixture.column, loka::app::scene::NODE_DIRTY_NONE, false);
            controller.relayout(0, 0);
            RECT client;
            LOKA_VERIFY(GetClientRect(root, &client));
            LOKA_VERIFY(scale.clientCapacityToLu(client.bottom) == 250);
            LOKA_VERIFY(countChildWindows(root) == (boxed ? 5 : 3));
            // Walk the root's children in creation order like the Mac twin
            // walks subviews: page, caption, [badge], button. The Button's
            // HWND anchors that order.
            // GetWindow(GW_CHILD) may list either creation order or z-order
            // (newest first); the Button's published HWND anchors which one.
            const int count = boxed ? 5 : 3;
            const int buttonIndex = boxed ? 3 : 2;
            Win32ButtonContext *buttonContext = static_cast<Win32ButtonContext *>(fixture.button->getContext());
            LOKA_VERIFY(buttonContext && buttonContext->hwnd());
            HWND button = buttonContext->hwnd();
            HWND page = 0;
            HWND caption = 0;
            if (nthChildWindow(root, buttonIndex) == button)
            {
              page = nthChildWindow(root, 0);
              caption = nthChildWindow(root, 1);
            }
            else if (nthChildWindow(root, count - 1 - buttonIndex) == button)
            {
              page = nthChildWindow(root, count - 1);
              caption = nthChildWindow(root, count - 2);
            }
            LOKA_VERIFY(page && caption && page != caption && caption != button);
            HDC dc = GetDC(page);
            LOKA_VERIFY(dc != NULL);
            HGDIOBJ previous = SelectObject(dc, controller.textFont(FontSize<18>()));
            TEXTMETRICW font;
            LOKA_VERIFY(GetTextMetricsW(dc, &font));
            const UINT flags = DT_LEFT | DT_NOPREFIX | DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL;
            // The fixed Box seats the page at 300 lu; otherwise it spans the client inside the root inset.
            const int pageWidth = boxed ? 300 : 340 - 2 * Win32SceneLayout::kRootInset;
            RECT oneLine = {0, 0, scale.nativeLength(0, pageWidth).px, 0};
            LOKA_VERIFY(DrawTextW(dc, L"First", -1, &oneLine, flags) > 0);
            RECT measured = {0, 0, scale.nativeLength(0, pageWidth).px, 0};
            LOKA_VERIFY(DrawTextW(dc, wideStrings[sample], -1, &measured, flags) > 0);
            SelectObject(dc, previous);
            ReleaseDC(page, dc);
            const int nativeHeight = measured.bottom - measured.top;
            const int singleHeight = oneLine.bottom - oneLine.top;
            LOKA_VERIFY(nativeHeight == (sample + 1) * singleHeight);
            const int fontHeight = scale.measurementToLu(font.tmHeight + font.tmExternalLeading);
            const int minimum = fontHeight > 20 ? fontHeight : 20;
            const int padded = scale.measurementToLu(nativeHeight) + 8;
            const int textHeight = padded > minimum ? padded : minimum;
            const int captionY = Win32SceneLayout::kRootInset + (boxed ? 170 : textHeight) + Win32SceneLayout::kGap;
            const int buttonY = captionY + 20 + Win32SceneLayout::kGap;
            const RECT pageFrame = childRectInParent(page, root);
            const RECT expectedPage =
                scale
                    .projectFrame(
                        loka::core::Frame(Win32SceneLayout::kRootInset, Win32SceneLayout::kRootInset, pageWidth, textHeight))
                    .r;
            LOKA_VERIFY(EqualRect(&pageFrame, &expectedPage));
            LOKA_VERIFY(childRectInParent(caption, root).top == scale.projectEdge(captionY));
            const RECT buttonFrame = childRectInParent(button, root);
            LOKA_VERIFY(buttonFrame.top == scale.projectEdge(buttonY));
            const int buttonHeight = controller.rowLayoutMetrics().buttonHeight;
            LOKA_VERIFY(buttonFrame.bottom == scale.projectEdge(buttonY + buttonHeight));
            if (boxed)
              LOKA_VERIFY(buttonY == Win32SceneLayout::kRootInset + 170 + 20 + 2 * Win32SceneLayout::kGap);
            std::printf("  Win32 text extent: space=%d/%d boxed=%d lines=%d nativeText=%d "
                        "textLu=%d buttonLu=%d..%d framePx=%ld..%ld clientPx=%ld\n",
                        metrics[scaleIndex].spaceScale.num, metrics[scaleIndex].spaceScale.den,
                        boxed, sample + 1, nativeHeight, textHeight, buttonY, buttonY + buttonHeight,
                        buttonFrame.top, buttonFrame.bottom, client.bottom);
            controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
          }
          LOKA_VERIFY(DestroyWindow(root));
        }
  }
} // namespace

// Characterization for the per-cell ensure contract (#7 R1, moved out of
// Win32NodeContextMapper): a registered handler must (a) publish the created
// context through setContext, (b) reuse an existing context instead of
// materializing a second native window, and (c) route repeat ensures through
// the relayout path so the window follows the requested geometry. The
// attach-time lifecycle read is not discriminable headless (every Win32
// context creates its window WS_VISIBLE, and the read's distinct consumer is
// OpenFileDialog's presentIfNeeded, which cannot run in a test); that leg is
// covered by review plus the rig's Tutorial capture comparison.
void testWin32NodeHandlerEnsureContract()
{
  printf("\n==== [testWin32NodeHandlerEnsureContract] start ====\n");
  verifyFixedColumnFitsAtBothScales();
  verifyTextColumnExtents();
  HWND root = CreateWindowExW(
      0, L"STATIC", L"ensure-host", WS_OVERLAPPED, 0, 0, 320, 240, NULL, NULL, GetModuleHandle(NULL), NULL);
  assert(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);

    // -- Button: full contract via the hwnd accessor --
    loka::app::ButtonProps buttonProps;
    loka::app::ButtonNode button(buttonProps);

    loka::app::scene::LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 100;
    state.height = 30;
    LOKA_VERIFY(controller.prepareProjectedLayout(&button, state));

    Win32ButtonContext *ctx = static_cast<Win32ButtonContext *>(button.getContext());
    assert(ctx && "ensure must publish the created context through setContext");
    LOKA_VERIFY(ctx->hwnd() && IsWindow(ctx->hwnd()));
    RECT r = childRectInParent(ctx->hwnd(), root);
    assert(r.left == 10 && r.top == 20 && r.right - r.left == 100 && r.bottom - r.top == 30);
    const int childrenAfterFirstEnsure = countChildWindows(root);
    assert(childrenAfterFirstEnsure >= 1);

    // Second ensure with new geometry: same context, same window population,
    // window moved by the relayout path -- not recreated.
    state.x = 40;
    state.y = 50;
    state.width = 120;
    state.height = 40;
    LOKA_VERIFY(controller.prepareProjectedLayout(&button, state));
    LOKA_VERIFY(button.getContext() == ctx && "re-ensure must reuse the existing context, not recreate it");
    LOKA_VERIFY(countChildWindows(root) == childrenAfterFirstEnsure &&
           "re-ensure must not materialize another native window");
    r = childRectInParent(ctx->hwnd(), root);
    assert(r.left == 40 && r.top == 50 && r.right - r.left == 120 && r.bottom - r.top == 40 &&
           "re-ensure must route through relayout so the window follows the requested geometry");

    const HFONT font96 = reinterpret_cast<HFONT>(
        SendMessageW(ctx->hwnd(), WM_GETFONT, 0, 0));
    LOKA_VERIFY(font96);
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(192));
    LOKA_VERIFY(controller.prepareProjectedLayout(&button, state));
    const HFONT font192 = reinterpret_cast<HFONT>(
        SendMessageW(ctx->hwnd(), WM_GETFONT, 0, 0));
    LOKA_VERIFY(font192 && font192 != font96
                && "a DPI change must replace each native control's message font");
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(192));
    LOKA_VERIFY(reinterpret_cast<HFONT>(
                    SendMessageW(ctx->hwnd(), WM_GETFONT, 0, 0)) == font192
                && "a matching DPI must retain the already-applied font");
    LOGFONTW releasedFont;
    ZeroMemory(&releasedFont, sizeof(releasedFont));
    LOKA_VERIFY(GetObjectW(font96, sizeof(releasedFont), &releasedFont) == 0
                && "the replaced controller-owned font must be released");
    r = childRectInParent(ctx->hwnd(), root);
    assert(r.left == 80 && r.top == 100
           && r.right - r.left == 240 && r.bottom - r.top == 80
           && "a DPI change must reproject the same logical child frame");
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(96));
    LOKA_VERIFY(controller.prepareProjectedLayout(&button, state));
    LOKA_VERIFY(GetObjectW(font192, sizeof(releasedFont), &releasedFont) == 0
                && "each later scale replacement must release its prior font");

    // -- Text: same contract on a second node kind (no hwnd accessor; the
    // child-window census carries the not-recreated leg) --
    loka::app::TextProps textProps;
    loka::app::TextNode text(textProps);
    state.x = 5;
    state.y = 100;
    state.width = 200;
    state.height = 16;
    LOKA_VERIFY(controller.prepareProjectedLayout(&text, state));
    loka::app::scene::NodeContext *textCtx = text.getContext();
    assert(textCtx && "ensure must publish the created context through setContext");
    const int childrenWithText = countChildWindows(root);
    assert(childrenWithText == childrenAfterFirstEnsure + 1);
    LOKA_VERIFY(controller.prepareProjectedLayout(&text, state));
    LOKA_VERIFY(text.getContext() == textCtx);
    LOKA_VERIFY(countChildWindows(root) == childrenWithText);

    // -- TextEditor: a retained Unicode multiline native control --
    loka::app::testing::TextEditorStateOwner editorState;
    loka::testing::TextBufferFixture editorLines;
    LOKA_VERIFY(editorLines.attach(&editorState.tracker, 256) == loka::core::ATTACH_OK);
    LOKA_VERIFY(editorLines.insert(0, loka::core::String("line")) == loka::core::EDIT_OK);
    loka::app::TextEditorNode editor((loka::app::TextEditorProps(editorLines, editorState.cursor)));
    LOKA_VERIFY(controller.prepareProjectedLayout(&editor, state));
    Win32TextEditorContext *editorContext = static_cast<Win32TextEditorContext *>(editor.getContext());
    LOKA_VERIFY(editorContext && IsWindowUnicode(editorContext->hwnd()));
    LOKA_VERIFY(controller.prepareProjectedLayout(&editor, state));
    LOKA_VERIFY(editor.getContext() == editorContext);
    // Card bands now install real retained painters in debug and release builds.
    const int beforeBands = countChildWindows(root);
    for (int role = 0; role < loka::app::CARD_BAND_COUNT; ++role)
    {
      loka::app::CardBandNode band(loka::app::CardBandProps(static_cast<loka::app::CardBandRole>(role)));
      LOKA_VERIFY(controller.prepareProjectedLayout(&band, state));
      loka::app::scene::NodeContext *acceptedBand = band.getContext();
      LOKA_VERIFY(controller.prepareProjectedLayout(&band, state));
      LOKA_VERIFY(band.getContext() == acceptedBand && acceptedBand != 0);
      LOKA_VERIFY(IsWindowUnicode(static_cast<Win32CardBandContext *>(acceptedBand)->hwnd()));
    }
    controller.drainNativeRetirements();
    LOKA_VERIFY(countChildWindows(root) == beforeBands);
    // -- ScrollBar: Win32 has no native context for it; the registered
    // refusal stub must answer (false, no context) without tripping the
    // registry-miss education assert -- a known unsupported kind is a typed
    // refusal, not an accident. Reaching this line in a Debug build IS the
    // no-abort discrimination.
    loka::app::ScrollBarProps scrollProps;
    loka::app::ScrollBarNode scrollBar(scrollProps);
    state.x = 5;
    state.y = 130;
    state.width = 120;
    state.height = 16;
    LOKA_VERIFY(!controller.prepareProjectedLayout(&scrollBar, state) &&
           "an unsupported kind must refuse, not project");
    LOKA_VERIFY(!scrollBar.getContext());
    LOKA_VERIFY(countChildWindows(root) == childrenWithText + 1 &&
           "a refusal must not materialize a native window");

    printf("  button ctx=%p reused, children stable at %d; text ctx reused; scrollbar refused\n",
           static_cast<void *>(ctx), childrenWithText);
    // Nodes leave scope before the controller: ~Node retires and releases the
    // contexts, which destroys their child windows while root is still alive.
  }
  DestroyWindow(root);
  printf("==== [testWin32NodeHandlerEnsureContract] PASSED ====\n");
}

namespace
{
  HWND projectFontText(Win32ScenePlatformController &controller, HWND root,
                       loka::app::TextNode &text, int width = 160)
  {
    loka::app::scene::LayoutState state;
    state.x = 0;
    state.y = 0;
    state.width = static_cast<short>(width);
    state.height = 20;
    LOKA_VERIFY(controller.prepareProjectedLayout(&text, state));
    HWND first = GetWindow(root, GW_CHILD);
    // CreateWindowExW places a new child at the bottom of the sibling Z-order.
    HWND child = first ? GetWindow(first, GW_HWNDLAST) : NULL;
    LOKA_VERIFY(child);
    return child;
  }

  HFONT readFont(HWND child, LOGFONTW &descriptor)
  {
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
    LOKA_VERIFY(font);
    LOKA_VERIFY(GetObjectW(font, static_cast<int>(sizeof(descriptor)), &descriptor) != 0);
    return font;
  }

  int layoutFontText(Win32ScenePlatformController &controller, loka::app::TextNode &text)
  {
    loka::app::scene::LayoutState state;
    state.x = 0;
    state.y = 0;
    state.width = 160;
    state.height = 20;
    text.getContext()->layout(&controller, state);
    return state.height;
  }
}

void testWin32TextFontTable()
{
  using namespace loka::app;
  HWND root = CreateWindowExW(0, L"STATIC", L"font-host", WS_OVERLAPPED,
                              0, 0, 320, 240, NULL, NULL, GetModuleHandleW(NULL), NULL);
  LOKA_VERIFY(root);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    TextProps plainProps("Font table sample text with enough words to wrap over several lines.");
    TextNode plain(plainProps);
    HWND plainWindow = projectFontText(controller, root, plain);
    LOGFONTW defaultDescriptor;
    LOKA_VERIFY(readFont(plainWindow, defaultDescriptor) == controller.displayFont());
    LOKA_VERIFY(layoutFontText(controller, plain) == loka::app::layout::FallbackControlMetrics::kTextHeight);

    // Undeclared Text keeps the native default; declared alignment selects a
    // STATIC type without combining it with SS_LEFTNOWORDWRAP's type bits.
    LOKA_VERIFY((GetWindowLongPtrW(plainWindow, GWL_STYLE) & SS_TYPEMASK) == SS_LEFT);
    const TextAlign alignments[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
    for (int wraps = 0; wraps < 2; ++wraps)
      for (int ellipsis = 0; ellipsis < 2; ++ellipsis)
        for (int a = 0; a < 3; ++a)
        {
          TextProps alignedProps(plainProps);
          alignedProps.blockStyle_ = BlockStyle().wrap(wraps ? TEXT_WRAP_WORD : TEXT_WRAP_NONE)
              .truncation(ellipsis ? TEXT_TRUNCATION_ELLIPSIS : TEXT_TRUNCATION_CLIP).align(alignments[a]);
          TextNode aligned(alignedProps);
          HWND child = projectFontText(controller, root, aligned);
          const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
          const LONG_PTR expected = a == 0 ? (wraps && ellipsis ? SS_LEFT : SS_LEFTNOWORDWRAP)
                                          : a == 1 ? SS_CENTER : SS_RIGHT;
          LOKA_VERIFY((style & SS_TYPEMASK) == expected);
          LOKA_VERIFY((style & SS_EDITCONTROL) == 0);
          LOKA_VERIFY((style & SS_NOPREFIX) != 0);
          LOKA_VERIFY((style & SS_ENDELLIPSIS) == (ellipsis ? SS_ENDELLIPSIS : 0));
          if (a == 0)
          {
            // Retained LEFT -> CENTER keeps the context, HWND and other flags.
            SetWindowLongPtrW(child, GWL_STYLE, style | SS_NOPREFIX);
            loka::app::scene::NodeContext *context = aligned.getContext();
            const int children = countChildWindows(root);
            aligned.props.blockStyle_.align(TEXT_ALIGN_CENTER);
            context->onPropsApplied();
            LOKA_VERIFY(projectFontText(controller, root, aligned) == child);
            LOKA_VERIFY(aligned.getContext() == context);
            LOKA_VERIFY(countChildWindows(root) == children);
            const LONG_PTR retained = GetWindowLongPtrW(child, GWL_STYLE);
            LOKA_VERIFY((retained & SS_TYPEMASK) == SS_CENTER);
            LOKA_VERIFY((retained & ~static_cast<LONG_PTR>(SS_TYPEMASK))
                        == ((style | SS_NOPREFIX) & ~static_cast<LONG_PTR>(SS_TYPEMASK)));
            // Retained CENTER -> undeclared restores the creation default type.
            aligned.props.blockStyle_ = BlockStyle();
            context->onPropsApplied();
            LOKA_VERIFY(projectFontText(controller, root, aligned) == child);
            LOKA_VERIFY((GetWindowLongPtrW(child, GWL_STYLE) & SS_TYPEMASK) == SS_LEFT);
          }
        }

    TextProps largeProps(plainProps);
    largeProps.textStyle_ = FontSize<24>();
    TextNode largeMetrics(largeProps);
    HWND largeWindow = projectFontText(controller, root, largeMetrics);
    LOGFONTW descriptor;
    HFONT font96 = readFont(largeWindow, descriptor);
    LOKA_VERIFY(font96 != controller.displayFont());
    LOKA_VERIFY(descriptor.lfHeight == -MulDiv(24, 96, 96));
    LOKA_VERIFY(layoutFontText(controller, largeMetrics) > layoutFontText(controller, plain));

    TextProps boldProps(plainProps);
    boldProps.textStyle_ = Bold;
    TextNode bold(boldProps);
    HWND boldWindow = projectFontText(controller, root, bold);
    readFont(boldWindow, descriptor);
    LOKA_VERIFY(descriptor.lfWeight == FW_BOLD);
    LOKA_VERIFY(descriptor.lfHeight == defaultDescriptor.lfHeight);

    TextProps italicProps(plainProps);
    italicProps.textStyle_ = Italic;
    TextNode italic(italicProps);
    HWND italicWindow = projectFontText(controller, root, italic);
    readFont(italicWindow, descriptor);
    LOKA_VERIFY(descriptor.lfItalic != 0);

    // Same descriptor shares an admission-time handle, without lazy creation.
    TextNode same(largeProps);
    HWND sameWindow = projectFontText(controller, root, same);
    LOKA_VERIFY(readFont(sameWindow, descriptor) == font96);
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(144));
    HFONT font144 = readFont(largeWindow, descriptor);
    LOKA_VERIFY(font144 != font96);
    LOKA_VERIFY(descriptor.lfHeight == -MulDiv(24, 144, 96));
    LOKA_VERIFY(GetObjectW(font96, static_cast<int>(sizeof(descriptor)), &descriptor) == 0);
    LOKA_VERIFY(readFont(plainWindow, descriptor) == controller.displayFont());
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(144));
    LOKA_VERIFY(readFont(largeWindow, descriptor) == font144);

    // Retained apply changes the existing STATIC. An unmounted fixture has
    // no Scene retry owner and must not synthesize a WM_SIZE message.
    MSG message;
    while (PeekMessageW(&message, root, WM_SIZE, WM_SIZE, PM_REMOVE)) {}
    largeMetrics.props.textStyle_ = FontSize<12>() + Bold + Italic;
    largeMetrics.getContext()->onPropsApplied();
    LOKA_VERIFY(readFont(largeWindow, descriptor) != font144);
    LOKA_VERIFY(descriptor.lfHeight == -MulDiv(12, 144, 96));
    LOKA_VERIFY(descriptor.lfWeight == FW_BOLD && descriptor.lfItalic != 0);
    LOKA_VERIFY(!PeekMessageW(&message, root, WM_SIZE, WM_SIZE, PM_REMOVE));
    largeMetrics.props.textStyle_ = TextStyle();
    largeMetrics.getContext()->onPropsApplied();
    LOKA_VERIFY(readFont(largeWindow, descriptor) == controller.displayFont());

    // Layout is also the live-style projection path; it must not need props apply.
    largeMetrics.props.textStyle_ = FontSize<24>();
    layoutFontText(controller, largeMetrics);
    LOKA_VERIFY(readFont(largeWindow, descriptor) == font144);
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(96));
    TextProps wrappedProps(plainProps);
    wrappedProps.blockStyle_.wrap(TEXT_WRAP_WORD);
    TextNode wrapped(wrappedProps);
    projectFontText(controller, root, wrapped);
    wrappedProps.textStyle_ = FontSize<24>();
    TextNode wrappedLarge(wrappedProps);
    HWND wrappedLargeWindow = projectFontText(controller, root, wrappedLarge);
    LOKA_VERIFY(layoutFontText(controller, wrappedLarge) > layoutFontText(controller, wrapped));
    loka::app::scene::LayoutState repeated;
    repeated.width = 160;
    wrappedLarge.layout(&controller, repeated);
    const short measuredHeight = repeated.height;
    repeated.x = 9;
    repeated.y = 27;
    LOKA_VERIFY(controller.prepareProjectedLayout(&wrappedLarge, repeated));
    wrappedLarge.layout(&controller, repeated);
    LOKA_VERIFY(repeated.height == measuredHeight);
    RECT repeatedFrame;
    LOKA_VERIFY(GetWindowRect(wrappedLargeWindow, &repeatedFrame));
    MapWindowPoints(NULL, root, reinterpret_cast<POINT *>(&repeatedFrame), 2);
    LOKA_VERIFY(repeatedFrame.left == controller.displayScale().nativeEdge(repeated.x).px);
    LOKA_VERIFY(repeatedFrame.top == controller.displayScale().nativeEdge(repeated.y).px);
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(144));
    LOKA_VERIFY(GetClientRect(wrappedLargeWindow, &repeatedFrame) && IsRectEmpty(&repeatedFrame));
    wrappedLarge.layout(&controller, repeated);
    LOKA_VERIFY(repeated.height > 0);
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(96));

    loka::core::PushStateTracker tracker;
    loka::core::MutableState<TextStyle> liveStyle((FontSize<12>()));
    TextProps liveProps(plainProps);
    liveProps.textStyleState_ = &liveStyle;
    TextNode liveText(liveProps);
    HWND liveWindow = projectFontText(controller, root, liveText);
    readFont(liveWindow, descriptor);
    LOKA_VERIFY(descriptor.lfHeight == -MulDiv(12, 96, 96));
    {
      loka::core::StateTrackerGuard guard(&tracker);
      liveStyle.set(FontSize<24>() + Italic);
    }
    layoutFontText(controller, liveText);
    readFont(liveWindow, descriptor);
    LOKA_VERIFY(descriptor.lfHeight == -MulDiv(24, 96, 96));
    LOKA_VERIFY(descriptor.lfItalic != 0);
  }
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(144, loka::app::RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    TextProps props("Admission at 150 percent");
    props.textStyle_ = FontSize<24>();
    TextNode text(props);
    HWND child = projectFontText(controller, root, text);
    LOGFONTW descriptor;
    readFont(child, descriptor);
    LOKA_VERIFY(descriptor.lfHeight == -MulDiv(24, 144, 96));
  }
  DestroyWindow(root);
}


#include "context/Win32AttributedTextContext.hpp"
#include "support/LokaAllocFailure.hpp"
#include <climits>

namespace loka
{
  namespace testing
  {
    class Win32AttributedTextAccess
    {
    public:
      static const Win32AttributedTextTable &table(const Win32AttributedTextContext &context)
      {
        return context.table_;
      }
      static bool hasNode(const Win32AttributedTextContext &context)
      {
        return context.node_ != 0;
      }
      static bool known(const Win32AttributedTextContext &context)
      {
        return context.presented_.isKnown();
      }
      static void draw(Win32AttributedTextContext &context, HDC dc, const RECT &rect)
      {
        context.draw(dc, rect);
      }
      static HFONT font(const Win32AttributedTextTable &table, std::size_t i)
      {
        return table.fonts_[i];
      }
      static WCHAR unit(const Win32AttributedTextTable &table, std::size_t i)
      {
        return table.units_[i];
      }
      static std::size_t unitCount(const Win32AttributedTextTable &table)
      {
        return table.units_.size();
      }
      static const loka::app::TextLineMetrics &metrics(const Win32AttributedTextTable &table, std::size_t i)
      {
        return table.metrics_[i];
      }
      static Win32AttributedTextContext *fromWindow(HWND hwnd)
      {
        return reinterpret_cast<Win32AttributedTextContext *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
      }
    };
  } // namespace testing
} // namespace loka
namespace
{
  typedef loka::testing::Win32AttributedTextAccess AttributedAccess;
  loka::app::scene::LayoutState attributedSeat(short width)
  {
    loka::app::scene::LayoutState state;
    state.x = 1;
    state.y = 3;
    state.width = width;
    state.height = 80;
    return state;
  }
  int measureSpan(HWND hwnd, HFONT font, const wchar_t *text, int count)
  {
    HDC dc = GetDC(hwnd);
    LOKA_VERIFY(dc);
    HGDIOBJ previous = SelectObject(dc, font);
    SIZE size;
    LOKA_VERIFY(GetTextExtentExPointW(dc, text, count, INT_MAX, NULL, NULL, &size));
    SelectObject(dc, previous);
    ReleaseDC(hwnd, dc);
    return size.cx;
  }
  HWND attributedHost()
  {
    HWND root = CreateWindowExW(
        0, L"STATIC", L"attributed-host", WS_POPUP, 0, 0, 600, 400, NULL, NULL, GetModuleHandleW(NULL), NULL);
    LOKA_VERIFY(root);
    return root;
  }
} // namespace
void testWin32AttributedTextPerRunProjection()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  HWND root = attributedHost();
  {
    const loka::win32::Win32DisplayScale scale(144, RailMetrics());
    Win32ScenePlatformController controller(root, scale);
    LOKA_VERIFY(controller.textShaping() == PER_RUN);
    RefusedNodeHandler replacement(NodeTypeToken<AttributedTextNode>());
    LOKA_VERIFY(!controller.registerNodeHandler(&replacement));
    const AttributedString value = Styled("a ab", FontSize<12>() + Bold) + Styled("cd", FontSize<24>() + Italic);
    AttributedTextNode node((AttributedText(value) + BlockStyle().wrap(TEXT_WRAP_WORD)).props);
    const int wordWidth = measureSpan(root, controller.textFont(FontSize<12>() + Bold), L"ab", 2)
                          + measureSpan(root, controller.textFont(FontSize<24>() + Italic), L"cd", 2);
    // Choose odd logical width at odd origin: origin-free rounding differs.
    short width = static_cast<short>(scale.measurementToLu(wordWidth) + 2);
    if (!(width & 1))
      ++width;
    LayoutState state = attributedSeat(width);
    LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
    Win32AttributedTextContext *context = static_cast<Win32AttributedTextContext *>(node.getContext());
    LOKA_VERIFY(context && context->paintHwnd());
    context->layout(&controller, state);
    const Win32AttributedTextTable &table = AttributedAccess::table(*context);
    LOKA_VERIFY(table.valid() && table.lines().lineCount() == 2);
    LOKA_VERIFY(table.lines().line(1).fragmentCount == 2);
    const TextLineRecord &mixed = table.lines().line(1);
    const TextLineMetrics &smallMetrics = AttributedAccess::metrics(table, 0);
    const TextLineMetrics &largeMetrics = AttributedAccess::metrics(table, 1);
    LOKA_VERIFY(mixed.metrics.ascent == (smallMetrics.ascent > largeMetrics.ascent ? smallMetrics.ascent : largeMetrics.ascent));
    LOKA_VERIFY(mixed.metrics.descent == (smallMetrics.descent > largeMetrics.descent ? smallMetrics.descent : largeMetrics.descent));
    LOKA_VERIFY(mixed.width == wordWidth);
    RECT native = childRectInParent(context->paintHwnd(), root);
    LOKA_VERIFY(native.left == scale.projectEdge(1));
    LOKA_VERIFY(native.right - native.left == scale.nativeLength(1, 1 + width).px);
    LOKA_VERIFY(scale.nativeLength(1, 1 + width).px != scale.projectLength(width));
    const int expectedHeight =
        scale.measurementToLu(smallMetrics.ascent + smallMetrics.descent + smallMetrics.leading)
        + scale.measurementToLu(mixed.metrics.ascent + mixed.metrics.descent + mixed.metrics.leading);
    LOKA_VERIFY(state.height == expectedHeight);
    HWND child = context->paintHwnd();
    state = attributedSeat(width * 3);
    LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
    LOKA_VERIFY(node.getContext() == context && context->paintHwnd() == child);
    context->layout(&controller, state);
    LOKA_VERIFY(table.lines().lineCount() == 1);

    // Supply the ground that a parent paint provides on a real window, then
    // discriminate partial history and saved GDI attributes offscreen.
    HDC windowDC = GetDC(root);
    HDC dc = CreateCompatibleDC(windowDC);
    HBITMAP bitmap = CreateCompatibleBitmap(windowDC, 400, 200);
    LOKA_VERIFY(dc && bitmap);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    RECT rect = {0, 0, 400, 200};
    IntersectClipRect(dc, 0, 0, 400, 200);
    SetTextAlign(dc, TA_RIGHT | TA_TOP);
    SetBkMode(dc, OPAQUE);
    LOKA_VERIFY(FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))));
    AttributedAccess::draw(*context, dc, rect);
    LOKA_VERIFY(GetTextAlign(dc) == (TA_RIGHT | TA_TOP) && GetBkMode(dc) == OPAQUE);
    LOKA_VERIFY(AttributedAccess::known(*context));
    bool ink = false;
    for (int y = 0; y < 80; ++y)
      for (int x = 0; x < 200; ++x)
        if (GetPixel(dc, x, y) != RGB(255, 255, 255))
          ink = true;
    LOKA_VERIFY(ink);
    // Compare actual ink translations on the same GDI surface. Bearings stay
    // constant; the line offset must use this rail's measured painted width.
    int leftInk = 400;
    const TextAlign alignments[] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
    for (int a = 0; a < 3; ++a)
    {
      node.props.blockStyle_.align(alignments[a]);
      context->onPropsApplied();
      state = attributedSeat(width * 3);
      context->layout(&controller, state);
      LOKA_VERIFY(table.lines().lineCount() == 1 && table.lines().line(0).width < 400);
      LOKA_VERIFY(FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH))));
      AttributedAccess::draw(*context, dc, rect);
      int firstInk = 400;
      for (int y = 0; y < 80; ++y)
        for (int x = 0; x < firstInk; ++x)
          if (GetPixel(dc, x, y) != RGB(255, 255, 255))
            firstInk = x;
      LOKA_VERIFY(firstInk < 400);
      if (a == 0)
        leftInk = firstInk;
      const int slack = 400 - table.lines().line(0).width;
      LOKA_VERIFY(firstInk == leftInk + (a == 0 ? 0 : a == 1 ? slack / 2 : slack));
    }
    const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);
    IntersectClipRect(dc, 0, 0, 10, 10);
    AttributedAccess::draw(*context, dc, rect);
    LOKA_VERIFY(!AttributedAccess::known(*context));
    SelectClipRgn(dc, NULL);
    IntersectClipRect(dc, 0, 0, 400, 200);
    node.props = AttributedTextProps(AttributedString());
    context->onPropsApplied();
    state = attributedSeat(width);
    context->layout(&controller, state);
    // Empty text preserves supplied ground, including nonwhite ground.
    LOKA_VERIFY(FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))));
    AttributedAccess::draw(*context, dc, rect);
    LOKA_VERIFY(AttributedAccess::known(*context));
    for (int y = 0; y < 80; ++y)
      for (int x = 0; x < 200; ++x)
        LOKA_VERIFY(GetPixel(dc, x, y) == RGB(0, 0, 0));
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(root, windowDC);

    node.props = AttributedTextProps(Styled("\xef\xbc\xa1\xf0\x9f\x98\x80", FontSize<12>()));
    context->onPropsApplied();
    state = attributedSeat(width);
    context->layout(&controller, state);
    LOKA_VERIFY(AttributedAccess::unitCount(table) == 3);
    LOKA_VERIFY(AttributedAccess::unit(table, 0) == 0xff21);
    LOKA_VERIFY(AttributedAccess::unit(table, 1) == 0xd83d && AttributedAccess::unit(table, 2) == 0xde00);
    node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
    state = attributedSeat(1);
    context->layout(&controller, state);
    LOKA_VERIFY(table.lines().lineCount() == 2);
    const TextFragment &pair = table.lines().fragment(table.lines().line(1).firstFragment);
    LOKA_VERIFY(pair.end - pair.start == 2);
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(table.valid() && IsWindow(child));
    LOKA_VERIFY(!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE));
    // A hidden update is rebuilt from current props and survives reveal.
    node.props = AttributedTextProps(value);
    context->onPropsApplied();
    state = attributedSeat(width);
    context->layout(&controller, state);
    context->onFactChanged(NODE_FACT_DETACHED_RETAINED, NODE_FACT_ATTACHED);
    LOKA_VERIFY(table.valid() && table.value() == value);
    LOKA_VERIFY(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE);
    context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    LOKA_VERIFY(!table.valid() && !context->paintHwnd());
    LOKA_VERIFY(!AttributedAccess::hasNode(*context));
    LOKA_VERIFY(!static_cast<Win32RetirableContext *>(context)->hwnd());
    LOKA_VERIFY(GetWindowLongPtrW(child, GWLP_USERDATA) == 0);
    LOKA_VERIFY(IsWindow(child));
    controller.drainNativeRetirements();
    LOKA_VERIFY(!IsWindow(child));
  }
  LOKA_VERIFY(DestroyWindow(root));
}

namespace
{
  LRESULT CALLBACK ReenterPlainTextWrite(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
  {
    WNDPROC original = *static_cast<WNDPROC *>(GetPropW(hwnd, L"loka.test.original"));
    if (message == WM_SETTEXT)
    {
      // Restore first: one bounded native reentry, not recursive interception.
      SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
      Win32TextContext *context = static_cast<Win32TextContext *>(GetPropW(hwnd, L"loka.test.context"));
      loka::app::scene::LayoutState *state =
          static_cast<loka::app::scene::LayoutState *>(GetPropW(hwnd, L"loka.test.layout"));
      context->layout(0, *state);
      wchar_t native[256];
      LOKA_VERIFY(GetWindowTextW(hwnd, native, 256) == 8);
      // An early measurement commit would reuse the old WORD native string.
      LOKA_VERIFY(std::wstring(native) == L"ab c\ndef");
    }
    return CallWindowProcW(original, hwnd, message, wParam, lParam);
  }

  LRESULT CALLBACK RefusePlainTextWrite(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
  {
    if (message == WM_SETTEXT)
      return FALSE;
    return DefWindowProcW(hwnd, message, wParam, lParam);
  }
}

void testWin32PlainTextWrapWidthAtPlacement()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core::testing;
  // #1008: at 120 dpi and x=2 the origin-zero width is one pixel wider than
  // the STATIC's client, so lines broken for it re-wrapped (CENTER/RIGHT) or
  // clipped (LEFT) inside the control. Lines must be broken for the client.
  HWND root = attributedHost();
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(120, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    const TextAlign aligns[3] = {TEXT_ALIGN_LEFT, TEXT_ALIGN_CENTER, TEXT_ALIGN_RIGHT};
    for (int a = 0; a < 3; ++a)
    {
      TextProps props("The quick brown fox jumps over the lazy dog");
      props.blockStyle_.wrap(TEXT_WRAP_CHAR).align(aligns[a]);
      TextNode node(props);
      HWND child = projectFontText(controller, root, node);
      Win32TextContext *context = static_cast<Win32TextContext *>(node.getContext());
      LayoutState state;
      state.x = 2;
      state.y = 3;
      state.width = 43;
      state.height = 80;
      context->layout(&controller, state);
      RECT client;
      LOKA_VERIFY(GetClientRect(child, &client));
      const int clientWidth = client.right - client.left;
      // The break width must be the client width, not the origin-zero width.
      LOKA_VERIFY(clientWidth == controller.displayScale().nativeLength(2, 45).px);
      wchar_t native[256];
      const int length = GetWindowTextW(child, native, 256);
      LOKA_VERIFY(length > 0);
      HDC dc = GetDC(child);
      LOKA_VERIFY(dc);
      HFONT font = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
      HGDIOBJ previous = SelectObject(dc, font);
      LOKA_VERIFY(previous && previous != HGDI_ERROR);
      RECT one = {0, 0, clientWidth, 0};
      LOKA_VERIFY(DrawTextW(dc, L"X", 1, &one, DT_CALCRECT | DT_NOPREFIX));
      int lines = 0;
      int start = 0;
      for (int i = 0; i <= length; ++i)
      {
        if (i < length && native[i] != L'\n')
          continue;
        ++lines;
        // Every generated line fits the client width (no clip) and stays one
        // row under the STATIC's own word-breaking (no re-wrap).
        SIZE extent;
        LOKA_VERIFY(GetTextExtentPoint32W(dc, native + start, i - start, &extent));
        LOKA_VERIFY(extent.cx <= clientWidth);
        RECT rc = {0, 0, clientWidth, 0};
        LOKA_VERIFY(DrawTextW(dc, native + start, i - start, &rc, DT_CALCRECT | DT_NOPREFIX | DT_WORDBREAK));
        LOKA_VERIFY(rc.bottom == one.bottom);
        start = i + 1;
      }
      SelectObject(dc, previous);
      ReleaseDC(child, dc);
      LOKA_VERIFY(lines >= 2);
      context->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
      controller.drainNativeRetirements();
    }
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32PlainTextWrappedLines()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using namespace loka::core::testing;
  HWND root = attributedHost();
  failLokaAllocRaw("Win32AttributedText", "Break", 0);
  for (int dpi = 96; dpi <= 144; dpi += 48)
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(dpi, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    TextProps props("ab cdef");
    props.blockStyle_.wrap(TEXT_WRAP_CHAR);
    TextNode node(props);
    HWND child = projectFontText(controller, root, node);
    Win32TextContext *context = static_cast<Win32TextContext *>(node.getContext());
    HDC dc = GetDC(child);
    LOKA_VERIFY(dc);
    HFONT font = reinterpret_cast<HFONT>(SendMessageW(child, WM_GETFONT, 0, 0));
    LOKA_VERIFY(font == controller.textFont(props.resolvedTextStyle()));
    HGDIOBJ previous = SelectObject(dc, font);
    LOKA_VERIFY(previous && previous != HGDI_ERROR);
    SIZE glyphs = {0, 0};
    LOKA_VERIFY(GetTextExtentPoint32W(dc, L"abcd", 4, &glyphs));
    SelectObject(dc, previous);
    ReleaseDC(child, dc);
    LayoutState state = attributedSeat(static_cast<short>(controller.displayScale().measurementToLu(glyphs.cx)));
    context->layout(&controller, state);
    wchar_t native[256];
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 8);
    LOKA_VERIFY(std::wstring(native) == L"ab c\ndef");
    LOKA_VERIFY((GetWindowLongPtrW(child, GWL_STYLE) & SS_TYPEMASK) == SS_LEFTNOWORDWRAP);
    LOKA_VERIFY((GetWindowLongPtrW(child, GWL_STYLE) & SS_EDITCONTROL) == 0);
    LOKA_VERIFY((GetWindowLongPtrW(child, GWL_STYLE) & SS_NOPREFIX) != 0);
    const short twoLines = state.height;
    dc = GetDC(child);
    LOKA_VERIFY(dc);
    previous = SelectObject(dc, font);
    LOKA_VERIFY(previous && previous != HGDI_ERROR);
    RECT measured = {0, 0, glyphs.cx, 0};
    LOKA_VERIFY(DrawTextW(dc, native, -1, &measured, DT_CALCRECT | DT_NOPREFIX) != 0);
    SelectObject(dc, previous);
    ReleaseDC(child, dc);
    const int padded = controller.displayScale().measurementToLu(measured.bottom - measured.top) + 8;
    const int minimum = layout::FallbackControlMetrics::kTextHeight;
    LOKA_VERIFY(state.height == (padded > minimum ? padded : minimum));

    node.props.blockStyle_.wrap(TEXT_WRAP_WORD);
    context->onPropsApplied();
    state.inputs = NODE_DIRTY_NONE;
    context->layout(&controller, state);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 8);
    LOKA_VERIFY(std::wstring(native) == L"ab \ncdef");
    node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
    context->onPropsApplied();
    context->layout(&controller, state);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 8);
    LOKA_VERIFY(std::wstring(native) == L"ab c\ndef");
    LOKA_VERIFY(node.getContext() == context && IsWindow(child));

    PushStateTracker tracker;
    MutableState<String> live(String("ab cdef"));
    tracker.addState(&live);
    node.props = TextProps(&live);
    node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
    context->onPropsApplied();
    context->layout(&controller, state);
    {
      StateTrackerGuard guard(&tracker);
      live.set(String("xy zwvu"));
    }
    // No node dirty input: this discriminates the subscribed applyText invalidation.
    context->layout(&controller, state);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 8);
    LOKA_VERIFY(std::wstring(native) == L"xy z\nwvu");
    {
      StateTrackerGuard guard(&tracker);
      live.set(String("xy zwvu"), true);
    }
    context->layout(&controller, state);
    const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_EXACT);

    node.props.blockStyle_.wrap(TEXT_WRAP_NONE);
    context->onPropsApplied();
    context->layout(&controller, state);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 7);
    LOKA_VERIFY(std::wstring(native) == L"xy zwvu");
    LOKA_VERIFY(twoLines > state.height);
    node.props.blockStyle_.wrap(TEXT_WRAP_CHAR).truncation(TEXT_TRUNCATION_ELLIPSIS);
    context->onPropsApplied();
    context->layout(&controller, state);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 7);
    LOKA_VERIFY(std::wstring(native) == L"xy zwvu");
    LOKA_VERIFY((GetWindowLongPtrW(child, GWL_STYLE) & SS_ENDELLIPSIS) == SS_ENDELLIPSIS);
    node.props = TextProps("ab cdef\n");
    node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
    context->onPropsApplied(); // Also releases the borrowed live State before it dies.
    context->layout(&controller, state);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 9);
    LOKA_VERIFY(std::wstring(native) == L"ab c\ndef\n");
    LOKA_VERIFY((GetWindowLongPtrW(child, GWL_STYLE) & SS_ENDELLIPSIS) == 0);

    node.props = TextProps("ab cdef");
    node.props.blockStyle_.wrap(TEXT_WRAP_WORD);
    context->onPropsApplied();
    failLokaAllocRaw("Win32AttributedText", "Break", 1);
    context->layout(&controller, state);
    LOKA_VERIFY(state.height == 0);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 9);
    LOKA_VERIFY(std::wstring(native) == L"ab c\ndef\n");
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
    context->layout(&controller, state);
    LOKA_VERIFY(state.height > 0);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 8);
    LOKA_VERIFY(std::wstring(native) == L"ab \ncdef");

    node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
    context->onPropsApplied();
    WNDPROC original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&RefusePlainTextWrite)));
    LOKA_VERIFY(original);
    context->layout(&controller, state);
    LOKA_VERIFY(state.height == 0);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 8);
    LOKA_VERIFY(std::wstring(native) == L"ab \ncdef");
    SetWindowLongPtrW(child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
    context->layout(&controller, state);
    LOKA_VERIFY(state.height > 0);
    LOKA_VERIFY(GetWindowTextW(child, native, 256) == 8);
    LOKA_VERIFY(std::wstring(native) == L"ab c\ndef");

    // A refused write of a long string with the same length as the current
    // text is still detected: verification compares content at every length.
    {
      // Digits are tabular in the UI font, so both texts break into the same
      // lines and the generated strings have the same length; the pin checks
      // that precondition after the successful write below.
      const loka::core::String longA(std::string(300, '0').c_str());
      const loka::core::String longB(std::string(300, '1').c_str());
      node.props = TextProps(longA);
      node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
      context->onPropsApplied();
      context->layout(&controller, state);
      LOKA_VERIFY(state.height > 0);
      const int longLength = GetWindowTextLengthW(child);
      LOKA_VERIFY(longLength > 300);
      node.props = TextProps(longB);
      node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
      context->onPropsApplied();
      SetWindowLongPtrW(child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&RefusePlainTextWrite));
      context->layout(&controller, state);
      LOKA_VERIFY(state.height == 0);
      LOKA_VERIFY(GetWindowTextLengthW(child) == longLength);
      wchar_t first[2] = {0, 0};
      LOKA_VERIFY(GetWindowTextW(child, first, 2) == 1 && first[0] == L'0');
      SetWindowLongPtrW(child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
      context->layout(&controller, state);
      LOKA_VERIFY(state.height > 0);
      LOKA_VERIFY(GetWindowTextW(child, first, 2) == 1 && first[0] == L'1');
      LOKA_VERIFY(GetWindowTextLengthW(child) == longLength);
      node.props = TextProps("ab cdef");
      node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
      context->onPropsApplied();
      context->layout(&controller, state);
      LOKA_VERIFY(GetWindowTextW(child, native, 256) == 8);
      LOKA_VERIFY(std::wstring(native) == L"ab c\ndef");
    }

    node.props.blockStyle_.wrap(TEXT_WRAP_WORD);
    context->onPropsApplied();
    context->layout(&controller, state);
    node.props.blockStyle_.wrap(TEXT_WRAP_CHAR);
    context->onPropsApplied();
    LOKA_VERIFY(SetPropW(child, L"loka.test.original", &original));
    LOKA_VERIFY(SetPropW(child, L"loka.test.context", context));
    LOKA_VERIFY(SetPropW(child, L"loka.test.layout", &state));
    SetWindowLongPtrW(child, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ReenterPlainTextWrite));
    context->layout(&controller, state);
    RemovePropW(child, L"loka.test.original");
    RemovePropW(child, L"loka.test.context");
    RemovePropW(child, L"loka.test.layout");
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
  allowLokaAllocRaw();
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32AttributedTextAllocationFailure()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core::testing;
  HWND root = attributedHost();
  // The backend is installed across the entire allocation-balanced region.
  failLokaAllocRaw("Win32AttributedText", "Context", 1);
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    AttributedTextNode node((AttributedTextProps(Styled("sample", Bold))));
    LayoutState state = attributedSeat(100);
    LOKA_VERIFY(!controller.prepareProjectedLayout(&node, state));
    LOKA_VERIFY(!node.getContext());
    LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
    Win32AttributedTextContext *context = static_cast<Win32AttributedTextContext *>(node.getContext());
    context->layout(&controller, state);
    LOKA_VERIFY(AttributedAccess::table(*context).valid());
    failLokaAllocRaw("Win32AttributedText", "Break", 1);
    // A same-width ensure/layout must reuse the completed table: the armed
    // allocator refusal is not consumed, while placement still moves.
    state.y += 11;
    LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
    context->layout(&controller, state);
    LOKA_VERIFY(AttributedAccess::table(*context).valid());
    RECT placed;
    LOKA_VERIFY(GetWindowRect(context->paintHwnd(), &placed));
    MapWindowPoints(NULL, root, reinterpret_cast<POINT *>(&placed), 2);
    LOKA_VERIFY(placed.top == controller.displayScale().nativeEdge(state.y).px);
    ++state.width; // A real miss consumes the still-armed refusal.
    context->layout(&controller, state);
    LOKA_VERIFY(!AttributedAccess::table(*context).valid() && !AttributedAccess::known(*context));
    const PaintQuery query = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
    LOKA_VERIFY(context->queryPaintDamage(query).kind == PAINT_ANSWER_REFUSED);
    context->layout(&controller, state);
    LOKA_VERIFY(AttributedAccess::table(*context).valid());
  }
  LOKA_VERIFY(lokaAllocRawLive() == 0);
  allowLokaAllocRaw();
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32AttributedTextLiveDpiChange()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  // Same shape as the Text DPI pin above: the controller owns the display
  // scale, updateDisplayScale is the door WM_DPICHANGED reaches (pinned in
  // Win32WindowClientSizeTests), and the broadcast must revoke every borrowed
  // HFONT before the old generation is deleted.
  HWND root = attributedHost();
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, loka::win32::DefaultRailMetrics()));
    AttributedTextNode node((AttributedTextProps(Styled("var x = ", FontSize<12>() + Bold) + Styled("1;", FontSize<24>() + Italic))));
    LayoutState state = attributedSeat(200);
    LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
    HWND child = FindWindowExW(root, NULL, L"LOKA_ATTRIBUTED_TEXT", NULL);
    LOKA_VERIFY(child);
    Win32AttributedTextContext *context = AttributedAccess::fromWindow(child);
    LOKA_VERIFY(context && node.getContext() == context);
    context->layout(&controller, state);
    const Win32AttributedTextTable &table = AttributedAccess::table(*context);
    LOKA_VERIFY(table.valid());
    const HFONT oldFont = AttributedAccess::font(table, 0);
    LOGFONTW descriptor;
    LOKA_VERIFY(GetObjectW(oldFont, sizeof(descriptor), &descriptor));
    // Generation change: the table is revoked before the old handles die.
    controller.updateDisplayScale(loka::win32::Win32DisplayScale(144, loka::win32::DefaultRailMetrics()));
    LOKA_VERIFY(!table.valid() && !AttributedAccess::known(*context));
    RECT revoked;
    LOKA_VERIFY(GetClientRect(child, &revoked) && IsRectEmpty(&revoked));
    LOKA_VERIFY(GetObjectW(oldFont, sizeof(descriptor), &descriptor) == 0);
    // The next layout rebuilds against the new generation, never the dead handle.
    context->layout(&controller, state);
    LOKA_VERIFY(table.valid());
    LOKA_VERIFY(AttributedAccess::font(table, 0) != oldFont);
    LOKA_VERIFY(GetObjectW(AttributedAccess::font(table, 0), sizeof(descriptor), &descriptor));
    const loka::win32::Win32DisplayScale scale144(144, loka::win32::DefaultRailMetrics());
    LOKA_VERIFY(descriptor.lfHeight == -scale144.fontHeightToNative(12));
    LOKA_VERIFY(AttributedAccess::fromWindow(child) == context);
  }
}

#include "support/PropsReconciliation.hpp"
#include "testing/Win32ScenePlatformTestAccess.hpp"

void testWin32AttributedTextPaintRouting()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace PropsReconciliationSupport;
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;
  HWND rootWindow = attributedHost();
  loka::core::testing::failLokaAllocRaw("Win32AttributedText", "Break", 0);
  {
    Win32ScenePlatformController controller(rootWindow, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
    AttributedText declaration(Styled("var x = ", Bold) + Styled("1;", Italic));
    Scene scene((Boundary<Tree<AttributedText> >(Props<AttributedText>(&declaration))));
    scene.mount(&controller);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    settle(scene);
    BoundaryNode *boundary = root(scene);
    LOKA_VERIFY(boundary);
    controller.onChange(boundary, NODE_DIRTY_NONE, false);
    controller.relayout(320, 240);
    AttributedTextNode *node = boundary->childrenHead()->asAttributedTextNode();
    LOKA_VERIFY(node && node->getContext());
    Win32AttributedTextContext *context = static_cast<Win32AttributedTextContext *>(node->getContext());
    ShowWindow(rootWindow, SW_SHOWNOACTIVATE);
    Access::flushPendingInvalidations(controller);
    RedrawWindow(context->paintHwnd(), NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    LOKA_VERIFY(AttributedAccess::known(*context));
    BoundaryLocalApplyInfo info;
    info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
    PlatformApplyPlan plan;
    plan.paintKind = PlatformApplyPlan::PAINT_LOCAL;
    plan.setPrimaryRoot(boundary);
    Access::resetRedrawStats(controller);
    controller.onBoundaryApply(boundary, boundary, info, plan);
    LOKA_VERIFY(Access::onBoundaryApplyCalls(controller) == 1);
    LOKA_VERIFY(Access::queuedPaintInvalidates(controller) == 0);
    // Positive control: unknown presentation must take the broad fallback.
    SendMessageW(context->paintHwnd(), WM_SIZE, 0, 0);
    controller.onBoundaryApply(boundary, boundary, info, plan);
    LOKA_VERIFY(Access::queuedPaintInvalidates(controller) == 1);
    // A props-only update schedules layout; it must not remain an empty cache.
    MSG message;
    while (PeekMessageW(&message, rootWindow, WM_SIZE, WM_SIZE, PM_REMOVE))
    {
    }
    node->props = AttributedTextProps(Styled("changed", FontSize<24>()));
    context->onPropsApplied();
    LOKA_VERIFY(!PeekMessageW(&message, rootWindow, WM_SIZE, WM_SIZE, PM_REMOVE));
    LOKA_VERIFY(scene.hasPendingInvalidation());
    scene.flushInvalidation();
    LOKA_VERIFY(AttributedAccess::table(*context).valid());
    LOKA_VERIFY(AttributedAccess::table(*context).value() == node->props.text_->get());
    // Repeated refusal stays on the Scene queue, never the native message drain.
    node->requeueLayoutInputs(NODE_DIRTY_PROPS);
    for (int attempt = 0; attempt != 3; ++attempt)
    {
      while (PeekMessageW(&message, rootWindow, WM_NULL, WM_NULL, PM_REMOVE)) {}
      loka::core::testing::failLokaAllocRaw("Win32AttributedText", "Break", 1);
      if (attempt == 0) controller.relayout(320, 240);
      else scene.flushInvalidation();
      LOKA_VERIFY(!AttributedAccess::table(*context).valid());
      LOKA_VERIFY(scene.hasPendingInvalidation());
      LOKA_VERIFY(!PeekMessageW(&message, rootWindow, WM_SIZE, WM_SIZE, PM_REMOVE));
      LOKA_VERIFY(PeekMessageW(&message, rootWindow, WM_NULL, WM_NULL, PM_REMOVE));
    }
    loka::core::testing::failLokaAllocRaw("Win32AttributedText", "Break", 0);
    scene.flushInvalidation();
    LOKA_VERIFY(AttributedAccess::table(*context).valid());
    LOKA_VERIFY(!scene.hasPendingInvalidation());
    loka::dsl::testing::SceneTestAccess::unmount(scene);
  }
  LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
  loka::core::testing::allowLokaAllocRaw();
  LOKA_VERIFY(DestroyWindow(rootWindow));
}

namespace
{
  class RefusingNaturalWidthString : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &) const
    {
      return false;
    }
  };

  int expectedPushButtonWidth(Win32ScenePlatformController &controller, const wchar_t *label)
  {
    HDC dc = GetDC(controller.rootHwnd());
    LOKA_VERIFY(dc != NULL);
    HGDIOBJ previous = SelectObject(dc, controller.displayFont());
    LOKA_VERIFY(previous && previous != HGDI_ERROR);
    SIZE extent = {0, 0};
    const BOOL measured = GetTextExtentPoint32W(dc, label, static_cast<int>(std::wcslen(label)), &extent);
    LOKA_VERIFY(SelectObject(dc, previous) != NULL);
    LOKA_VERIFY(ReleaseDC(controller.rootHwnd(), dc));
    LOKA_VERIFY(measured);
    // Independent contract oracle: eight DIPs per side, including rail space scale.
    const int inset = controller.displayScale().nativeLength(0, 8).px;
    return controller.displayScale().measurementToLu(static_cast<int>(extent.cx) + 2 * inset);
  }

  void verifyButtonSeat(loka::app::ButtonNode *button, HWND root,
                        const loka::win32::Win32DisplayScale &scale, int x, int width)
  {
    Win32ButtonContext *context = static_cast<Win32ButtonContext *>(button->getContext());
    LOKA_VERIFY(context && context->hwnd());
    wchar_t className[32];
    LOKA_VERIFY(GetClassNameW(context->hwnd(), className, 32));
    LOKA_VERIFY(std::wcscmp(className, L"Button") == 0 || std::wcscmp(className, L"BUTTON") == 0);
    const RECT frame = childRectInParent(context->hwnd(), root);
    LOKA_VERIFY(frame.left == scale.projectEdge(x));
    LOKA_VERIFY(frame.right == scale.projectEdge(x + width));
    LOKA_VERIFY(frame.right - frame.left == scale.nativeLength(x, x + width).px);
  }

  // An unregistered Stack type takes the controller's existing direct Row path.
  class DirectNaturalWidthRow : public loka::app::StackNode
  {
  public:
    explicit DirectNaturalWidthRow(const loka::app::StackProps &props) : StackNode(props) {}
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<DirectNaturalWidthRow>();
    }
  };
}

void testWin32ButtonNaturalWidthAnswer()
{
  using namespace loka::app;
  const loka::win32::Win32DisplayScale scales[] = {
      loka::win32::Win32DisplayScale(96),
      loka::win32::Win32DisplayScale(144, loka::win32::DefaultRailMetrics())};
  for (int i = 0; i < 2; ++i)
  {
    Win32ScenePlatformController controller(NULL, scales[i]);
    ButtonNode newButton(ButtonProps().text("New"));
    ButtonNode saveButton(ButtonProps().text("Save As..."));
    short newWidth = 0, saveWidth = 0, repeatedWidth = 0;
    LOKA_VERIFY(controller.queryNaturalWidth(&newButton, newWidth));
    LOKA_VERIFY(controller.queryNaturalWidth(&saveButton, saveWidth));
    LOKA_VERIFY(controller.queryNaturalWidth(&newButton, repeatedWidth));
    LOKA_VERIFY(newWidth == repeatedWidth);
    LOKA_VERIFY(newWidth == expectedPushButtonWidth(controller, L"New"));
    LOKA_VERIFY(saveWidth == expectedPushButtonWidth(controller, L"Save As..."));
    LOKA_VERIFY(newWidth < saveWidth);
    const wchar_t wideLabel[] = {0x65e5, 0x672c, 0x8a9e, 0};
    ButtonNode wideButton(ButtonProps().text(loka::core::String(
        loka::win32::CreateWin32StringFromUtf16(wideLabel, 3))));
    short wideWidth = 0;
    LOKA_VERIFY(controller.queryNaturalWidth(&wideButton, wideWidth));
    LOKA_VERIFY(wideWidth == expectedPushButtonWidth(controller, wideLabel));
    ButtonNode missing((ButtonProps()));
    StackNode other((StackProps()));
    short refused = 13;
    LOKA_VERIFY(!controller.queryNaturalWidth(&missing, refused));
    LOKA_VERIFY(!controller.queryNaturalWidth(&other, refused));
    LOKA_VERIFY(!controller.queryNaturalWidth(0, refused));
    LOKA_VERIFY(refused == 13);
    ButtonNode huge(ButtonProps().text(loka::core::String(std::string(32768, 'W'))));
    LOKA_VERIFY(!controller.queryNaturalWidth(&huge, refused));
    LOKA_VERIFY(refused == 13);
  }
}

void testWin32IntrinsicImageNaturalWidthAnswer()
{
  using namespace loka::app;
  using namespace loka::core;
  const loka::win32::Win32DisplayScale scales[] = {
      loka::win32::Win32DisplayScale(96), loka::win32::Win32DisplayScale(144, loka::win32::DefaultRailMetrics())};
  for (int i = 0; i < 2; ++i)
  {
    Win32ScenePlatformController controller(NULL, scales[i]);
    int native = 0;
    MutableState<resource::Image> imageState(resource::Image::FromNative(&native, 640, 10, 0, 0));
    ImageViewProps props;
    props.image(&imageState).attr(ImageViewAttr().sizePolicy(IMAGE_VIEW_SIZE_INTRINSIC));
    ImageViewNode image(props);
    short width = 123;
    LOKA_VERIFY(controller.queryNaturalWidth(&image, width));
    // ResolveImageLayoutWidth is private to its translation unit; use its public conversion.
    LOKA_VERIFY(width == scales[i].intrinsicPixelsToLu(640));
    const int overflowPixels = static_cast<int>(SHRT_MAX) * 4;
    LOKA_VERIFY(scales[i].intrinsicPixelsToLu(overflowPixels) > SHRT_MAX);
    for (unsigned refusal = 0; refusal < 7; ++refusal)
    {
      image.props = props;
      imageState.set(resource::Image::FromNative(&native, 640, 10, 0, 0));
      switch (refusal)
      {
      case 0:
        image.props.width_ = 100;
        break;
      case 1:
        image.props.attr_.sizePolicyValue_ = IMAGE_VIEW_SIZE_AUTO;
        break;
      case 2:
        image.props.attr_.sizePolicyValue_ = IMAGE_VIEW_SIZE_FILL_PARENT;
        break;
      case 3:
        imageState.set(resource::Image::Empty());
        break;
      case 4:
        imageState.set(resource::Image::FromNative(&native, 0, 10, 0, 0));
        break;
      case 5:
        imageState.set(resource::Image::FromNative(&native, overflowPixels, 10, 0, 0));
        break;
      case 6:
        image.props.image_ = 0;
        break;
      }
      width = 123;
      LOKA_VERIFY(!controller.queryNaturalWidth(&image, width));
      LOKA_VERIFY(width == 123);
    }
  }
}

void testWin32TextNaturalWidthAnswer()
{
  using namespace loka::app;
  using namespace loka::core;
  const loka::win32::Win32DisplayScale scales[] = {
      loka::win32::Win32DisplayScale(96), loka::win32::Win32DisplayScale(144, loka::win32::DefaultRailMetrics())};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
  {
    HWND root = CreateWindowExW(
        0, L"STATIC", L"natural-text", WS_POPUP, 0, 0, 640, 160, NULL, NULL, GetModuleHandleW(NULL), NULL);
    LOKA_VERIFY(root != NULL);
    {
      Win32ScenePlatformController controller(root, scales[scaleIndex]);
      RegisterWin32BuiltInSupport(controller);
      const TextStyle styles[] = {TextStyle(), SmallLabel, SmallLabel + Bold, FontSize<24>() + Italic};
      for (int i = 0; i < 4; ++i)
      {
        TextProps props("Width & italic Wj");
        props.textStyle_ = styles[i];
        TextNode text(props);
        HWND child = projectFontText(controller, root, text);
        LOGFONTW descriptor;
        const HFONT nativeFont = readFont(child, descriptor);
        RECT measured = {0, 0, 0, 0};
        // Invoke the context's own CALCRECT path with the font installed on its STATIC.
        LOKA_VERIFY(Win32TextContext::measureTextRect(child, nativeFont, L"Width & italic Wj", measured));
        short width = 0;
        LOKA_VERIFY(controller.queryNaturalWidth(&text, width));
        LOKA_VERIFY(width == scales[scaleIndex].measurementToLu(measured.right - measured.left));
        // Independent GDI oracle keeps a shared-helper mutation discriminating.
        HDC dc = GetDC(child);
        LOKA_VERIFY(dc != NULL);
        HGDIOBJ previous = SelectObject(dc, nativeFont);
        LOKA_VERIFY(previous && previous != HGDI_ERROR);
        RECT expected = {0, 0, 0, 0};
        LOKA_VERIFY(DrawTextW(dc, L"Width & italic Wj", -1, &expected, DT_LEFT | DT_NOPREFIX | DT_CALCRECT));
        LOKA_VERIFY(SelectObject(dc, previous) != NULL);
        LOKA_VERIFY(ReleaseDC(child, dc));
        LOKA_VERIFY(measured.right == expected.right && measured.bottom == expected.bottom);
      }
      PushStateTracker tracker;
      MutableState<String> label(String::Literal("i"));
      MutableState<TextStyle> style((FontSize<12>()));
      tracker.addState(&label);
      tracker.addState(&style);
      TextProps liveProps;
      liveProps.text(&label);
      liveProps.textStyleState_ = &style;
      TextNode live(liveProps);
      short before = 0, after = 0;
      LOKA_VERIFY(controller.queryNaturalWidth(&live, before));
      {
        StateTrackerGuard guard(&tracker);
        label.set(String::Literal("A much longer current label"));
        style.set(FontSize<24>() + Bold);
      }
      LOKA_VERIFY(controller.queryNaturalWidth(&live, after));
      LOKA_VERIFY(after > before);
      TextProps snapshotProps("A much longer current label");
      snapshotProps.textStyle_ = FontSize<24>() + Bold;
      TextNode snapshot(snapshotProps);
      short snapshotWidth = 0;
      LOKA_VERIFY(controller.queryNaturalWidth(&snapshot, snapshotWidth));
      LOKA_VERIFY(after == snapshotWidth);
      const wchar_t wideLabel[] = {0x65e5, 0x672c, 0x8a9e, 0};
      TextNode unicode(TextProps(String(loka::win32::CreateWin32StringFromUtf16(wideLabel, 3))));
      RECT unicodeRect = {0, 0, 0, 0};
      LOKA_VERIFY(Win32TextContext::measureTextRect(root, controller.displayFont(), wideLabel, unicodeRect));
      LOKA_VERIFY(controller.queryNaturalWidth(&unicode, after));
      LOKA_VERIFY(after == scales[scaleIndex].measurementToLu(unicodeRect.right));
      TextNode missing((TextProps()));
      const String refusedString(Managed<loka::platform::String>::Wrap(new RefusingNaturalWidthString()));
      TextNode conversionFailure((TextProps(refusedString)));
      short refused = 13;
      LOKA_VERIFY(!controller.queryNaturalWidth(&conversionFailure, refused));
      LOKA_VERIFY(refused == 13);
      LOKA_VERIFY(!controller.queryNaturalWidth(&missing, refused));
      const char *declines[] = {"", "one\ntwo", "one\rtwo", "\n", "\r"};
      for (int i = 0; i < 5; ++i)
      {
        TextNode text((TextProps(declines[i])));
        LOKA_VERIFY(!controller.queryNaturalWidth(&text, refused));
        LOKA_VERIFY(refused == 13);
      }
      TextNode huge(TextProps(String(std::string(32768, 'W'))));
      LOKA_VERIFY(!controller.queryNaturalWidth(&huge, refused));
      LOKA_VERIFY(refused == 13);
      // A null font keeps the height path's historical meaning: the DC's own font.
      RECT noFont = {0, 0, 0, 0};
      LOKA_VERIFY(Win32TextContext::measureTextRect(root, NULL, L"text", noFont));
      LOKA_VERIFY(noFont.right > 0);
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
  HWND refusedRoot = CreateWindowExW(
      0, L"STATIC", L"refused-text", WS_POPUP, 0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
  LOKA_VERIFY(refusedRoot != NULL);
  Win32ScenePlatformController refusedController(refusedRoot,
                                                 loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
  LOKA_VERIFY(DestroyWindow(refusedRoot));
  LOKA_VERIFY(GetDC(refusedRoot) == NULL);
  TextNode text(TextProps("No DC"));
  short refused = 13;
  LOKA_VERIFY(!refusedController.queryNaturalWidth(&text, refused));
  LOKA_VERIFY(refused == 13);
}

void testWin32SmallLabelNaturalWidth()
{
  using namespace loka::app;
  Win32ScenePlatformController controller(NULL, loka::win32::Win32DisplayScale(96, loka::app::RailMetrics()));
  TextNode body(TextProps("Alignment sample WWW"));
  TextProps captionProps("Alignment sample WWW");
  captionProps.textStyle_ = SmallLabel;
  TextNode caption(captionProps);
  short bodyWidth = 0, captionWidth = 0;
  LOKA_VERIFY(controller.queryNaturalWidth(&body, bodyWidth));
  LOKA_VERIFY(controller.queryNaturalWidth(&caption, captionWidth));
  // Acceptance pin requires the SmallLabel font-role row from #1285.
  LOKA_VERIFY(captionWidth < bodyWidth);
}

void testWin32RibbonGroupNaturalWidthTraversal()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  const loka::win32::Win32DisplayScale scales[] = {
      loka::win32::Win32DisplayScale(96), loka::win32::Win32DisplayScale(144, loka::win32::DefaultRailMetrics())};
  const char *captions[] = {"Alignment", "Alignment and paragraph positioning controls"};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    for (int captionIndex = 0; captionIndex < 2; ++captionIndex)
    {
      const loka::win32::Win32DisplayScale &scale = scales[scaleIndex];
      HWND root = CreateWindowExW(0,
                                  L"STATIC",
                                  L"natural-group",
                                  WS_POPUP,
                                  0,
                                  0,
                                  scale.clientLengthToNative(960).px,
                                  scale.clientLengthToNative(160).px,
                                  NULL,
                                  NULL,
                                  GetModuleHandleW(NULL),
                                  NULL);
      LOKA_VERIFY(root != NULL);
      {
        Win32ScenePlatformController controller(root, scale);
        NodeDefinitionBase *definition =
            (RibbonControl().testId("groupRibbon")
             << RibbonItem("Open")
             << (RibbonGroup(captions[captionIndex]) << RibbonItem("L") << RibbonItem("C") << RibbonItem("R"))
             << RibbonItem("Close"))
                .clone();
        LOKA_VERIFY(definition != 0);
        Scene scene(definition);
        LOKA_VERIFY(scene.mount(&controller));
        SceneTestAccess::updateAttached(scene, true);
        controller.relayout(960, 160);
        ButtonNode *buttons[5] = {0, 0, 0, 0, 0};
        short widths[5] = {0, 0, 0, 0, 0};
        FlowError error;
        for (int i = 0; i < 5; ++i)
        {
          LOKA_VERIFY(
              ResolveSelector(&scene, WithinAnchor("groupRibbon").descendant<ButtonNode>(i + 1), buttons[i], error)
              == FLOW_STEP_SUCCEEDED);
          LOKA_VERIFY(controller.queryNaturalWidth(buttons[i], widths[i]));
        }
        TextNode *caption = 0;
        LOKA_VERIFY(ResolveSelector(&scene, WithinAnchor("groupRibbon").descendant<TextNode>(1), caption, error)
                    == FLOW_STEP_SUCCEEDED);
        short captionWidth = 0;
        LOKA_VERIFY(controller.queryNaturalWidth(caption, captionWidth));
        const int gap = Win32SceneLayout::kGap;
        const int itemsWidth = widths[1] + gap + widths[2] + gap + widths[3];
        const int groupWidth = itemsWidth > captionWidth ? itemsWidth : captionWidth;
        if (captionIndex == 1)
          LOKA_VERIFY(captionWidth > itemsWidth);
        const int groupX = Win32SceneLayout::kRootInset + widths[0] + gap;
        verifyButtonSeat(buttons[0], root, scale, Win32SceneLayout::kRootInset, widths[0]);
        int x = groupX + RibbonGroupNode::kFrameInset;
        for (int i = 1; i < 4; ++i)
        {
          verifyButtonSeat(buttons[i], root, scale, x, widths[i]);
          x += widths[i] + gap;
        }
        verifyButtonSeat(
            buttons[4], root, scale, groupX + groupWidth + 2 * RibbonGroupNode::kFrameInset + gap, widths[4]);
        // STATIC spans the inset content seat; inspect the real context's native projection.
        HWND captionWindow = NULL;
        for (HWND child = GetWindow(root, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
          wchar_t className[32];
          LOKA_VERIFY(GetClassNameW(child, className, 32));
          if (std::wcscmp(className, L"Static") == 0 || std::wcscmp(className, L"STATIC") == 0)
            captionWindow = child;
        }
        LOKA_VERIFY(captionWindow != NULL);
        const RECT seat = childRectInParent(captionWindow, root);
        LOKA_VERIFY(seat.left == scale.projectEdge(groupX + RibbonGroupNode::kFrameInset));
        LOKA_VERIFY(seat.right == scale.projectEdge(groupX + RibbonGroupNode::kFrameInset + groupWidth));
        SceneTestAccess::unmount(scene);
        controller.drainNativeRetirements();
      }
      LOKA_VERIFY(DestroyWindow(root));
    }
}

void testWin32RibbonNaturalWidthTraversal()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  const loka::win32::Win32DisplayScale scales[] = {
      loka::win32::Win32DisplayScale(96),
      loka::win32::Win32DisplayScale(144, loka::win32::DefaultRailMetrics())};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
  {
    const loka::win32::Win32DisplayScale &scale = scales[scaleIndex];
    HWND root = CreateWindowExW(0, L"STATIC", L"natural-ribbon", WS_POPUP, 0, 0,
                                scale.clientLengthToNative(640).px, scale.clientLengthToNative(160).px,
                                NULL, NULL, GetModuleHandleW(NULL), NULL);
    LOKA_VERIFY(root != NULL);
    {
      Win32ScenePlatformController controller(root, scale);
      NodeDefinitionBase *definition =
          (Column() << (RibbonControl().testId("naturalRibbon")
                        << RibbonItem("New") << RibbonItem("Save As...") << RibbonItem("Fixed").width(120))
                    << (Row().testId("sharedRow") << Button("New") << Button("Save As..."))).clone();
      LOKA_VERIFY(definition != 0);
      Scene scene(definition);
      LOKA_VERIFY(scene.mount(&controller));
      SceneTestAccess::updateAttached(scene, true);
      controller.relayout(640, 160);
      LOKA_VERIFY(countChildWindows(root) == 5);
      const int widths[] = {expectedPushButtonWidth(controller, L"New"),
                            expectedPushButtonWidth(controller, L"Save As..."), 120};
      const int gap = Win32SceneLayout::kGap;
      int x = Win32SceneLayout::kRootInset;
      for (int i = 0; i < 3; ++i)
      {
        ButtonNode *button = 0;
        FlowError error;
        LOKA_VERIFY(ResolveSelector(&scene, WithinAnchor("naturalRibbon").descendant<ButtonNode>(i + 1),
                                    button, error) == FLOW_STEP_SUCCEEDED);
        LOKA_VERIFY(button != 0);
        Win32ButtonContext *context = static_cast<Win32ButtonContext *>(button->getContext());
        LOKA_VERIFY(context != 0);
        LOKA_VERIFY(reinterpret_cast<HFONT>(SendMessageW(context->hwnd(), WM_GETFONT, 0, 0))
                    == controller.displayFont());
        verifyButtonSeat(button, root, scale, x, widths[i]);
        x += widths[i] + gap;
      }
      // Different titles remain equal seats in an ordinary Row.
      const int sharedWidth = (640 - 2 * Win32SceneLayout::kRootInset - gap) / 2;
      LOKA_VERIFY(2 * sharedWidth + gap == 640 - 2 * Win32SceneLayout::kRootInset);
      for (int i = 0; i < 2; ++i)
      {
        ButtonNode *button = 0;
        FlowError error;
        LOKA_VERIFY(ResolveSelector(&scene, WithinAnchor("sharedRow").descendant<ButtonNode>(i + 1),
                                    button, error) == FLOW_STEP_SUCCEEDED);
        LOKA_VERIFY(button != 0);
        verifyButtonSeat(button, root, scale, Win32SceneLayout::kRootInset + i * (sharedWidth + gap), sharedWidth);
      }
      SceneTestAccess::unmount(scene);
      controller.drainNativeRetirements();
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
}

void testWin32NaturalWidthDirectRow()
{
  using namespace loka::app;
  const loka::win32::Win32DisplayScale scale(144, loka::win32::DefaultRailMetrics());
  HWND root = CreateWindowExW(0, L"STATIC", L"natural-direct", WS_POPUP, 0, 0,
                              scale.clientLengthToNative(640).px, scale.clientLengthToNative(160).px,
                              NULL, NULL, GetModuleHandleW(NULL), NULL);
  LOKA_VERIFY(root != NULL);
  {
    Win32ScenePlatformController controller(root, scale);
    StackProps props;
    props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
    DirectNaturalWidthRow row(props);
    ButtonNode *first = new ButtonNode(ButtonProps().text("New"));
    ButtonNode *second = new ButtonNode(ButtonProps().text("Save As..."));
    row.addChild(first);
    row.addChild(second);
    controller.onChange(&row, scene::NODE_DIRTY_NONE, false);
    controller.relayout(640, 160);
    const int firstWidth = expectedPushButtonWidth(controller, L"New");
    const int secondWidth = expectedPushButtonWidth(controller, L"Save As...");
    verifyButtonSeat(first, root, scale, Win32SceneLayout::kRootInset, firstWidth);
    verifyButtonSeat(
        second, root, scale, Win32SceneLayout::kRootInset + firstWidth + Win32SceneLayout::kGap, secondWidth);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32AttributedTextInksAndAdditivity()
{
  using namespace loka::app;
  HWND root = attributedHost();
  {
    Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96, RailMetrics()));
    HDC source = GetDC(root);
    LOKA_VERIFY(source != NULL);
    HDC dc = CreateCompatibleDC(source);
    HBITMAP bitmap = CreateCompatibleBitmap(source, 400, 60);
    LOKA_VERIFY(dc && bitmap);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    LOKA_VERIFY(previous && previous != HGDI_ERROR);
    const RECT rect = {0, 0, 400, 60};
    LOKA_VERIFY(FillRect(dc, &rect, GetSysColorBrush(COLOR_BTNFACE)));
    const COLORREF sentinel = RGB(1, 2, 3);
    SetTextColor(dc, sentinel);
    Win32AttributedTextTable table;
    const TextStyle font = FontSize<24>();
    const AttributedString runs = Styled("MMMM", font + Red) + Styled("MMMM", font) + Styled("MMMM", font + Blue);
    LOKA_VERIFY(table.build(runs, BlockStyle(), 400, dc, controller));
    LOKA_VERIFY(table.draw(dc, rect, BlockStyle()));
    LOKA_VERIFY(GetTextColor(dc) == sentinel);
    const int runWidth = measureSpan(root, controller.textFont(font), L"MMMM", 4);
    const COLORREF inks[] = {RGB(0xC4, 0x2B, 0x1C), GetSysColor(COLOR_BTNTEXT), RGB(0x0F, 0x6C, 0xBD)};
    for (int run = 0; run < 3; ++run)
    {
      int count = 0;
      for (int y = 0; y < table.height(); ++y)
        for (int x = run * runWidth; x < (run + 1) * runWidth; ++x)
          if (GetPixel(dc, x, y) == inks[run])
            ++count;
      std::printf("AttributedText run %d ink pixels=%d\n", run, count);
      LOKA_VERIFY(count > 0);
    }
    Win32AttributedTextTable split, uniform;
    const AttributedString splitValue = Styled("AV", Red) + Styled("AWAY", TextStyle());
    const AttributedString uniformValue = Styled("AVAWAY", TextStyle());
    LOKA_VERIFY(split.build(splitValue, BlockStyle(), 400, dc, controller));
    LOKA_VERIFY(uniform.build(uniformValue, BlockStyle(), 400, dc, controller));
    std::printf("GDI color-only additivity: split=%d uniform=%d\n", split.width(), uniform.width());
    LOKA_VERIFY(split.width() == uniform.width());
    const int narrow = measureSpan(root, controller.displayFont(), L"AVA", 3);
    const BlockStyle wrap = BlockStyle().wrap(TEXT_WRAP_CHAR);
    LOKA_VERIFY(split.build(splitValue, wrap, narrow, dc, controller));
    LOKA_VERIFY(uniform.build(uniformValue, wrap, narrow, dc, controller));
    std::wstring splitLines, uniformLines;
    LOKA_VERIFY(split.joinLines(splitLines));
    LOKA_VERIFY(uniform.joinLines(uniformLines));
    LOKA_VERIFY(splitLines.find(L'\n') != std::wstring::npos);
    LOKA_VERIFY(splitLines == uniformLines);
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(root, source);
  }
  DestroyWindow(root);
}

void testWin32RibbonGroupEdgesEnsureGeometryAndLifecycle()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  const int dpis[] = {96, 144};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
  {
    const loka::win32::Win32DisplayScale scale(dpis[scaleIndex], RailMetrics());
    HWND root = CreateWindowExW(
        0, L"STATIC", L"ribbon-edge-host", WS_POPUP, 0, 0, 600, 300, NULL, NULL, GetModuleHandleW(NULL), NULL);
    LOKA_VERIFY(root != NULL);
    {
      Win32ScenePlatformController controller(root, scale);
      RibbonGroupNode group((RibbonGroupProps()));
      StackNode *content = new StackNode(StackProps(STACK_AXIS_COLUMN));
      ButtonNode *button = new ButtonNode(ButtonProps().text("New"));
      content->addChild(button);
      content->addChild(new TextNode(TextProps("File")));
      group.addChild(content);
      const RibbonGroupEdge roles[] = {RIBBON_GROUP_EDGE_LEFT, RIBBON_GROUP_EDGE_RIGHT, RIBBON_GROUP_EDGE_BOTTOM};
      RibbonGroupEdgeNode *edges[3];
      Win32RibbonGroupEdgeContext *contexts[3] = {0, 0, 0};
      HWND windows[3] = {0, 0, 0};
      for (int i = 0; i < 3; ++i)
      {
        edges[i] = new RibbonGroupEdgeNode(RibbonGroupEdgeProps(roles[i]));
        group.addChild(edges[i]);
      }
      NotifySubtreeNodeAttached(&group);
      controller.onChange(&group, NODE_DIRTY_NONE, false);
      RefusedNodeHandler foreign(NodeTypeToken<RibbonGroupEdgeNode>());
      LOKA_VERIFY(!controller.registerNodeHandler(&foreign));
      for (int pass = 0; pass < 2; ++pass)
      {
        const int width = 320 + pass * 40;
        controller.relayout(width, 160);
        int edgeCount = 0;
        for (HWND child = GetWindow(root, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        {
          wchar_t name[64];
          LOKA_VERIFY(GetClassNameW(child, name, 64));
          if (std::wcscmp(name, L"LOKA_RIBBON_GROUP_EDGE") != 0)
            continue;
          ++edgeCount;
          for (int i = 0; i < 3; ++i)
            if (reinterpret_cast<void *>(GetWindowLongPtr(child, GWLP_USERDATA)) == edges[i]->getContext())
            {
              if (pass == 0)
                windows[i] = child;
              else
                LOKA_VERIFY(windows[i] == child);
            }
        }
        LOKA_VERIFY(edgeCount == 3);
        RECT frames[3];
        for (int i = 0; i < 3; ++i)
        {
          LOKA_VERIFY(windows[i] && IsWindow(windows[i]));
          LOKA_VERIFY((GetWindowLongPtr(windows[i], GWL_STYLE) & WS_VISIBLE) != 0);
          if (pass == 0)
            contexts[i] = static_cast<Win32RibbonGroupEdgeContext *>(edges[i]->getContext());
          LOKA_VERIFY(contexts[i] && edges[i]->getContext() == contexts[i]);
          frames[i] = childRectInParent(windows[i], root);
          RECT client;
          LOKA_VERIFY(GetClientRect(windows[i], &client));
          LOKA_VERIFY(client.right == frames[i].right - frames[i].left);
          LOKA_VERIFY(client.bottom == frames[i].bottom - frames[i].top);
          const PaintQuery eligible = {Win32RetirableContext::paintScope(), PLACEMENT_ELIGIBLE};
          const PaintQuery pending = {eligible.scope, PLACEMENT_PENDING};
          LOKA_VERIFY(contexts[i]->queryPaintDamage(eligible).kind == PAINT_ANSWER_NATIVE_SCHEDULED);
          LOKA_VERIFY(contexts[i]->queryPaintDamage(pending).reason == PAINT_REFUSED_PLACEMENT_UNSETTLED);
        }
        const int inset = Win32SceneLayout::kRootInset;
        LOKA_VERIFY(frames[0].left == scale.projectEdge(inset));
        LOKA_VERIFY(frames[0].top == scale.projectEdge(inset));
        LOKA_VERIFY(frames[0].right == scale.projectEdge(inset + 1));
        LOKA_VERIFY(frames[1].right == scale.projectEdge(width - inset));
        LOKA_VERIFY(frames[1].left == scale.projectEdge(width - inset - 1));
        LOKA_VERIFY(frames[1].top == frames[0].top);
        LOKA_VERIFY(frames[0].bottom == frames[2].top && frames[1].bottom == frames[2].top);
        LOKA_VERIFY(frames[2].left == frames[0].left && frames[2].right == frames[1].right);
        LOKA_VERIFY(frames[2].bottom > frames[2].top);
        if (scaleIndex == 0)
          LOKA_VERIFY(frames[2].bottom - frames[2].top == 1);
      }
      NotifySubtreeNodeDetached(&group);
      LifecycleFactTestAccess::DeliverFacts(&group);
      for (int i = 0; i < 3; ++i)
      {
        LOKA_VERIFY(edges[i]->getContext() == contexts[i] && IsWindow(windows[i]));
        LOKA_VERIFY((GetWindowLongPtr(windows[i], GWL_STYLE) & WS_VISIBLE) == 0);
      }
      NotifySubtreeNodeAttached(&group);
      LifecycleFactTestAccess::DeliverFacts(&group);
      for (int i = 0; i < 3; ++i)
        LOKA_VERIFY((GetWindowLongPtr(windows[i], GWL_STYLE) & WS_VISIBLE) != 0);
      LifecycleFactTestAccess::MarkSubtreeRetired(&group);
      controller.releaseNodeContexts(&group);
      for (int i = 0; i < 3; ++i)
      {
        LOKA_VERIFY(edges[i]->getContext() == 0 && IsWindow(windows[i]));
        LOKA_VERIFY((GetWindowLongPtr(windows[i], GWL_STYLE) & WS_VISIBLE) == 0);
        LOKA_VERIFY(GetWindowLongPtr(windows[i], GWLP_USERDATA) == 0);
      }
      controller.drainNativeRetirements();
      for (int i = 0; i < 3; ++i)
        LOKA_VERIFY(!IsWindow(windows[i]));
      controller.onChange(0, NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
}

namespace
{
  int expectedPushButtonHeightPixels(const Win32ScenePlatformController &controller)
  {
    HDC dc = GetDC(NULL);
    LOKA_VERIFY(dc != NULL);
    LOKA_VERIFY(controller.displayFont() != NULL);
    HGDIOBJ previous = SelectObject(dc, controller.displayFont());
    LOKA_VERIFY(previous && previous != HGDI_ERROR);
    TEXTMETRICW text;
    LOKA_VERIFY(GetTextMetricsW(dc, &text));
    LOKA_VERIFY(SelectObject(dc, previous) != NULL);
    LOKA_VERIFY(ReleaseDC(NULL, dc));
    // 4 DIPs per side at dpi only, written out independently of the rail's projection.
    return static_cast<int>(text.tmHeight) + 2 * MulDiv(4, static_cast<int>(controller.displayScale().dpi()), 96);
  }

  BOOL WINAPI declineButtonTextMetrics(HDC, LPTEXTMETRICW)
  {
    return FALSE;
  }

  HWND buttonHeightHost(const loka::win32::Win32DisplayScale &scale)
  {
    HWND root = CreateWindowExW(0, L"STATIC", L"button-height", WS_POPUP, 0, 0,
                               scale.clientLengthToNative(640).px, scale.clientLengthToNative(160).px,
                               NULL, NULL, GetModuleHandleW(NULL), NULL);
    LOKA_VERIFY(root != NULL);
    return root;
  }

  void verifyButtonCenteredRow(Win32ScenePlatformController &controller, HWND root, bool direct, bool declined)
  {
    using namespace loka::app;
    StackProps props(STACK_AXIS_ROW);
    props.alignVertical(VERTICAL_ALIGNMENT_CENTER);
    StackNode *row = direct ? new DirectNaturalWidthRow(props) : new StackNode(props);
    ButtonNode *button = new ButtonNode(ButtonProps().text("Button"));
    EditTextNode *field = new EditTextNode((EditTextProps()));
    row->addChild(button);
    row->addChild(field);
    controller.onChange(row, scene::NODE_DIRTY_NONE, false);
    controller.relayout(640, 160);
    Win32ButtonContext *buttonContext = static_cast<Win32ButtonContext *>(button->getContext());
    Win32EditTextContext *fieldContext = static_cast<Win32EditTextContext *>(field->getContext());
    LOKA_VERIFY(buttonContext && fieldContext);
    const RECT buttonFrame = childRectInParent(buttonContext->hwnd(), root);
    const RECT fieldFrame = childRectInParent(fieldContext->hwnd(), root);
    const loka::win32::Win32DisplayScale &scale = controller.displayScale();
    LOKA_VERIFY(std::abs((buttonFrame.top + buttonFrame.bottom) - (fieldFrame.top + fieldFrame.bottom))
                <= 2 * scale.nativeLength(0, 1).px);
    if (declined)
    {
      const int top = Win32SceneLayout::kRootInset;
      LOKA_VERIFY(buttonFrame.top == scale.projectEdge(top));
      LOKA_VERIFY(buttonFrame.bottom == scale.projectEdge(top + layout::FallbackControlMetrics::kButtonHeight));
    }
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
    delete row;
  }
}

void testWin32ButtonNativeHeightColumn()
{
  using namespace loka::app;
  // 96 dpi with the default rail metrics is the production configuration; there
  // spaceScale 5/4 would widen a 4-DIP inset to 5 px if it leaked into the margin.
  const loka::win32::Win32DisplayScale scales[] = {
      loka::win32::Win32DisplayScale(96), loka::win32::Win32DisplayScale(96, loka::win32::DefaultRailMetrics()),
      loka::win32::Win32DisplayScale(144, loka::win32::DefaultRailMetrics())};
  for (int i = 0; i < 3; ++i)
  {
    const loka::win32::Win32DisplayScale &scale = scales[i];
    HWND root = buttonHeightHost(scale);
    {
      Win32ScenePlatformController controller(root, scale);
      StackNode column((StackProps(STACK_AXIS_COLUMN)));
      ButtonNode *first = new ButtonNode(ButtonProps().text("First"));
      ButtonNode *second = new ButtonNode(ButtonProps().text("Second"));
      column.addChild(first);
      column.addChild(second);
      controller.onChange(&column, scene::NODE_DIRTY_NONE, false);
      controller.relayout(640, 160);
      const int nativeHeight = expectedPushButtonHeightPixels(controller);
      const int height = scale.measurementToLu(nativeHeight);
      LOKA_VERIFY(controller.rowLayoutMetrics().buttonHeight == height);
      const RECT frame = childRectInParent(static_cast<Win32ButtonContext *>(first->getContext())->hwnd(), root);
      const RECT next = childRectInParent(static_cast<Win32ButtonContext *>(second->getContext())->hwnd(), root);
      const int top = Win32SceneLayout::kRootInset;
      LOKA_VERIFY(frame.top == scale.projectEdge(top));
      LOKA_VERIFY(frame.bottom == scale.projectEdge(top + height));
      LOKA_VERIFY(std::abs(frame.bottom - frame.top - nativeHeight) <= scale.nativeLength(0, 1).px);
      LOKA_VERIFY(next.top == scale.projectEdge(top + height + Win32SceneLayout::kGap));
      controller.onChange(0, scene::NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
}

void testWin32ButtonNativeHeightCenteredRow()
{
  const loka::win32::Win32DisplayScale scales[] = {
      loka::win32::Win32DisplayScale(96), loka::win32::Win32DisplayScale(96, loka::win32::DefaultRailMetrics()),
      loka::win32::Win32DisplayScale(144, loka::win32::DefaultRailMetrics())};
  for (int i = 0; i < 3; ++i)
    for (int direct = 0; direct < 2; ++direct)
    {
      HWND root = buttonHeightHost(scales[i]);
      {
        Win32ScenePlatformController controller(root, scales[i]);
        verifyButtonCenteredRow(controller, root, direct != 0, false);
      }
      LOKA_VERIFY(DestroyWindow(root));
    }
}

void testWin32ButtonDeclinedHeightKeepsRowAligned()
{
  using namespace loka::app;
  const loka::win32::Win32DisplayScale scale(96);
  for (int direct = 0; direct < 2; ++direct)
  {
    HWND root = buttonHeightHost(scale);
    Win32ScenePlatformController *controller = NULL;
    HDC probe = GetDC(root);
    LOKA_VERIFY(probe != NULL);
    TEXTMETRICW text;
    LOKA_VERIFY(GetTextMetricsW(probe, &text));
    {
      // Refuse only construction: subsequent GDI queries must answer natively.
      Win32ImportRefusal refusal("gdi32.dll", "GetTextMetricsW",
                                 reinterpret_cast<ULONG_PTR>(&declineButtonTextMetrics));
      LOKA_VERIFY(!GetTextMetricsW(probe, &text));
      controller = new Win32ScenePlatformController(root, scale);
    }
    LOKA_VERIFY(GetTextMetricsW(probe, &text));
    LOKA_VERIFY(ReleaseDC(root, probe));
    const int nativeHeight = expectedPushButtonHeightPixels(*controller);
    LOKA_VERIFY(scale.measurementToLu(nativeHeight) != layout::FallbackControlMetrics::kButtonHeight);
    LOKA_VERIFY(controller->rowLayoutMetrics().buttonHeight == layout::FallbackControlMetrics::kButtonHeight);
    LOKA_VERIFY(Win32SceneLayout::spacing().horizontalGap() == Win32SceneLayout::kGap);
    verifyButtonCenteredRow(*controller, root, direct != 0, true);
    // A DPI change must not split the cached Stack copy from the Button reader.
    controller->updateDisplayScale(loka::win32::Win32DisplayScale(144));
    LOKA_VERIFY(controller->rowLayoutMetrics().buttonHeight == layout::FallbackControlMetrics::kButtonHeight);
    LOKA_VERIFY(Win32SceneLayout::spacing().horizontalGap() == Win32SceneLayout::kGap);
    verifyButtonCenteredRow(*controller, root, direct != 0, true);
    delete controller;
    LOKA_VERIFY(DestroyWindow(root));
  }
}

void testWin32ContainerGapLeafExits()
{
  using namespace loka::app;
  const RailMetrics metrics[] = {RailMetrics(), loka::win32::DefaultRailMetrics()};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    for (int leaf = 0; leaf < 8; ++leaf)
    {
      const loka::win32::Win32DisplayScale scale(96, metrics[scaleIndex]);
      HWND root = buttonHeightHost(scale);
      {
        Win32ScenePlatformController controller(root, scale);
        // Fragment gives Cell an unspecified height, exercising its natural exit.
        FragmentNode content((FragmentProps()));
        scene::Node *first = 0;
        switch (leaf)
        {
        case 0:
          first = new TextNode(TextProps("Text"));
          break;
        case 1:
          first = new ButtonNode(ButtonProps().text("Button"));
          break;
        case 2:
          first = new EditTextNode((EditTextProps()));
          break;
        case 3:
          first = new PopupMenuNode((PopupMenuProps()));
          break;
        case 4:
          first = new ImageViewNode((ImageViewProps()));
          break;
        case 5:
          first = new CellNode(CellProps().text("Cell"));
          break;
        case 6:
          first = new AttributedTextNode(AttributedTextProps(Styled("Styled", Bold)));
          break;
        case 7:
          first = new RectSurfaceNode(RectSurfaceProps().size(80, 30));
          break;
        }
        LOKA_VERIFY(first != 0);
        TextNode *next = new TextNode(TextProps("Following"));
        content.addChild(first);
        content.addChild(next);
        controller.onChange(&content, scene::NODE_DIRTY_NONE, false);
        // Root padding consumes the height, leaving an unspecified seat.
        controller.relayout(640, 2 * Win32SceneLayout::kRootInset);
        LOKA_VERIFY(first->getContext() && next->getContext());
        const RECT frame = childRectInParent(static_cast<Win32RetirableContext *>(first->getContext())->hwnd(), root);
        const RECT following = childRectInParent(static_cast<Win32TextContext *>(next->getContext())->hwnd(), root);
        LOKA_VERIFY(frame.left == scale.projectEdge(Win32SceneLayout::kRootInset));
        LOKA_VERIFY(frame.top == scale.projectEdge(Win32SceneLayout::kRootInset));
        // At these scales projection is injective; recover the actual painted
        // bottom independently of the leaf's returned layout advance.
        int bottom = Win32SceneLayout::kRootInset;
        if (leaf == 3)
        {
          // Combo-box native sizing includes the drop list; its logical seat
          // is the closed control height, as in the existing popup layout.
          bottom += layout::FallbackControlMetrics::kPopupMenuHeight;
        }
        else
        {
          while (scale.projectEdge(bottom) < frame.bottom)
            ++bottom;
          LOKA_VERIFY(frame.bottom == scale.projectEdge(bottom));
        }
        LOKA_VERIFY(following.top == scale.projectEdge(bottom + Win32SceneLayout::kGap));
        controller.onChange(0, scene::NODE_DIRTY_NONE, false);
      }
      LOKA_VERIFY(DestroyWindow(root));
    }
}

void testWin32ContainerGapColumnAndPassThrough()
{
  using namespace loka::app;
  const RailMetrics metrics[] = {RailMetrics(), loka::win32::DefaultRailMetrics()};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    for (int direct = 0; direct < 2; ++direct)
    {
      const loka::win32::Win32DisplayScale scale(96, metrics[scaleIndex]);
      HWND root = buttonHeightHost(scale);
      {
        Win32ScenePlatformController controller(root, scale);
        const StackProps props(STACK_AXIS_COLUMN);
        StackNode *column = direct ? new DirectNaturalWidthRow(props) : new StackNode(props);
        TextNode *text = new TextNode(TextProps("First"));
        ButtonNode *button = new ButtonNode(ButtonProps().text("Second"));
        EditTextNode *edit = new EditTextNode((EditTextProps()));
        FragmentNode *fragment = new FragmentNode((FragmentProps()));
        TextNode *last = new TextNode(TextProps("Last"));
        TextNode *tail = new TextNode(TextProps("Tail"));
        column->addChild(text);
        column->addChild(button);
        column->addChild(edit);
        fragment->addChild(last);
        fragment->addChild(tail);
        column->addChild(fragment);
        controller.onChange(column, scene::NODE_DIRTY_NONE, false);
        controller.relayout(640, 400);
        scene::Node *nodes[] = {text, button, edit, last, tail};
        int top = Win32SceneLayout::kRootInset;
        for (int i = 0; i < 5; ++i)
        {
          LOKA_VERIFY(nodes[i]->getContext() != 0);
          const RECT frame =
              childRectInParent(static_cast<Win32RetirableContext *>(nodes[i]->getContext())->hwnd(), root);
          LOKA_VERIFY(frame.left == scale.projectEdge(Win32SceneLayout::kRootInset));
          LOKA_VERIFY(frame.top == scale.projectEdge(top));
          while (scale.projectEdge(top) < frame.bottom)
            ++top;
          LOKA_VERIFY(frame.bottom == scale.projectEdge(top));
          top += Win32SceneLayout::kGap;
        }
        controller.onChange(0, scene::NODE_DIRTY_NONE, false);
        delete column;
      }
      LOKA_VERIFY(DestroyWindow(root));
    }
}

void testWin32ContainerGapNoSeats()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::dsl;
  using namespace loka::dsl::testing;
  const RailMetrics metrics[] = {RailMetrics(), loka::win32::DefaultRailMetrics()};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
  {
    const loka::win32::Win32DisplayScale scale(96, metrics[scaleIndex]);
    HWND root = buttonHeightHost(scale);
    {
      Win32ScenePlatformController controller(root, scale);
      loka::core::MutableState<bool> hidden(false);
      // A ConfirmDialog needs a result channel; the STATIC host gives it no transport, so nothing opens.
      loka::core::MutableState<ConfirmResult> resultStorage;
      NodeState<ConfirmResult> result(&resultStorage, 0);
      NodeDefinitionBase *definition =
          (Fragment().testId("gapRoot") << Text("First") << Fragment() << (Show(hidden) << Text("Hidden"))
                                        << ConfirmDialog().result(result) << Text("Second"))
              .clone();
      LOKA_VERIFY(definition != 0);
      Scene scene(definition);
      LOKA_VERIFY(scene.mount(&controller));
      SceneTestAccess::updateAttached(scene, true);
      controller.relayout(640, 160);
      TextNode *first = 0;
      TextNode *second = 0;
      loka::dsl::FlowError error;
      LOKA_VERIFY(ResolveSelector(&scene, WithinAnchor("gapRoot").descendant<TextNode>(1), first, error)
                  == loka::dsl::FLOW_STEP_SUCCEEDED);
      LOKA_VERIFY(ResolveSelector(&scene, WithinAnchor("gapRoot").descendant<TextNode>(2), second, error)
                  == loka::dsl::FLOW_STEP_SUCCEEDED);
      LOKA_VERIFY(first && second);
      const RECT a = childRectInParent(static_cast<Win32TextContext *>(first->getContext())->hwnd(), root);
      const RECT b = childRectInParent(static_cast<Win32TextContext *>(second->getContext())->hwnd(), root);
      int bottom = Win32SceneLayout::kRootInset;
      while (scale.projectEdge(bottom) < a.bottom)
        ++bottom;
      LOKA_VERIFY(b.top == scale.projectEdge(bottom + Win32SceneLayout::kGap));
      LOKA_VERIFY(countChildWindows(root) == 2);
      SceneTestAccess::unmount(scene);
      controller.drainNativeRetirements();
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
}

namespace
{
  class DirectGapBox : public loka::app::BoxNode
  {
  public:
    explicit DirectGapBox(const loka::app::BoxProps &props)
        : BoxNode(props)
    {
    }
    virtual const void *nodeTypeKey() const
    {
      return loka::app::scene::NodeTypeToken<DirectGapBox>();
    }
  };
} // namespace

void testWin32ContainerGapBox()
{
  using namespace loka::app;
  for (int direct = 0; direct < 2; ++direct)
  {
    const loka::win32::Win32DisplayScale scale(96);
    HWND root = buttonHeightHost(scale);
    {
      Win32ScenePlatformController controller(root, scale);
      FragmentNode content((FragmentProps()));
      BoxNode *box =
          direct ? new DirectGapBox(BoxProps().setSize(200, 100)) : new BoxNode(BoxProps().setSize(200, 100));
      TextNode *first = new TextNode(TextProps("First"));
      TextNode *second = new TextNode(TextProps("Second"));
      TextNode *following = new TextNode(TextProps("Following"));
      box->addChild(first);
      box->addChild(second);
      content.addChild(box);
      content.addChild(following);
      controller.onChange(&content, loka::app::scene::NODE_DIRTY_NONE, false);
      controller.relayout(640, 160);
      LOKA_VERIFY(first->getContext() && second->getContext() && following->getContext());
      const RECT a = childRectInParent(static_cast<Win32TextContext *>(first->getContext())->hwnd(), root);
      const RECT b = childRectInParent(static_cast<Win32TextContext *>(second->getContext())->hwnd(), root);
      const RECT c = childRectInParent(static_cast<Win32TextContext *>(following->getContext())->hwnd(), root);
      LOKA_VERIFY(a.top == scale.projectEdge(Win32SceneLayout::kRootInset));
      LOKA_VERIFY(b.top == a.bottom + Win32SceneLayout::kGap);
      LOKA_VERIFY(c.top == scale.projectEdge(Win32SceneLayout::kRootInset + 100 + Win32SceneLayout::kGap));
      controller.onChange(0, loka::app::scene::NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
}

void testWin32ContainerGapRow()
{
  using namespace loka::app;
  const RailMetrics metrics[] = {RailMetrics(), loka::win32::DefaultRailMetrics()};
  for (int scaleIndex = 0; scaleIndex < 2; ++scaleIndex)
    for (int aligned = 0; aligned < 2; ++aligned)
    {
      const loka::win32::Win32DisplayScale scale(96, metrics[scaleIndex]);
      HWND root = buttonHeightHost(scale);
      {
        Win32ScenePlatformController controller(root, scale);
        StackNode column((StackProps(STACK_AXIS_COLUMN)));
        StackProps rowProps(STACK_AXIS_ROW);
        if (aligned)
          rowProps.alignVertical(VERTICAL_ALIGNMENT_CENTER);
        StackNode *row = new StackNode(rowProps);
        ButtonNode *first = new ButtonNode(ButtonProps().text("First"));
        ButtonNode *second = new ButtonNode(ButtonProps().text("Second"));
        TextNode *following = new TextNode(TextProps("Following"));
        row->addChild(first);
        row->addChild(second);
        column.addChild(row);
        column.addChild(following);
        controller.onChange(&column, scene::NODE_DIRTY_NONE, false);
        controller.relayout(640, 160);
        const RECT a = childRectInParent(static_cast<Win32ButtonContext *>(first->getContext())->hwnd(), root);
        const RECT b = childRectInParent(static_cast<Win32ButtonContext *>(second->getContext())->hwnd(), root);
        const RECT c = childRectInParent(static_cast<Win32TextContext *>(following->getContext())->hwnd(), root);
        const int width = (640 - 2 * Win32SceneLayout::kRootInset - Win32SceneLayout::kGap) / 2;
        LOKA_VERIFY(a.right == scale.projectEdge(Win32SceneLayout::kRootInset + width));
        LOKA_VERIFY(b.left == scale.projectEdge(Win32SceneLayout::kRootInset + width + Win32SceneLayout::kGap));
        LOKA_VERIFY(c.top
                    == scale.projectEdge(Win32SceneLayout::kRootInset + controller.rowLayoutMetrics().buttonHeight
                                         + Win32SceneLayout::kGap));
        controller.onChange(0, scene::NODE_DIRTY_NONE, false);
      }
      LOKA_VERIFY(DestroyWindow(root));
    }
}

namespace
{
  // Same protected-list inspection as MenuTitleTests; no production getter.
  template <typename T> class AdmissionState : public loka::core::MutableState<T>
  {
  public:
    explicit AdmissionState(const T &value)
        : loka::core::MutableState<T>(value)
    {
    }
    size_t subscriptions() const
    {
      return this->handlers.size() + this->deferredHandlers.size();
    }
  };
} // namespace

namespace
{
  void verifyResidentCreationRefusalAndRetry(int leaf)
  {
    using namespace loka::app;
    using namespace loka::app::scene;
    using namespace loka::core;
    HWND root = CreateWindowExW(
        0, L"STATIC", L"refusal-host", WS_OVERLAPPED, 0, 0, 320, 240, NULL, NULL, GetModuleHandle(NULL), NULL);
    LOKA_VERIFY(root != NULL);
    {
      Win32ScenePlatformController controller(root, loka::win32::Win32DisplayScale(96));
      RegisterWin32BuiltInSupport(controller);
      AdmissionState<String> text(String::Literal("Before"));
      AdmissionState<bool> enabled(true);
      AdmissionState<int> selection(0);
      AdmissionState<resource::Image> image((resource::Image()));
      loka::app::testing::TextEditorStateOwner editorState;
      loka::testing::TextBufferFixture lines;
      LOKA_VERIFY(lines.attach(&editorState.tracker, 256) == ATTACH_OK);
      LOKA_VERIFY(lines.insert(0, String::Literal("line")) == EDIT_OK);
      Node *node = 0;
      switch (leaf)
      {
      case 0:
        node = new TextNode(TextProps(&text));
        break;
      case 1:
        node = new ButtonNode(ButtonProps().text(&text).enabled(&enabled));
        break;
      case 2:
        node = new CellNode(CellProps().text(&text));
        break;
      case 3:
        node = new EditTextNode(EditTextProps(WriteSeat<String>(&text)));
        break;
      case 4:
        node = new ImageViewNode(ImageViewProps().image(&image));
        break;
      case 5:
        node = new PopupMenuNode(PopupMenuProps().selectedIndex(WriteSeat<int>(&selection)).enabled(&enabled));
        break;
      case 6:
        node = new TextEditorNode(TextEditorProps(lines, editorState.cursor));
        break;
      case 7:
        node = new RibbonGroupEdgeNode(RibbonGroupEdgeProps(RIBBON_GROUP_EDGE_LEFT));
        break;
      case 9:
        node = new CardBandNode(CardBandProps(CARD_BAND_LEFT));
        break;
      case 8:
        node = new AttributedTextNode(AttributedTextProps(Styled("Styled", Bold)));
        break;
      }
      LOKA_VERIFY(node != 0);
      const size_t textBefore = text.subscriptions();
      const size_t enabledBefore = enabled.subscriptions();
      const size_t selectionBefore = selection.subscriptions();
      const size_t imageBefore = image.subscriptions();
      LayoutState state;
      state.x = 10;
      state.y = 10;
      state.width = 200;
      state.height = 40;
      loka::win32::testing::failNativeChildWindowCreations(2);
      for (int attempt = 0; attempt < 2; ++attempt)
      {
        LOKA_VERIFY(!controller.prepareProjectedLayout(node, state));
        LOKA_VERIFY(node->getContext() == 0);
        LOKA_VERIFY(node->lifecycleFact() == NODE_FACT_ATTACHED);
        LOKA_VERIFY(countChildWindows(root) == 0);
        LOKA_VERIFY(text.subscriptions() == textBefore);
        LOKA_VERIFY(enabled.subscriptions() == enabledBefore);
        LOKA_VERIFY(selection.subscriptions() == selectionBefore);
        LOKA_VERIFY(image.subscriptions() == imageBefore);
        // Force notifications, including ImageView's deferred subscription.
        text.set(String::Literal("After"), true);
        enabled.set(false, true);
        selection.set(1, true);
        image.set(resource::Image(), true);
      }
      LOKA_VERIFY(controller.prepareProjectedLayout(node, state));
      NodeContext *accepted = node->getContext();
      LOKA_VERIFY(accepted != 0);
      const HWND child = static_cast<Win32RetirableContext *>(accepted)->hwnd();
      LOKA_VERIFY(child != NULL && IsWindow(child));
      LOKA_VERIFY(countChildWindows(root) == 1);
      // Positive controls: prove the six fixtures really exercised bindings.
      if (leaf == 0 || leaf == 1 || leaf == 2 || leaf == 3)
        LOKA_VERIFY(text.subscriptions() > textBefore);
      if (leaf == 1 || leaf == 5)
        LOKA_VERIFY(enabled.subscriptions() > enabledBefore);
      if (leaf == 4)
        LOKA_VERIFY(image.subscriptions() > imageBefore);
      if (leaf == 5)
        LOKA_VERIFY(selection.subscriptions() > selectionBefore);
      LOKA_VERIFY(controller.prepareProjectedLayout(node, state));
      LOKA_VERIFY(node->getContext() == accepted && countChildWindows(root) == 1);
      LifecycleFactTestAccess::MarkSubtreeRetired(node);
      node->setContext(0);
      delete node;
      controller.drainNativeRetirements();
      LOKA_VERIFY(countChildWindows(root) == 0);
      LOKA_VERIFY(text.subscriptions() == textBefore && enabled.subscriptions() == enabledBefore);
      LOKA_VERIFY(selection.subscriptions() == selectionBefore && image.subscriptions() == imageBefore);
      loka::win32::testing::failNativeChildWindowCreations(0);
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
} // namespace

void testWin32TextCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(0);
}

void testWin32ButtonCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(1);
}

void testWin32CellCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(2);
}

void testWin32EditTextCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(3);
}

void testWin32ImageViewCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(4);
}

void testWin32PopupMenuCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(5);
}

void testWin32TextEditorCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(6);
}

void testWin32RibbonGroupEdgeCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(7);
}

void testWin32AttributedTextCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(8);
}

void testWin32CardBandCreationRefusalAndRetry()
{
  verifyResidentCreationRefusalAndRetry(9);
}

void testWin32CardBandsLifecycleAndRepaint()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;
  HWND rootWindow = attributedHost();
  {
    Win32ScenePlatformController controller(rootWindow, loka::win32::Win32DisplayScale(96, RailMetrics()));
    RegisterWin32BuiltInSupport(controller);
    CardNode card((CardProps()));
    card.addChild(new StackNode(StackProps(STACK_AXIS_COLUMN)));
    CardBandNode *bands[CARD_BAND_COUNT];
    HWND windows[CARD_BAND_COUNT];
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
    {
      bands[role] = new CardBandNode(CardBandProps(static_cast<CardBandRole>(role)));
      card.addChild(bands[role]);
    }
    NotifySubtreeNodeAttached(&card);
    controller.onChange(&card, NODE_DIRTY_NONE, false);
    controller.relayout(200, 100);
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
    {
      Win32CardBandContext *context = static_cast<Win32CardBandContext *>(bands[role]->getContext());
      LOKA_VERIFY(context != 0);
      windows[role] = context->hwnd();
      LOKA_VERIFY(IsWindowUnicode(windows[role]));
      Access::flushPendingInvalidations(controller);
      context->onPropsApplied();
      Access::PendingInvalidationSnapshot pending;
      LOKA_VERIFY(Access::queryPendingInvalidation(controller, 0, pending));
      LOKA_VERIFY(pending.hwnd == rootWindow && pending.includeChildren && !pending.eraseBackground);
      Access::flushPendingInvalidations(controller);
      context->relayout(11, 17, 40, 3);
      LOKA_VERIFY(Access::queryPendingInvalidation(controller, 0, pending));
      LOKA_VERIFY(pending.hwnd == rootWindow && pending.includeChildren);
    }
    // Ribbon parity: empty bands keep their existing zero-sized HWNDs.
    controller.relayout(1, 100);
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
    {
      RECT client;
      LOKA_VERIFY(GetClientRect(windows[role], &client) && IsRectEmpty(&client));
      LOKA_VERIFY(static_cast<Win32CardBandContext *>(bands[role]->getContext())->hwnd() == windows[role]);
    }
    controller.relayout(200, 100);
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
    {
      RECT client;
      LOKA_VERIFY(GetClientRect(windows[role], &client) && !IsRectEmpty(&client));
    }
    NotifySubtreeNodeDetached(&card);
    LifecycleFactTestAccess::DeliverFacts(&card);
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
      LOKA_VERIFY((GetWindowLongPtrW(windows[role], GWL_STYLE) & WS_VISIBLE) == 0);
    NotifySubtreeNodeAttached(&card);
    LifecycleFactTestAccess::DeliverFacts(&card);
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
      LOKA_VERIFY((GetWindowLongPtrW(windows[role], GWL_STYLE) & WS_VISIBLE) != 0);
    LifecycleFactTestAccess::MarkSubtreeRetired(&card);
    controller.releaseNodeContexts(&card);
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
    {
      LOKA_VERIFY(bands[role]->getContext() == 0 && IsWindow(windows[role]));
      LOKA_VERIFY(GetWindowLongPtrW(windows[role], GWLP_USERDATA) == 0);
    }
    controller.drainNativeRetirements();
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
      LOKA_VERIFY(!IsWindow(windows[role]));
    controller.onChange(0, NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(rootWindow));
}

void testWin32CardBandPaintOnlyClassification()
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace PropsReconciliationSupport;
  typedef loka::dsl::testing::Win32ScenePlatformTestAccess Access;
  HWND rootWindow = attributedHost();
  {
    Win32ScenePlatformController controller(rootWindow, loka::win32::Win32DisplayScale(96, RailMetrics()));
    Card declaration;
    Scene scene((Boundary<Tree<Card> >(Props<Card>(&declaration))));
    scene.mount(&controller);
    loka::dsl::testing::SceneTestAccess::updateAttached(scene, true);
    settle(scene);
    BoundaryNode *boundary = root(scene);
    controller.onChange(boundary, NODE_DIRTY_NONE, false);
    controller.relayout(200, 100);
    Node *card = boundary->childrenHead();
    Node *band = card->asNestable()->childrenHead()->nextInComposition;
    LOKA_VERIFY(band && band->kind() == NODE_KIND_CARD_BAND && band->getContext());
    BoundaryLocalApplyInfo info;
    info.paintKind = LOCAL_APPLY_PAINT_GENERIC;
    PlatformApplyPlan plan;
    plan.paintKind = PlatformApplyPlan::PAINT_LOCAL;
    plan.setPrimaryRoot(boundary);
    Access::resetRedrawStats(controller);
    controller.onBoundaryApply(boundary, boundary, info, plan);
    LOKA_VERIFY(Access::onBoundaryApplyCalls(controller) == 1);
    LOKA_VERIFY(Access::queuedPaintInvalidates(controller) == 0);
    // A missing native resident MUST refuse, not disappear as a container.
    // This second apply discriminates the #1360 skipped-container survivor.
    band->getContext()->onFactChanged(NODE_FACT_ATTACHED, NODE_FACT_RETIRED);
    controller.onBoundaryApply(boundary, boundary, info, plan);
    LOKA_VERIFY(Access::queuedPaintInvalidates(controller) == 1);
  }
  LOKA_VERIFY(DestroyWindow(rootWindow));
}

namespace
{
  // Invert only exact projected edges, as in the existing container-gap pins.
  int gapDoorEdgeLu(const loka::win32::Win32DisplayScale &scale, LONG edge)
  {
    int lu = 0;
    while (lu < SHRT_MAX && scale.projectEdge(lu) < edge)
      ++lu;
    LOKA_VERIFY(scale.projectEdge(lu) == edge);
    return lu;
  }

  RECT gapDoorFrame(loka::app::scene::Node *node, HWND parent)
  {
    LOKA_VERIFY(node->getContext() != 0);
    return childRectInParent(static_cast<Win32RetirableContext *>(node->getContext())->hwnd(), parent);
  }

  void verifyGapDoorInterval(Win32ScenePlatformController &controller, HWND parent,
                             loka::app::scene::Node *first, loka::app::scene::Node *second, int gap)
  {
    const RECT before = gapDoorFrame(first, parent);
    const RECT after = gapDoorFrame(second, parent);
    const loka::win32::Win32DisplayScale &scale = controller.displayScale();
    const int bottom = gapDoorEdgeLu(scale, before.bottom);
    LOKA_VERIFY(after.top == scale.projectEdge(bottom + gap));
  }

  void verifyGapDoorColumn(bool registered, bool fullSequence)
  {
    using namespace loka::app;
    const loka::win32::Win32DisplayScale scale(96, loka::win32::DefaultRailMetrics());
    HWND root = buttonHeightHost(scale);
    {
      Win32ScenePlatformController controller(root, scale);
      StackProps props(STACK_AXIS_COLUMN);
      props.gap(fullSequence ? 16 : 20);
      StackNode *column = registered ? new StackNode(props) : new DirectNaturalWidthRow(props);
      TextNode *head = fullSequence ? new TextNode(TextProps("Head")) : 0;
      FragmentNode *fragment = new FragmentNode((FragmentProps()));
      TextNode *first = new TextNode(TextProps("First"));
      TextNode *second = new TextNode(TextProps("Second"));
      ButtonNode *tail = fullSequence ? new ButtonNode(ButtonProps().text("Tail")) : 0;
      if (fullSequence)
        column->addChild(head);

      fragment->addChild(first);
      fragment->addChild(second);
      column->addChild(fragment);
      if (fullSequence)
        column->addChild(tail);

      controller.onChange(column, scene::NODE_DIRTY_NONE, false);
      controller.relayout(640, 400);
      verifyGapDoorInterval(controller, root, first, second, fullSequence ? 16 : 20);
      if (fullSequence)
      {
        LOKA_VERIFY(gapDoorFrame(head, root).top == scale.projectEdge(Win32SceneLayout::kRootInset));
        verifyGapDoorInterval(controller, root, head, first, 16);
        verifyGapDoorInterval(controller, root, second, tail, 16);
      }
      controller.onChange(0, scene::NODE_DIRTY_NONE, false);
      delete column;
    }
    LOKA_VERIFY(DestroyWindow(root));
  }

  enum GapDoorReset { GAP_DOOR_BOX, GAP_DOOR_ZSTACK, GAP_DOOR_GRID, GAP_DOOR_SCROLL, GAP_DOOR_COLUMN };

  void verifyGapDoorReset(GapDoorReset reset)
  {
    using namespace loka::app;
    const loka::win32::Win32DisplayScale scale(96, loka::win32::DefaultRailMetrics());
    HWND root = buttonHeightHost(scale);
    {
      Win32ScenePlatformController controller(root, scale);
      StackProps props(STACK_AXIS_COLUMN);
      props.gap(20);
      DirectNaturalWidthRow column(props);
      scene::NestableNode *container = 0;
      switch (reset)
      {
      case GAP_DOOR_BOX: container = new BoxNode((BoxProps())); break;
      case GAP_DOOR_ZSTACK: container = new ZStackNode((ZStackProps())); break;
      case GAP_DOOR_GRID: container = new GridNode(GridProps(1, 1)); break;
      case GAP_DOOR_SCROLL: container = new ScrollViewNode((ScrollViewProps())); break;
      case GAP_DOOR_COLUMN: container = new StackNode(StackProps(STACK_AXIS_COLUMN)); break;
      }
      LOKA_VERIFY(container != 0);
      FragmentNode *inside = new FragmentNode((FragmentProps()));
      TextNode *first = new TextNode(TextProps("Inside first"));
      TextNode *second = new TextNode(TextProps("Inside second"));
      inside->addChild(first);
      inside->addChild(second);
      container->addChild(inside);
      column.addChild(container);
      FragmentNode *sibling = new FragmentNode((FragmentProps()));
      TextNode *third = new TextNode(TextProps("Sibling first"));
      TextNode *fourth = new TextNode(TextProps("Sibling second"));
      sibling->addChild(third);
      sibling->addChild(fourth);
      column.addChild(sibling);
      controller.onChange(&column, scene::NODE_DIRTY_NONE, false);
      controller.relayout(640, 400);
      // Scroll children use viewport-local coordinates; all other children use root coordinates.
      if (reset == GAP_DOOR_SCROLL)
        LOKA_VERIFY(container->getContext() != 0);
      HWND parent = reset == GAP_DOOR_SCROLL
          ? static_cast<Win32RetirableContext *>(container->getContext())->hwnd() : root;
      verifyGapDoorInterval(controller, parent, first, second, 6);
      verifyGapDoorInterval(controller, root, third, fourth, 20);
      controller.onChange(0, scene::NODE_DIRTY_NONE, false);
    }
    LOKA_VERIFY(DestroyWindow(root));
  }
}

void testWin32GapDoorColumnFragment()
{
  verifyGapDoorColumn(false, true);
}

void testWin32GapDoorBoxSiblingIsolation()
{
  verifyGapDoorReset(GAP_DOOR_BOX);
}

void testWin32GapDoorZStackReset()
{
  verifyGapDoorReset(GAP_DOOR_ZSTACK);
}

void testWin32GapDoorGridReset()
{
  verifyGapDoorReset(GAP_DOOR_GRID);
}

void testWin32GapDoorScrollViewResetAndSiblingIsolation()
{
  verifyGapDoorReset(GAP_DOOR_SCROLL);
}

void testWin32GapDoorUnspecifiedColumnReset()
{
  verifyGapDoorReset(GAP_DOOR_COLUMN);
}

void testWin32GapDoorRegisteredColumnBridge()
{
  verifyGapDoorColumn(true, false);
}

void testWin32GapDoorRowFragment()
{
  using namespace loka::app;
  const loka::win32::Win32DisplayScale scale(96, loka::win32::DefaultRailMetrics());
  HWND root = buttonHeightHost(scale);
  {
    Win32ScenePlatformController controller(root, scale);
    StackProps props(STACK_AXIS_ROW);
    props.gap(20);
    DirectNaturalWidthRow row(props);
    FragmentNode *fragment = new FragmentNode((FragmentProps()));
    TextNode *first = new TextNode(TextProps("First"));
    TextNode *second = new TextNode(TextProps("Second"));
    fragment->addChild(first);
    fragment->addChild(second);
    row.addChild(fragment);
    controller.onChange(&row, scene::NODE_DIRTY_NONE, false);
    controller.relayout(640, 400);
    verifyGapDoorInterval(controller, root, first, second, 20);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32GapDoorRootFragment()
{
  using namespace loka::app;
  const loka::win32::Win32DisplayScale scale(96, loka::win32::DefaultRailMetrics());
  HWND root = buttonHeightHost(scale);
  {
    Win32ScenePlatformController controller(root, scale);
    FragmentNode fragment((FragmentProps()));
    TextNode *first = new TextNode(TextProps("First"));
    TextNode *second = new TextNode(TextProps("Second"));
    fragment.addChild(first);
    fragment.addChild(second);
    controller.onChange(&fragment, scene::NODE_DIRTY_NONE, false);
    controller.relayout(640, 400);
    LOKA_VERIFY(gapDoorFrame(first, root).top == scale.projectEdge(Win32SceneLayout::kRootInset));
    verifyGapDoorInterval(controller, root, first, second, 6);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}

void testWin32GapDoorScrollViewContent()
{
  using namespace loka::app;
  const loka::win32::Win32DisplayScale scale(96, loka::win32::DefaultRailMetrics());
  HWND root = buttonHeightHost(scale);
  {
    Win32ScenePlatformController controller(root, scale);
    StackProps props(STACK_AXIS_COLUMN);
    props.gap(20);
    DirectNaturalWidthRow column(props);
    ScrollViewNode *scroll = new ScrollViewNode((ScrollViewProps()));
    BoxNode *box = new BoxNode((BoxProps()));
    FragmentNode *fragment = new FragmentNode((FragmentProps()));
    TextNode *first = new TextNode(TextProps("Box first"));
    TextNode *second = new TextNode(TextProps("Box second"));
    TextNode *third = new TextNode(TextProps("Fragment first"));
    TextNode *fourth = new TextNode(TextProps("Fragment second"));
    box->addChild(first);
    box->addChild(second);
    fragment->addChild(third);
    fragment->addChild(fourth);
    scroll->addChild(box);
    scroll->addChild(fragment);
    column.addChild(scroll);
    controller.onChange(&column, scene::NODE_DIRTY_NONE, false);
    controller.relayout(640, 400);
    LOKA_VERIFY(scroll->getContext() != 0);
    HWND frame = static_cast<Win32RetirableContext *>(scroll->getContext())->hwnd();
    HWND parent = FindWindowExW(frame, NULL, L"LOKA_SCROLL_VIEW", NULL);
    LOKA_VERIFY(parent);
    verifyGapDoorInterval(controller, parent, first, second, 6);
    verifyGapDoorInterval(controller, parent, second, third, 6);
    verifyGapDoorInterval(controller, parent, third, fourth, 6);
    controller.onChange(0, scene::NODE_DIRTY_NONE, false);
  }
  LOKA_VERIFY(DestroyWindow(root));
}
