#ifndef LOKA_MAC_OBJC_COMPAT_HPP
#define LOKA_MAC_OBJC_COMPAT_HPP

#import <AvailabilityMacros.h>
#import <AppKit/AppKit.h>
#include <float.h>

#if !defined(__OBJC2__) || !__OBJC2__
#ifndef NSINTEGER_DEFINED
typedef int NSInteger;
typedef unsigned int NSUInteger;
#define NSINTEGER_DEFINED 1
#endif

#ifndef CGFLOAT_TYPE
typedef float CGFloat;
#define CGFLOAT_TYPE 1
#endif
#endif

#if !defined(MAC_OS_X_VERSION_MAX_ALLOWED) || (MAC_OS_X_VERSION_MAX_ALLOWED < 1060)
#ifndef NSApplicationActivationPolicyRegular
#define NSApplicationActivationPolicyRegular 0
#endif
#endif

#ifndef CGFLOAT_MAX
#define CGFLOAT_MAX FLT_MAX
#endif

#ifndef NSRunLoopCommonModes
#define NSRunLoopCommonModes ((NSString *)kCFRunLoopCommonModes)
#endif

// Legacy AssertMacros.h exports this common member name as a function-like
// macro. Do not let AppKit's native namespace leak into portable C++ headers.
#ifdef verify
#undef verify
#endif

// AppKit's modern constant names are enum values, not preprocessor macros.
// Select by SDK version so current SDKs stay warning-clean while the legacy
// 10.5 SDK used for the 10.4 deployment target keeps the original spellings.
// The push-button bezel constant has three SDK spellings: NSRoundedBezelStyle
// until the 10.14 SDK deprecated it, NSBezelStyleRounded from the 10.14 SDK
// through the 13.x SDKs, and NSBezelStylePush since the macOS 14 SDK rename.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 140000)
#define LOKA_MAC_BUTTON_BEZEL_STYLE NSBezelStylePush
#elif defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101400)
#define LOKA_MAC_BUTTON_BEZEL_STYLE NSBezelStyleRounded
#else
#define LOKA_MAC_BUTTON_BEZEL_STYLE NSRoundedBezelStyle
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101400)
#define LOKA_MAC_BUTTON_TYPE_MOMENTARY_PUSH_IN NSButtonTypeMomentaryPushIn
#else
#define LOKA_MAC_BUTTON_TYPE_MOMENTARY_PUSH_IN NSMomentaryPushInButton
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101200)
#define LOKA_MAC_WINDOW_STYLE_TITLED NSWindowStyleMaskTitled
#define LOKA_MAC_WINDOW_STYLE_CLOSABLE NSWindowStyleMaskClosable
#define LOKA_MAC_WINDOW_STYLE_RESIZABLE NSWindowStyleMaskResizable
#define LOKA_MAC_WINDOW_STYLE_MINIATURIZABLE NSWindowStyleMaskMiniaturizable
#define LOKA_MAC_COMPOSITING_SOURCE_OVER NSCompositingOperationSourceOver
#define LOKA_MAC_TEXT_ALIGNMENT_LEFT NSTextAlignmentLeft
#define LOKA_MAC_TEXT_ALIGNMENT_CENTER NSTextAlignmentCenter
#define LOKA_MAC_TEXT_ALIGNMENT_RIGHT NSTextAlignmentRight
#define LOKA_MAC_TEXT_ALIGNMENT_NATURAL NSTextAlignmentNatural
#else
#define LOKA_MAC_WINDOW_STYLE_TITLED NSTitledWindowMask
#define LOKA_MAC_WINDOW_STYLE_CLOSABLE NSClosableWindowMask
#define LOKA_MAC_WINDOW_STYLE_RESIZABLE NSResizableWindowMask
#define LOKA_MAC_WINDOW_STYLE_MINIATURIZABLE NSMiniaturizableWindowMask
#define LOKA_MAC_COMPOSITING_SOURCE_OVER NSCompositeSourceOver
#define LOKA_MAC_TEXT_ALIGNMENT_LEFT NSLeftTextAlignment
#define LOKA_MAC_TEXT_ALIGNMENT_CENTER NSCenterTextAlignment
#define LOKA_MAC_TEXT_ALIGNMENT_RIGHT NSRightTextAlignment
#define LOKA_MAC_TEXT_ALIGNMENT_NATURAL NSNaturalTextAlignment
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101000)
#define LOKA_MAC_MODAL_RESPONSE_OK NSModalResponseOK
#else
#define LOKA_MAC_MODAL_RESPONSE_OK NSOKButton
#endif

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101300)
#define LOKA_MAC_CONTROL_STATE_ON NSControlStateValueOn
#define LOKA_MAC_CONTROL_STATE_OFF NSControlStateValueOff
#else
#define LOKA_MAC_CONTROL_STATE_ON NSOnState
#define LOKA_MAC_CONTROL_STATE_OFF NSOffState
#endif

