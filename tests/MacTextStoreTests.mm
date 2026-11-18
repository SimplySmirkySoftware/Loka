#include "MacTextStoreTests.hpp"
#include "MacScenePlatformController.hpp"
#include "MacApp.hpp"
#include "MacWindow.hpp"
#include "MacObjCCompat.hpp"
#include "text/MacTextStore.hpp"
#include "support/MacRetirementProbe.hpp"
#include "support/TestVerify.hpp"
#include "support/TextDocumentAccess.hpp"
#include "support/TextStoreConformance.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include "testing/app/AppTestAccess.hpp"
#include "testing/MacWindowTestAccess.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/style/Style.hpp"
#include "app/text/TextStoreSource.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include <cstdio>
#include <string>

using namespace loka::app;
using namespace loka::app::scene;
namespace loka
{
  namespace macos
  {
    namespace testing
    {
      struct TextStoreAccess
      {
        static MacTextStore &store(TextStoreRow &row)
        {
          return static_cast<MacTextStore &>(row);
        }
        static NSTextView *view(TextStoreRow &row)
        {
          return store(row).view_;
        }
        static void observePartialNetwork(TextStoreRow &row, int *deaths)
        {
          MacTextStore &s = store(row);
          id objects[] = {s.storage_, s.layout_, s.container_, s.view_, s.undo_};
          for (unsigned i = 0; i < sizeof objects / sizeof *objects; ++i)
            if (objects[i])
              observeMacNativeDeallocation(objects[i], deaths[i]);
        }
        static bool pending(TextStoreRow &row)
        {
          return store(row).changes_.pending();
        }
        static NSTextStorage *storage(TextStoreRow &row)
        {
          return store(row).storage_;
        }
        static NSLayoutManager *layout(TextStoreRow &row)
        {
          return store(row).layout_;
        }
      };
    } // namespace testing
  } // namespace macos
} // namespace loka
using namespace loka::macos::testing;
namespace
{
  typedef TextStoreAccess Access;
  class Pool
  {
  public:
    Pool()
        : pool_([[NSAutoreleasePool alloc] init])
    {
      [NSApplication sharedApplication];
    }
    ~Pool()
    {
      [this->pool_ drain];
    }

