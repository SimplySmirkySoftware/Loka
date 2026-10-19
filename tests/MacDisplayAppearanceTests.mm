#include "MacDisplayAppearanceTests.hpp"
#include "support/TestVerify.hpp"
#include "MacDisplayAppearance.hpp"
#include "MacPlatformContext.hpp"
#include "app/PlatformContext.hpp"
#include <AppKit/AppKit.h>
#include <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <assert.h>

@interface LokaLegacyAppearanceProbe : NSObject
@end

@implementation LokaLegacyAppearanceProbe
- (NSString *)name
{
  return @"NSAppearanceNameAqua";
}
@end

@interface LokaMatchingAppearanceProbe : NSObject
{
  NSString *match_;
}
- (id)initWithMatch:(NSString *)match;
- (NSString *)name;
- (NSString *)bestMatchFromAppearancesWithNames:(NSArray *)names;
@end

@implementation LokaMatchingAppearanceProbe
- (id)initWithMatch:(NSString *)match
{
  self = [super init];
  if (self)
  {
    match_ = [match retain];
  }
  return self;
}

- (void)dealloc
{
  [match_ release];
  [super dealloc];
}

- (NSString *)name
{
  // The mechanism must use AppKit's match result, not infer from this name.
  return @"CustomLightLookingName";
}

- (NSString *)bestMatchFromAppearancesWithNames:(NSArray *)names
{
  assert([names count] == 2);
  assert([names containsObject:@"NSAppearanceNameAqua"]);
  assert([names containsObject:@"NSAppearanceNameDarkAqua"]);
  return match_;
}
@end

void testMacDisplayAppearanceDeclinesWithoutMatchingCapability()
{
  LokaLegacyAppearanceProbe *appearance = [[LokaLegacyAppearanceProbe alloc] init];
  Window::DisplayAppearance value = Window::DISPLAY_APPEARANCE_DARK;
  LOKA_VERIFY(!loka::macos::TryReadDisplayAppearance((void *)appearance, value));
  assert(value == Window::DISPLAY_APPEARANCE_DARK);
  [appearance release];
}

void testMacDisplayAppearanceUsesNativeBestMatch()
{
  LokaMatchingAppearanceProbe *darkAppearance = [[LokaMatchingAppearanceProbe alloc]
      initWithMatch:@"NSAppearanceNameDarkAqua"];
  Window::DisplayAppearance value = Window::DISPLAY_APPEARANCE_LIGHT;
  LOKA_VERIFY(loka::macos::TryReadDisplayAppearance((void *)darkAppearance, value));
  assert(value == Window::DISPLAY_APPEARANCE_DARK);
  [darkAppearance release];

  LokaMatchingAppearanceProbe *lightAppearance = [[LokaMatchingAppearanceProbe alloc]
      initWithMatch:@"NSAppearanceNameAqua"];
  value = Window::DISPLAY_APPEARANCE_DARK;
  LOKA_VERIFY(loka::macos::TryReadDisplayAppearance((void *)lightAppearance, value));
  assert(value == Window::DISPLAY_APPEARANCE_LIGHT);
  [lightAppearance release];

  LokaMatchingAppearanceProbe *unknownAppearance = [[LokaMatchingAppearanceProbe alloc]
      initWithMatch:@"CustomAppearanceName"];
  value = Window::DISPLAY_APPEARANCE_DARK;
  LOKA_VERIFY(!loka::macos::TryReadDisplayAppearance((void *)unknownAppearance, value));
  assert(value == Window::DISPLAY_APPEARANCE_DARK);
  [unknownAppearance release];
}

// Two screen-shaped probes with different depths: the primary (first of
// +screens) answers 24 bits, the key window's (+mainScreen) 64, so the query
// is pinned to the display it reads from, not merely to a positive value.
@interface LokaDepthScreenProbe : NSObject
{
  NSWindowDepth depth_;
}
- (id)initWithDepth:(NSWindowDepth)depth;
- (NSWindowDepth)depth;
@end

@implementation LokaDepthScreenProbe
- (id)initWithDepth:(NSWindowDepth)depth
{
  self = [super init];
  if (self)
  {
    depth_ = depth;
  }
  return self;
}
- (NSWindowDepth)depth
{
  return depth_;
}
@end

static NSArray *gLokaProbeScreens = nil;

@interface NSScreen (LokaDepthProbe)
+ (NSArray *)lokaProbeScreens;
+ (NSScreen *)lokaProbeMainScreen;
@end

@implementation NSScreen (LokaDepthProbe)
+ (NSArray *)lokaProbeScreens
{
  return gLokaProbeScreens;
}
+ (NSScreen *)lokaProbeMainScreen
{
  return (NSScreen *)[gLokaProbeScreens lastObject];
}
@end

namespace
{
  void swapScreenClassMethods()
  {
    method_exchangeImplementations(class_getClassMethod([NSScreen class], @selector(screens)),
                                   class_getClassMethod([NSScreen class], @selector(lokaProbeScreens)));
    method_exchangeImplementations(class_getClassMethod([NSScreen class], @selector(mainScreen)),
                                   class_getClassMethod([NSScreen class], @selector(lokaProbeMainScreen)));
  }
} // namespace

void testMacMainDisplayDepthAnswersFromTheMainScreen()
{
  printf("\n==== [testMacMainDisplayDepthAnswersFromTheMainScreen] start ====\n");
  // The rail answers from the main display, the first of -screens (the one
  // with the menu bar), not -mainScreen (the key window's): presence and value
  // from one call, through the base pointer the SmirkyCard host holds.
  MacPlatformContext context;
  const PlatformContext *base = &context;
  int depth = -1;
  NSArray *screens = [NSScreen screens];
  if (![screens count])
  {
    LOKA_VERIFY(!base->queryMainDisplayDepth(depth));
    LOKA_VERIFY(depth == -1);
    printf("[skip] no screens in this session; the decline branch is the one pinned\n");
  }
  else
  {
    NSScreen *primary = [screens objectAtIndex:0];
    LOKA_VERIFY(base->queryMainDisplayDepth(depth));
    LOKA_VERIFY(depth > 0);
    LOKA_VERIFY(depth == (int)NSBitsPerPixelFromDepth([primary depth]));
  }
  // Controlled screens (bot P1 on #1481): +screens answers two probes of
  // different depths and +mainScreen the second, so an implementation that
  // read the key window's screen would answer 64 here, not 24.
  LokaDepthScreenProbe *primaryProbe = [[LokaDepthScreenProbe alloc] initWithDepth:NSWindowDepthTwentyfourBitRGB];
  LokaDepthScreenProbe *keyProbe = [[LokaDepthScreenProbe alloc] initWithDepth:NSWindowDepthSixtyfourBitRGB];
  gLokaProbeScreens = [[NSArray alloc] initWithObjects:primaryProbe, keyProbe, nil];
  swapScreenClassMethods();
  int probed = -1;
  const bool answered = base->queryMainDisplayDepth(probed);
  swapScreenClassMethods();
  [gLokaProbeScreens release];
  gLokaProbeScreens = nil;
  [primaryProbe release];
  [keyProbe release];
  LOKA_VERIFY(answered);
  LOKA_VERIFY(probed == 24);
  printf("==== [testMacMainDisplayDepthAnswersFromTheMainScreen] PASSED ====\n");
}