// NSBitmapImageRep's PNG enum was renamed in the macOS 10.10 SDK. These are
// enum values rather than macros, so select the spelling from the SDK surface.
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && (MAC_OS_X_VERSION_MAX_ALLOWED >= 101000)
#define LOKA_MAC_BITMAP_PNG_FILE_TYPE NSBitmapImageFileTypePNG
#else
#define LOKA_MAC_BITMAP_PNG_FILE_TYPE NSPNGFileType
#endif

#if !defined(MAC_OS_X_VERSION_MAX_ALLOWED) || (MAC_OS_X_VERSION_MAX_ALLOWED < 1050)
@interface NSString (LokaTigerStringDrawingCompat)
- (NSSize)sizeWithFont:(NSFont *)font;
- (NSSize)sizeWithFont:(NSFont *)font constrainedToSize:(NSSize)size;
- (NSSize)sizeWithFont:(NSFont *)font constrainedToSize:(NSSize)size lineBreakMode:(NSLineBreakMode)mode;
@end
#endif

// The 10.6 SDK declares setUsesSingleLineMode: on NSCell. Older SDKs need the
// declaration so the capability-guarded cell call remains type-correct.
#if !defined(MAC_OS_X_VERSION_MAX_ALLOWED) || (MAC_OS_X_VERSION_MAX_ALLOWED < 1060)
@interface NSCell (LokaSingleLineModeCompat)
- (void)setUsesSingleLineMode:(BOOL)flag;
@end
#endif

// backingScaleFactor arrived in 10.7, so the legacy SDK does not declare it and
// Objective-C++ would infer an id return and reject the conversion to a number.
// Declared and never implemented: this supplies a return type so the call
// type-checks, while respondsToSelector: still decides at runtime whether the
// call happens at all -- and on anything this SDK can target, it does not. The
// CGFloat typedef above matches the 32-bit ABI the declaration would need.
//
// Note the difference from the Tiger blocks above: those methods do exist on the
// old system and are only missing from current SDKs, so their declarations
// enable a real call. This one enables compilation of a call that the runtime
// guard will decline.
// Keep this declaration unconditional. A legacy SDK installed beside a newer
// Xcode can resolve AvailabilityMacros.h from the host while resolving
// NSWindow.h from the selected SDK. In that mixed but supported setup an SDK
// version guard describes neither header surface and drops the declaration.
// Repeating a method declaration from a newer NSWindow interface is harmless;
// this category still provides no implementation.
@interface NSWindow (LokaBackingScaleCompat)
- (CGFloat)backingScaleFactor;
@end

// effectiveAppearance arrived after the legacy SDKs. As above, this
// declaration only gives Objective-C++ the return type; respondsToSelector:
// remains the runtime wall.
@interface NSWindow (LokaEffectiveAppearanceCompat)
- (id)effectiveAppearance;
@end

// NSView's inputContext and NSTextInputContext's discardMarkedText arrived in
// 10.6. As above, these declarations only give Objective-C++ the types;
// respondsToSelector: remains the runtime wall.
@interface NSView (LokaInputContextCompat)
- (id)inputContext;
@end
@interface NSObject (LokaDiscardMarkedTextCompat)
- (void)discardMarkedText;
@end

// Declaration-only compatibility with legacy SDKs, like backingScaleFactor.
// NSInteger is the ABI of NSScrollerStyle; 0 is legacy, 1 is overlay.
@interface NSScroller (LokaScrollerStyleCompat)
+ (NSInteger)preferredScrollerStyle;
+ (CGFloat)scrollerWidthForControlSize:(NSControlSize)controlSize scrollerStyle:(NSInteger)style;
@end

#endif // LOKA_MAC_OBJC_COMPAT_HPP