  private:
    Pool(const Pool &);
    Pool &operator=(const Pool &);
    NSAutoreleasePool *pool_;
  };
  TextReplacement replacement(const std::string &body, TextPlacement placement = TextPlacement::Start())
  {
    loka::core::resource::Blob bytes = loka::core::resource::Blob::Create();
    LOKA_VERIFY(bytes.tryAssign(reinterpret_cast<const unsigned char *>(body.data()), body.size()));
    bytes.sealBytes();
    TextReplacement result;
    LOKA_VERIFY(TextReplacement::FromUtf8(bytes, placement, result) == TEXT_DOCUMENT_OK);
    return result;
  }
  TextStoreRow *birth(MacScenePlatformController &controller,
                      const TextReplacement &value,
                      TextStoreReading &reading,
                      TextStoreRow *old = 0)
  {
    TextStoreSource source(value);
    TextStoreRow *fresh = 0;
    LOKA_VERIFY(controller.createTextStore(source, old, fresh, reading) == TEXT_STORE_BORN && fresh);
    LOKA_VERIFY(![Access::view(*fresh) superview] && ![Access::view(*fresh) window]);
    return fresh;
  }
  void checkChunk(MacScenePlatformController &controller,
                  TextStoreRow &row,
                  unsigned long start,
                  unsigned long end,
                  unsigned long capacity,
                  TextChunkResult result,
                  unsigned long next,
                  const std::string &expected)
  {
    char scratch[256];
    unsigned long written = 99;
    LOKA_VERIFY(capacity <= sizeof scratch);
    LOKA_VERIFY(controller.readTextChunk(row, start, end, scratch, capacity, written) == result);
    LOKA_VERIFY(start == next && written == expected.size() && std::string(scratch, written) == expected);
  }
  void changed(MacScenePlatformController &controller, TextStoreRow &row, TextChangedRange expected)
  {
    TextStoreReading reading;
    TextChangedRange range;
    LOKA_VERIFY(controller.peekTextStore(row) && controller.peekTextStore(row));
    LOKA_VERIFY(controller.readTextStore(row, reading) && reading.changed() && reading.queryRange(range));
    LOKA_VERIFY(range == expected && !controller.peekTextStore(row));
  }
} // namespace
void testMacTextStoreBirth()
{
  Pool pool;
  MacScenePlatformController controller(0, RailMetrics());
  const std::string body("a\0\r\n\xf0\x9f\x98\x80", 8);
  const std::string logical("a\0\n\xf0\x9f\x98\x80", 7);
  const TextPlacement placements[] = {TextPlacement::Start(),
                                      TextPlacement::AtUtf8Offset(2),
                                      TextPlacement::AtUtf8Offset(5),
                                      TextPlacement::AtUtf8Offset(100),
                                      TextPlacement::End()};
  const unsigned long carets[] = {0, 2, 3, 5, 5};
  for (unsigned i = 0; i < 5; ++i)
  {
    TextStoreSource source(replacement(body, placements[i]));
    TextStoreReading reading;
    TextStoreRow *row = 0;
    LOKA_VERIFY(controller.createTextStore(source, 0, row, reading) == TEXT_STORE_BORN && row);
    LOKA_VERIFY(source.clamped() == (i == 2 || i == 3));
    TextChangedRange range;
    LOKA_VERIFY(reading.queryRange(range) && range == TextChangedRange(0, 0, 5));
    LOKA_VERIFY(!reading.changed() && !reading.marked() && reading.caret() == carets[i]
                && reading.anchor() == carets[i]);
    LOKA_VERIFY(!controller.peekTextStore(*row));
    NSTextView *view = Access::view(*row);
    LOKA_VERIFY(![view superview] && ![view window] && ![view isRichText] && [view allowsUndo]);
    LOKA_VERIFY(![view isFieldEditor] && ![view usesFontPanel] && [view isVerticallyResizable]);
    LOKA_VERIFY([view font] == (NSFont *)controller.textFont(TextStyle()));
    LOKA_VERIFY([view maxSize].height == CGFLOAT_MAX && [view maxSize].width == CGFLOAT_MAX);
    LOKA_VERIFY([view undoManager] == Access::store(*row).undoManager() && ![[view undoManager] canUndo]);
    LOKA_VERIFY(NSEqualRanges([view selectedRange], NSMakeRange(carets[i], 0)));
    checkChunk(controller, *row, 0, kTextNativeEnd, 80, TEXT_CHUNK_PROGRESS, 5, logical);
    row->retire();
    controller.drainNativeRetirements();
  }
  // Grow across the initial buffer with the surrogate pair straddling its boundary.
  const std::string longBody = std::string(255, 'a') + "\xf0\x9f\x98\x80" + std::string(512, 'b');
  TextStoreReading reading;
  TextStoreRow *row = birth(controller, replacement(longBody, TextPlacement::End()), reading);
  LOKA_VERIFY(reading.caret() == 769);
  checkChunk(controller,
             *row,
             253,
             259,
             8,
             TEXT_CHUNK_PROGRESS,
             259,
             "aa\xf0\x9f\x98\x80"
             "bb");
}
void testMacTextStoreChanges()
{
  Pool pool;
  MacScenePlatformController controller(0, RailMetrics());
  TextStoreReading reading;
  TextStoreRow &row = *birth(controller, replacement("abcd"), reading);
  NSTextView *view = Access::view(row);
  NSTextStorage *storage = [view textStorage];
  [storage addAttribute:NSUnderlineStyleAttributeName value:[NSNumber numberWithInt:1] range:NSMakeRange(1, 2)];
  [view setSelectedRange:NSMakeRange(1, 2)];
  LOKA_VERIFY(!controller.applyTextRequest(row, 5) && !controller.peekTextStore(row));
  LOKA_VERIFY(controller.readTextStore(row, reading) && !reading.changed() && reading.anchor() == 1
              && reading.caret() == 3);
  [storage replaceCharactersInRange:NSMakeRange(2, 0) withString:@"XY"];
  LOKA_VERIFY(controller.applyTextRequest(row, 1));
  changed(controller, row, TextChangedRange(2, 0, 2));
  LOKA_VERIFY(controller.applyTextRequest(row, 4));
  LOKA_VERIFY(controller.readTextStore(row, reading) && !reading.changed() && reading.anchor() == 4
              && reading.caret() == 4);
  [storage replaceCharactersInRange:NSMakeRange(2, 2) withString:@""];
  changed(controller, row, TextChangedRange(2, 2, 0));
  [storage replaceCharactersInRange:NSMakeRange(1, 1) withString:@"123"];
  [storage replaceCharactersInRange:NSMakeRange(4, 1) withString:@""];
  changed(controller, row, TextChangedRange(1, 2, 3));
  [view setSelectedRange:NSMakeRange(0, 0)];
  [view insertText:@"Z" replacementRange:NSMakeRange(NSNotFound, 0)];
  {
    // A plain-text view fixes attributes over the paragraph while typing, so the native edited
    // range widens to it (Tahoe 26.7.1: 0+5 -> 0+6 for one inserted unit). The note must cover
    // the edit and carry its net size change; ranges are a guideline (page 2).
    TextStoreReading typed;
    TextChangedRange range;
    LOKA_VERIFY(controller.peekTextStore(row) && controller.readTextStore(row, typed) && typed.changed()
                && typed.queryRange(range));
    LOKA_VERIFY(range.start() == 0 && range.insertedLength() == range.length() + 1);
    LOKA_VERIFY(!controller.peekTextStore(row));
  }
  [view setMarkedText:@"k" selectedRange:NSMakeRange(1, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
  LOKA_VERIFY(controller.readTextStore(row, reading) && reading.marked());
  NSString *before = [[storage string] copy];
  controller.commitMarkedText(row);
  LOKA_VERIFY(controller.peekTextStore(row) && [[storage string] isEqualToString:before]);
  [before release];
  LOKA_VERIFY(controller.readTextStore(row, reading) && reading.changed() && !reading.marked());
  controller.commitMarkedText(row);
  LOKA_VERIFY(!controller.peekTextStore(row));
}
void testMacTextStoreChunks()
{
  Pool pool;
  MacScenePlatformController controller(0, RailMetrics());
  TextStoreReading reading;
  TextStoreRow &row = *birth(controller, TextReplacement::Empty(), reading);
  NSTextStorage *storage = Access::storage(row);
  const unichar raw[] = {'a', '\r', 'b', '\n', '\r', '\n', 0xd83d, 0xde00, 0x2028};
  [storage replaceCharactersInRange:NSMakeRange(0, 0) withString:[NSString stringWithCharacters:raw length:9]];
  checkChunk(controller, row, 0, kTextNativeEnd, 64, TEXT_CHUNK_PROGRESS, 9, "a\nb\n\n\xf0\x9f\x98\x80\xe2\x80\xa8");
  checkChunk(controller, row, 4, 6, 1, TEXT_CHUNK_PROGRESS, 6, "\n");
  checkChunk(controller, row, 0, 9, 2, TEXT_CHUNK_PROGRESS, 2, "a\n");
  for (unsigned i = 1; i <= 3; ++i)
    checkChunk(controller, row, 6, 8, i, TEXT_CHUNK_NO_ROOM, 6, "");
  checkChunk(controller, row, 6, 8, 4, TEXT_CHUNK_PROGRESS, 8, "\xf0\x9f\x98\x80");
  checkChunk(controller, row, 9, kTextNativeEnd, 1, TEXT_CHUNK_END, 9, "");
  const unsigned invalid[][2] = {{5, 5}, {7, 7}, {0, 5}, {0, 7}, {5, 8}, {7, 9}, {3, 2}, {10, 10}, {0, 10}};
  for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i)
    checkChunk(controller, row, invalid[i][0], invalid[i][1], 4, TEXT_CHUNK_REFUSED_RANGE, invalid[i][0], "");
  checkChunk(controller, row, 0, 0, 0, TEXT_CHUNK_REFUSED_RANGE, 0, "");
  unsigned long offset = 1, written = 42;
  LOKA_VERIFY(controller.readTextChunk(row, offset, 1, 0, 1, written) == TEXT_CHUNK_REFUSED_RANGE && offset == 1
              && !written);
  for (unsigned pair = 0; pair < 2; ++pair)
  {
    unichar units[129];
    for (unsigned i = 0; i < 127; ++i)
      units[i] = 'a';
    units[127] = pair ? 0xd83d : '\r';
    units[128] = pair ? 0xde00 : '\n';
    [storage replaceCharactersInRange:NSMakeRange(0, [storage length])
                           withString:[NSString stringWithCharacters:units length:129]];
    checkChunk(controller,
               row,
               0,
               kTextNativeEnd,
               160,
               TEXT_CHUNK_PROGRESS,
               129,
               std::string(127, 'a') + (pair ? "\xf0\x9f\x98\x80" : "\n"));
  }
  for (unsigned low = 0; low < 2; ++low)
  {
    unichar units[] = {'a', static_cast<unichar>(low ? 0xdc00 : 0xd800)};
    [storage replaceCharactersInRange:NSMakeRange(0, [storage length])
                           withString:[NSString stringWithCharacters:units length:2]];
    checkChunk(controller, row, 0, kTextNativeEnd, 4, TEXT_CHUNK_REFUSED_MALFORMED, 0, "");
    checkChunk(controller, row, 0, kTextNativeEnd, 1, TEXT_CHUNK_PROGRESS, 1, "a");
    checkChunk(controller, row, 1, kTextNativeEnd, 4, TEXT_CHUNK_REFUSED_MALFORMED, 1, "");
  }
  row.retire();
  checkChunk(controller, row, 0, kTextNativeEnd, 4, TEXT_CHUNK_REFUSED_RESOURCE, 0, "");
}

// Test-only observers keep the editable interval distinct from storage commit and undo replay.
@interface LokaStoreBracketObserver : NSObject
{
  unsigned phase_;
  unsigned change_[3], undo_[3], begin_[3];
}
- (void)setPhase:(unsigned)phase;
- (void)observe:(NSNotification *)notification;
- (void)checkResult:(BOOL)accepted;
@end
@implementation LokaStoreBracketObserver
- (void)setPhase:(unsigned)phase
{
  phase_ = phase;
}
- (void)observe:(NSNotification *)notification
{
  NSString *name = [notification name];
  if ([name isEqualToString:NSTextDidBeginEditingNotification])
    ++begin_[phase_];
  else if ([name isEqualToString:NSTextDidChangeNotification])
  {
    if (phase_ == 1)
      LOKA_VERIFY(![(NSTextView *)[notification object] isEditable]);
    ++change_[phase_];
  }
  else
    ++undo_[phase_];
}
- (void)checkResult:(BOOL)accepted
{
  std::printf("bracket approval/commit/replay: begin %u/%u/%u, change %u/%u/%u, undo %u/%u/%u\n",
              begin_[0],
              begin_[1],
              begin_[2],
              change_[0],
              change_[1],
              change_[2],
              undo_[0],
              undo_[1],
              undo_[2]);
  LOKA_VERIFY(change_[0] == 0);
  if (accepted)
    LOKA_VERIFY(change_[1] > 0 && undo_[2] > 0);
  else
    // Replaying the test's empty group may notify the undo manager; no text change may follow it.
    LOKA_VERIFY(change_[1] == 0 && change_[2] == 0);
}
@end
@interface LokaStoreRejectEdit : NSObject
{
  NSUndoManager *manager_;
}
- (id)initWithManager:(NSUndoManager *)manager;
@end
@implementation LokaStoreRejectEdit
- (id)initWithManager:(NSUndoManager *)manager
{
  self = [super init];
  if (self)
    manager_ = manager;
  return self;
}
- (NSUndoManager *)undoManagerForTextView:(NSTextView *)view
{
  (void)view;
  return manager_;
}
- (BOOL)textView:(NSTextView *)view shouldChangeTextInRange:(NSRange)range replacementString:(NSString *)string
{
  (void)view;
  (void)range;
  (void)string;
  return NO;
}
@end

void testMacTextStoreBracket()
{
  Pool pool;
  MacScenePlatformController controller(0, RailMetrics());
  // The caret precedes each edited range; undo and redo selections are recorded native observations.
  NSString *inputs[] = {@"+", @"", @"d", @"reject"};
  NSString *results[] = {@"body+", @"boy", @"body", @"body"};
  const NSRange ranges[] = {NSMakeRange(4, 0), NSMakeRange(2, 1), NSMakeRange(2, 1), NSMakeRange(2, 1)};
  const NSRange undoSelections[] = {NSMakeRange(4, 0), NSMakeRange(2, 1), NSMakeRange(2, 1), NSMakeRange(0, 0)};
  const NSRange redoSelections[] = {NSMakeRange(5, 0), NSMakeRange(2, 0), NSMakeRange(3, 0), NSMakeRange(0, 0)};
  for (unsigned i = 0; i < 4; ++i)
  {
    Pool iteration;
    TextStoreReading reading;
    TextStoreRow &row = *birth(controller, replacement("body", TextPlacement::AtUtf8Offset(1)), reading);
    NSTextView *view = Access::view(row);
    NSUndoManager *undo = [view undoManager];
    id delegate = [view delegate];
    LokaStoreRejectEdit *reject = [[LokaStoreRejectEdit alloc] initWithManager:undo];
    LokaStoreBracketObserver *probe = [[LokaStoreBracketObserver alloc] init];
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    if (i == 3)
      [view setDelegate:(id)reject];
    [view setEditable:NO];
    [view setSelectedRange:NSMakeRange(1, 0)];
    [undo setGroupsByEvent:NO];
    [undo beginUndoGrouping];
    [center addObserver:probe selector:@selector(observe:) name:NSTextDidBeginEditingNotification object:view];
    [center addObserver:probe selector:@selector(observe:) name:NSTextDidChangeNotification object:view];
    [center addObserver:probe selector:@selector(observe:) name:nil object:undo];
    [view setEditable:YES];
    BOOL accepted = [view shouldChangeTextInRange:ranges[i] replacementString:inputs[i]];
    [view setEditable:NO];
    [probe setPhase:1];
    if (accepted)
    {
      [[view textStorage] replaceCharactersInRange:ranges[i] withString:inputs[i]];
      [view didChangeText];
    }
    [undo endUndoGrouping];
    LOKA_VERIFY(accepted == (i != 3) && ![view isEditable] && [[view string] isEqualToString:results[i]]);
    LOKA_VERIFY(NSEqualRanges([view selectedRange], NSMakeRange(1, 0)));
    // The test's own group stays on the stack even when empty, so canUndo is true in both cases.
    LOKA_VERIFY([undo canUndo]);
    if (accepted)
    {
      [probe setPhase:2];
      [view setEditable:YES];
      [undo undo];
      LOKA_VERIFY([[view string] isEqualToString:@"body"] && ![undo canUndo] && [undo canRedo]);
      // Observed on Tahoe 26.7.1: undo selects the restored range, redo collapses after the replacement.
      LOKA_VERIFY(NSEqualRanges([view selectedRange], undoSelections[i]));
      [undo redo];
      LOKA_VERIFY([[view string] isEqualToString:results[i]] && NSEqualRanges([view selectedRange], redoSelections[i]));
    }
    else
    {
      LOKA_VERIFY(!controller.peekTextStore(row));
      // A refused approval registers nothing: undoing the empty group changes neither body nor selection.
      [probe setPhase:2];
      [undo undo];
      LOKA_VERIFY([[view string] isEqualToString:@"body"] && NSEqualRanges([view selectedRange], NSMakeRange(1, 0))
                  && ![undo canUndo]);
    }
    [probe checkResult:accepted];
    [center removeObserver:probe];
    [probe release];
    [view setDelegate:delegate];
    [reject release];
    row.retire();
    controller.drainNativeRetirements();
  }
}

namespace
{
  // Capture the partial network on the *real* post-registration refusal path.
  class FailureController : public MacScenePlatformController
  {
  public:
    FailureController()
        : MacScenePlatformController(0, RailMetrics()),
          retired(0)
    {
      std::fill(this->deaths, this->deaths + (DELEGATE - STORAGE), 0);
    }
    unsigned retired;
    int deaths[DELEGATE - STORAGE];
    virtual void retireTextStore(TextStoreRow &row)
    {
      ++this->retired;
      Access::observePartialNetwork(row, this->deaths);
      MacScenePlatformController::retireTextStore(row);
    }
  };
} // namespace
void testMacTextStoreReplacement()
{
  Pool pool;
  FailureController controller;
  TextStoreReading reading;
  TextStoreRow &old = *birth(controller, replacement("old"), reading);
  NSTextView *view = Access::view(old);
  [view insertText:@"X" replacementRange:NSMakeRange(NSNotFound, 0)];
  [view setSelectedRange:NSMakeRange(1, 2)];
  NSUndoManager *undo = [view undoManager];
  LOKA_VERIFY([undo canUndo] && controller.peekTextStore(old));
  for (unsigned i = 0; i < STORE_ALLOCATION_COUNT; ++i)
  {
    failStoreAllocation(static_cast<StoreAllocation>(i), 2);
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      const unsigned retired = controller.retired;
      int expected[DELEGATE - STORAGE];
      for (unsigned stage = STORAGE; stage < DELEGATE; ++stage)
        expected[stage - STORAGE] = controller.deaths[stage - STORAGE] + (i > stage);
      {
        Pool partial;
        TextStoreSource source(replacement("fresh"));
        TextStoreRow *fresh = &old;
        LOKA_VERIFY(controller.createTextStore(source, &old, fresh, reading) == TEXT_STORE_REFUSED_ALLOCATION
                    && !fresh);
        LOKA_VERIFY(controller.retired == retired + (i >= STORAGE));
        LOKA_VERIFY(loka::app::testing::TextStoreTestAccess::retiredCount(old) == (i >= STORAGE ? 1u : 0u));
        controller.drainNativeRetirements();
      }
      // G6 allows AppKit to keep the view temporarily after cutting storage from layout.
      LOKA_VERIFY(controller.deaths[0] == expected[0]);
      LOKA_VERIFY(controller.deaths[LAYOUT - STORAGE] == expected[LAYOUT - STORAGE]);
      NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:2.0];
      while (controller.deaths[VIEW - STORAGE] < expected[VIEW - STORAGE] && [deadline timeIntervalSinceNow] > 0)
      {
        Pool turn;
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
      }
      for (unsigned stage = STORAGE; stage < DELEGATE; ++stage)
        LOKA_VERIFY(controller.deaths[stage - STORAGE] == expected[stage - STORAGE]);
      LOKA_VERIFY([[view string] isEqualToString:@"Xold"] && NSEqualRanges([view selectedRange], NSMakeRange(1, 2)));
      LOKA_VERIFY([undo canUndo] && controller.peekTextStore(old));
    }
  }
  TextStoreRow &fresh = *birth(controller, replacement("fresh"), reading, &old);
  LOKA_VERIFY([[Access::view(fresh) string] isEqualToString:@"fresh"] && ![[Access::view(fresh) undoManager] canUndo]);
  LOKA_VERIFY([Access::view(fresh) undoManager] != undo && !controller.peekTextStore(fresh));
  for (unsigned pending = 1;; --pending)
  {
    TextStoreSource splice(replacement("different"));
    LOKA_VERIFY(controller.applyTextSplice(old, 1, 3, splice, reading) == TEXT_SPLICE_UNAVAILABLE);
    LOKA_VERIFY([[view string] isEqualToString:@"Xold"] && NSEqualRanges([view selectedRange], NSMakeRange(1, 2)));
    LOKA_VERIFY(controller.peekTextStore(old) == (pending != 0) && [undo canUndo] && ![undo canRedo]);
    if (!pending)
      break;
    LOKA_VERIFY(controller.readTextStore(old, reading) && reading.changed());
  }
  [undo undo];
  LOKA_VERIFY([[view string] isEqualToString:@"old"] && ![undo canUndo]);
}
void testMacTextStoreRetirement()
{
  Pool outer;
  MacScenePlatformController controller(0, RailMetrics());
  int storageDeaths = 0, layoutDeaths = 0;
  {
    Pool reclaim;
    TextStoreReading reading;
    TextStoreRow &row = *birth(controller, replacement("body"), reading);
    NSTextView *view = Access::view(row);
    NSTextStorage *storage = Access::storage(row);
    observeMacNativeDeallocation(storage, storageDeaths);
    observeMacNativeDeallocation(Access::layout(row), layoutDeaths);
    id delegate = [[view delegate] retain];
    LOKA_VERIFY(delegate && [storage delegate] == delegate);
    observeStoreCalls(&Access::store(row));
    [view insertText:@"A" replacementRange:NSMakeRange(NSNotFound, 0)];
    LOKA_VERIFY(observedStoreCalls() > 0 && [delegate undoManagerForTextView:view]);
    LOKA_VERIFY(controller.readTextStore(row, reading) && reading.changed());
    NSUndoManager *undo = [[view undoManager] retain];
    observeStoreCalls(&Access::store(row)); // From retire entry through the post-retire edit.
    row.retire();
    row.retire();
    LOKA_VERIFY(![storage delegate] && ![view delegate]);
    LOKA_VERIFY(![delegate undoManagerForTextView:view]);
    [storage replaceCharactersInRange:NSMakeRange(0, 1) withString:@"B"];
    LOKA_VERIFY(!Access::pending(row) && observedStoreCalls() == 0);
    LOKA_VERIFY(!controller.readTextStore(row, reading) && !controller.applyTextRequest(row, 0));
    LOKA_VERIFY(controller.peekTextStore(row));
    LOKA_VERIFY(!storageDeaths && !layoutDeaths);
    // Hold storage across drain so the explicit edge cut is observable independently of deallocation.
    [storage retain];
    LOKA_VERIFY([undo canUndo]);
    observeStoreCalls(&Access::store(row)); // Drain and the following pool boundary.
    controller.drainNativeRetirements();
    LOKA_VERIFY([[storage layoutManagers] count] == 0 && ![undo canUndo]);
    [undo release];
    [storage release];
    [delegate release];
  }
  LOKA_VERIFY(storageDeaths == 1 && layoutDeaths == 1 && observedStoreCalls() == 0);
  observeStoreCalls(0);
}

// The controller releases this native ticket at the existing release loop. Both
// checks must already hold, even when the member destructor could clean up later.
@interface LokaStoreExitWitness : NSObject
{
  TextStoreLink *link_;
  NSTextStorage *storage_;
  unsigned *calls_;
}
- (id)initWithLink:(TextStoreLink *)link storage:(NSTextStorage *)storage calls:(unsigned *)calls;
@end
@implementation LokaStoreExitWitness
- (id)initWithLink:(TextStoreLink *)link storage:(NSTextStorage *)storage calls:(unsigned *)calls
{
  self = [super init];
  if (self)
  {
    link_ = link;
    storage_ = [storage retain];
    calls_ = calls;
  }
  return self;
}
- (void)dealloc
{
  LOKA_VERIFY(!link_->row() && ![storage_ delegate] && [[storage_ layoutManagers] count] == 0);
  ++*calls_;
  [storage_ release];
  [super dealloc];
}
@end
void testMacTextStoreExits()
{
  Pool outer;
  for (unsigned destroy = 0; destroy < 2; ++destroy)
  {
    TextStoreLink link;
    unsigned calls = 0;
    {
      MacScenePlatformController controller(0, RailMetrics());
      TextStoreReading reading;
      TextStoreRow &row = *birth(controller, replacement("open"), reading);
      row.connect(link);
      LOKA_VERIFY(link.row() == &row);
      LokaStoreExitWitness *ticket = [[LokaStoreExitWitness alloc] initWithLink:&link
                                                                        storage:Access::storage(row)
                                                                          calls:&calls];
      controller.queueNativeRetirement(ticket);
      if (destroy)
      {
        controller.destroy();
        controller.destroy();
        LOKA_VERIFY(calls == 1);
      }
    }
    LOKA_VERIFY(calls == 1 && !link.row());
  }
}
namespace
{
  unsigned rootDeaths = 0;
  class StoreRoot : public BoundaryNodeFor<StoreRoot>
  {
  public:
    TextDocument text;
    explicit StoreRoot(const BoundaryPropsFor<StoreRoot> &props)
        : BoundaryNodeFor<StoreRoot>(props)
    {
      this->document(this->text);
    }
    virtual void composeNode(NodeComposition &) {}
    virtual ~StoreRoot()
    {
      const TextDocumentRegistration *registration = loka::app::testing::TextDocumentAccess::registration(this->text);
      LOKA_VERIFY(!registration || !registration->store());
      ++rootDeaths;
    }
  };
  class BirthController : public MacScenePlatformController
  {
  public:
    TextStoreReading birthReading;
    TextStoreRow *fresh;
    explicit BirthController(NSView *host)
        : MacScenePlatformController(host, RailMetrics()),
          fresh(0)
    {
    }
    virtual TextStoreBirth
    createTextStore(TextStoreSource &source, TextStoreRow *old, TextStoreRow *&out, TextStoreReading &reading)
    {
      const TextStoreBirth result = MacScenePlatformController::createTextStore(source, old, out, reading);
      if (result == TEXT_STORE_BORN)
      {
        this->birthReading = reading;
        this->fresh = out;
      }
      return result;
    }
  };
  class BorrowedWindow : public Window
  {
  public:
    BorrowedWindow(PlatformContext &context, BirthController &controller)
        : Window(&context, WindowProps().scene(new Scene(Boundary<StoreRoot>())))
    {
      LOKA_VERIFY(this->scene()->mount(&controller));
      loka::dsl::testing::SceneTestAccess::updateAttached(*this->scene(), true);
    }
    virtual ~BorrowedWindow()
    {
      this->unmountSceneForTeardown(*this->scene());
    }

  protected:
    virtual bool hasLiveScenePlatform() const
    {
      return true;
    }
  };
  class CloseApp : public MacApp
  {
  public:
    CloseApp()
        : MacApp(0)
    {
    }
    virtual void quit() {}
    void install(MacWindow *window)
    {
      this->group_ = new AppComponentGroup(std::vector<AppComponent *>(1, window));
      window->setApp(this);
    }
  };
} // namespace
void testMacTextStoreConformance()
{
  Pool pool;
  NSView *host = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 300, 200)] autorelease];
  BirthController controller(host);
  NullPlatformContext context;
  BorrowedWindow window(context, controller);
  WindowAdmissionTestApp app(window);
  app.flush();
  app.reconcileAtCompletion();
  StoreRoot &root = *static_cast<StoreRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*window.scene()));
  LOKA_VERIFY(root.text.fact()->get().state() == TEXT_DOCUMENT_READY && controller.fresh);
  const std::string logical = "a\n\xf0\x9f\x98\x80";
  loka::app::testing::checkTextStoreConformance(
      controller, app, root.text, *controller.fresh, replacement(logical, TextPlacement::End()), 4, 4);
  // Known gap, shared with the Win32 parking ledger: a replacement retires the old row at the
  // outer completion, after this turn's drain, and hasPendingSync() does not count retired rows,
  // so an idle window keeps it until other work admits the window (instrumented on macOS 26.7.1:
  // two further App flushes left it retired). The retired-row clock gets its own design page;
  // until then the pin drains at the controller's safe point directly.
  controller.drainNativeRetirements();
  LOKA_VERIFY(loka::app::testing::TextStoreTestAccess::retiredCount(*controller.fresh) == 0);
  const unsigned long revision = root.text.fact()->get().textRevision();
  {
    TextReader reader(root.text);
    char scratch[8];
    unsigned long n = 0;
    LOKA_VERIFY(reader.read(scratch, sizeof scratch, n) == TEXT_READ_MORE && std::string(scratch, n) == logical);
    LOKA_VERIFY(reader.read(scratch, sizeof scratch, n) == TEXT_READ_END && !n);
  }
  NSTextView *view = Access::view(*controller.fresh);
  [view setSelectedRange:NSMakeRange(1, 0)];
  app.reconcileAtCompletion();
  LOKA_VERIFY(root.text.fact()->get().textRevision() == revision);
  TextPosition caret;
  LOKA_VERIFY(root.text.queryPositionAtUtf8(1, caret) && root.text.fact()->get().caret() == caret);
  [view setMarkedText:@"k" selectedRange:NSMakeRange(1, 0) replacementRange:NSMakeRange(NSNotFound, 0)];
  app.reconcileAtCompletion();
  LOKA_VERIFY(root.text.fact()->get().state() == TEXT_DOCUMENT_MARKED);
  LOKA_VERIFY(app.commitTextInput() == TEXT_INPUT_SETTLED);
  LOKA_VERIFY(root.text.fact()->get().state() == TEXT_DOCUMENT_READY
              && root.text.fact()->get().textRevision() > revision);
}
void testMacTextStoreWindowClose()
{
  Pool pool;
  NullPlatformContext context;
  CloseApp app;
  const unsigned before = rootDeaths;
  MacWindow *window = new MacWindow(&context, WindowProps().scene(new Scene(Boundary<StoreRoot>())));
  app.install(window);
  app.reconcileAtCompletion();
  StoreRoot &root = *static_cast<StoreRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(*window->scene()));
  const TextDocumentRegistration *registration = loka::app::testing::TextDocumentAccess::registration(root.text);
  LOKA_VERIFY(registration && registration->store() && root.text.fact()->get().state() == TEXT_DOCUMENT_READY);
  NSTextView *view = Access::view(*registration->store());
  LOKA_VERIFY(![view superview] && ![view window]);
  NSWindow *native = (NSWindow *)loka::dsl::testing::MacWindowTestAccess::nativeWindow(*window);
  LOKA_VERIFY(native);
  [native close];
  loka::app::testing::AppTestAccess::flushWindowInvalidations(app);
  LOKA_VERIFY(rootDeaths == before + 1);
}
