#include "MacPlatformContext.hpp"
#include "support/TextBufferFixture.hpp"
#include "support/TextAlphabetCases.hpp"
#include "MacInputDoor.hpp"
#include "testing/core/StateTrackerTestAccess.hpp"
#include "support/TextEditorStateOwner.hpp"
#include "support/TextEditorAccess.hpp"
#include "MacTextEditorTests.hpp"
#include "context/MacTextEditorContext.hpp"
#include "MacScenePlatformController.hpp"
#include "MacObjCCompat.hpp"
#include "platform/MacNativeGeometry.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "support/MacRetirementProbe.hpp"
#include "support/LokaAllocFailure.hpp"
#include "platform/StringUTF8.hpp"
#include "platform/String.hpp"
#include <vector>
#include <algorithm>
#include <cstdio>

namespace loka
{
  namespace testing
  {
    class MacTextEditorAccess
    {
    public:
      static loka::app::EditorResult
      observe(MacTextEditorContext &context,
              std::size_t &offset,
              MacTextEditorContext::TextObservation origin = MacTextEditorContext::VIEW_CHANGE)
      {
        return context.applyNativeChange(origin, offset);
      }
      static unsigned restores(const MacTextEditorContext &context)
      {
        return context.restores_;
      }
    };
  } // namespace testing
} // namespace loka

/** A native setter which refuses once; the production context must detect it. */
@interface LokaRefusingEditorView : NSTextView
{
  BOOL refuse_;
}
@property(nonatomic, assign) BOOL refuse;
@end
@implementation LokaRefusingEditorView
@synthesize refuse = refuse_;
- (void)setString:(NSString *)string
{
  if ([self refuse])
    [self setRefuse:NO];
  else
    [super setString:string];
}
@end

/** Counts the exact native door used by the rail, including equal selections. */
@interface LokaRequestEditorView : NSTextView
{
  NSUInteger selectionWrites_;
  NSArray *selectionWriteStack_;
}
@property(nonatomic, assign) NSUInteger selectionWrites;
@property(nonatomic, retain) NSArray *selectionWriteStack;
@end
@implementation LokaRequestEditorView
@synthesize selectionWrites = selectionWrites_;
@synthesize selectionWriteStack = selectionWriteStack_;
- (void)setSelectedRange:(NSRange)range
{
  // Count only writes the rail or the test itself issued. AppKit's own text
  // system also calls this setter to fix the selection after a storage edit
  // (NSTextLayoutManager _fixSelectionAfterChangeInCharacterRange, measured on
  // hosted CI for #878); that write is not the rail's and must not be pinned.
  NSArray *stack = [NSThread callStackSymbols];
  NSString *caller = [stack count] > 1 ? [stack objectAtIndex:1] : @"";
  const BOOL fromTextSystem =
      [caller rangeOfString:@"UIFoundation"].location != NSNotFound || [caller rangeOfString:@"AppKit"].location != NSNotFound;
  if (!fromTextSystem)
  {
    [self setSelectionWrites:[self selectionWrites] + 1];
    [self setSelectionWriteStack:stack];
  }
  [super setSelectedRange:range];
}
- (void)dealloc
{
  [self setSelectionWriteStack:nil];
  [super dealloc];
}
@end

/** Discard the active command binding at the native selection callback seam. */
@interface LokaCommandRebindingEditorView : LokaRequestEditorView
{
  loka::app::TextEditorNode *rebindNode_;
}
@property(nonatomic, assign) loka::app::TextEditorNode *rebindNode;
@end
@implementation LokaCommandRebindingEditorView
@synthesize rebindNode = rebindNode_;
- (void)setSelectedRange:(NSRange)range
{
  [super setSelectedRange:range];
  loka::app::TextEditorNode *node = [self rebindNode];
  [self setRebindNode:0];
  if (node)
  {
    loka::app::TextEditorProps next = node->props;
    next.command_ = loka::app::scene::RequestBinding<loka::app::EditorCommand>();
    LOKA_VERIFY((loka::app::scene::NodePropsApplier<loka::app::TextEditorNode, loka::app::TextEditorProps>::apply(
        node, next)));
  }
}
@end

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  typedef loka::testing::MacTextEditorAccess Access;

  void verifyFont(NSFont *actual, NSFont *expected)
  {
    LOKA_VERIFY(actual != nil && expected != nil);
    LOKA_VERIFY([[actual fontName] isEqualToString:[expected fontName]]);
    LOKA_VERIFY([actual pointSize] == [expected pointSize]);
  }

  // Unstyled editor text keeps the system text color so it follows the
  // appearance; a missing attribute draws black (#1182).
  void verifyTextColor(NSColor *actual)
  {
    LOKA_VERIFY(actual != nil && [actual isEqual:[NSColor textColor]]);
  }

  struct NativeHost
  {
    NSAutoreleasePool *pool;
    NSWindow *window;
    NativeHost()
        : pool([[NSAutoreleasePool alloc] init]),
          window(nil)
    {
      [NSApplication sharedApplication];
      this->window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 320, 200)
                                                 styleMask:LOKA_MAC_WINDOW_STYLE_TITLED
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
      [this->window setReleasedWhenClosed:NO];
    }
    ~NativeHost()
    {
      [this->window close];
      [this->window release];
      [this->pool drain];
    }
  };
  /** One UTF-16 unit the open alphabet still refuses: a lone high surrogate, refused before encoding. */
  NSString *loneSurrogate()
  {
    const unichar unit = 0xD800;
    return [NSString stringWithCharacters:&unit length:1];
  }
  struct Fixture : loka::app::testing::TextEditorStateOwner
  {
    NativeHost host;
    loka::testing::TextBufferFixture lines;
    MacScenePlatformController controller;
    TextEditorNode node;
    MacTextEditorContext *context;
    NSScrollView *scroll;
    NSTextView *view;
    explicit Fixture(unsigned short count = 3, const std::string &text = "abcd", std::size_t capacity = 8192)
        : host(),
          lines(),
          controller([host.window contentView], RailMetrics()),
          node(TextEditorProps(lines, cursor).moveCaretTo(request)),
          context(0),
          scroll(nil),
          view(nil)
    {
      LOKA_VERIFY(this->lines.attach(&this->tracker, 256, capacity) == ATTACH_OK);
      for (unsigned short i = 0; i < count; ++i)
        LOKA_VERIFY(this->lines.insert(i, String(text)) == EDIT_OK);
      // The initial caret is a fact the seam publishes, not an app request:
      // this rail delivers requests only from PR 4 of #873 onward.
      // Over-capacity fixtures keep the seam's refusal; every other fixture seeds.
      if (count)
      {
        const EditorResult seeded = loka::app::testing::TextEditorAccess::document(this->node).moveCaret(
            LineCursor(this->lines.at(0).id, std::min(2, static_cast<int>(text.size()))));
        LOKA_VERIFY(seeded == EDITOR_OK || seeded == EDITOR_CAPACITY);
      }
      LayoutState state;
      state.x = 3;
      state.y = 5;
      state.width = 240;
      state.height = 96;
      state.spacing = 4;
      LOKA_VERIFY(this->controller.prepareProjectedLayout(&this->node, state));
      this->context = static_cast<MacTextEditorContext *>(this->node.getContext());
      LOKA_VERIFY(this->context);
      this->scroll = (NSScrollView *)[[[this->host.window contentView] subviews] objectAtIndex:0];
      this->view = (NSTextView *)[this->scroll documentView];
      LOKA_VERIFY(![this->view isRichText]);
      this->verifyPlainAttributes();
      LOKA_VERIFY(this->context->layout(&this->controller, state) == 101);
      LOKA_VERIFY(state.height == 96);
      LOKA_VERIFY(
          NSEqualRects([this->scroll frame], this->controller.projection().projectFrame(Frame(3, 5, 240, 96)).r));
      [this->host.window makeFirstResponder:this->view];
    }
    ~Fixture()
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&this->node);
      LifecycleFactTestAccess::DeliverFacts(&this->node);
    }
    void verifyPlainAttributes()
    {
      NSFont *font = (NSFont *)this->controller.textFont(TextStyle(), true);
      verifyFont([this->view font], font);
      verifyFont([[this->view typingAttributes] objectForKey:NSFontAttributeName], font);
      verifyTextColor([[this->view typingAttributes] objectForKey:NSForegroundColorAttributeName]);
      NSTextStorage *storage = [this->view textStorage];
      for (NSUInteger i = 0; i < [storage length]; ++i)
      {
        verifyFont([storage attribute:NSFontAttributeName atIndex:i effectiveRange:0], font);
        verifyTextColor([storage attribute:NSForegroundColorAttributeName atIndex:i effectiveRange:0]);
      }
    }
    void notify()
    {
      [[this->view delegate] textDidChange:[NSNotification notificationWithName:NSTextDidChangeNotification
                                                                         object:this->view]];
    }
    void edit(NSString *text, NSUInteger caret)
    {
      MacInputDoor::textEditorCapture(*this->context);
      // Model a local native edit rather than a whole-document replacement.
      NSString *before = [this->view string];
      NSUInteger first = 0, oldEnd = [before length], newEnd = [text length];
      while (first < oldEnd && first < newEnd && [before characterAtIndex:first] == [text characterAtIndex:first])
        ++first;
      while (oldEnd > first && newEnd > first
             && [before characterAtIndex:oldEnd - 1] == [text characterAtIndex:newEnd - 1])
      {
        --oldEnd;
        --newEnd;
      }
      [[this->view textStorage] replaceCharactersInRange:NSMakeRange(first, oldEnd - first)
                                            withString:[text substringWithRange:NSMakeRange(first, newEnd - first)]];
      // The view publishes its final selection after storage processing.
      [this->view setSelectedRange:NSMakeRange(caret, 0)];
      this->notify();
    }
    void turn()
    {
      // A turn is 10 ms of run loop, not one handled source: Mac OS X 10.9
      // returns from runMode:beforeDate: after each source, about 1 ms apart,
      // and fired a zero-delay perform 37 ms after arming it, so a bounded
      // count of single calls ran out before the rail's deferred entry.
      NSDate *until = [NSDate dateWithTimeIntervalSinceNow:0.01];
      while ([until timeIntervalSinceNow] > 0)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:until];
    }
    void restored(unsigned count)
    {
      // This assertion also proves restore did not run inside textDidChange.
      LOKA_VERIFY(Access::restores(*this->context) == count - 1);
      for (int i = 0; i < 20 && Access::restores(*this->context) < count; ++i)
        this->turn();
      LOKA_VERIFY(Access::restores(*this->context) == count);
      std::string bytes;
      LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(this->node).project(bytes) == EDITOR_OK);
      std::replace(bytes.begin(), bytes.end(), '\r', '\n');
      NSString *expected = [[[NSString alloc] initWithBytes:bytes.data()
                                                     length:bytes.size()
                                                   encoding:NSUTF8StringEncoding] autorelease];
      LOKA_VERIFY([[this->view string] isEqualToString:expected]);
      LOKA_VERIFY([[this->view string] length] == [expected length]);
      LOKA_VERIFY([this->view isEditable]);
      this->verifyPlainAttributes();
    }
  };
  struct Snapshot
  {
    std::vector<ItemId> ids;
    std::vector<String> text;
    ListRevision revision;
    LineCursor cursor;
    explicit Snapshot(const Fixture &f)
        : revision(f.lines.revision().get()),
          cursor(f.cursor.state()->get())
    {
      for (unsigned short i = 0; i < f.lines.size(); ++i)
      {
        this->ids.push_back(f.lines.at(i).id);
        this->text.push_back(f.lines.at(i).value);
      }
    }
    void unchanged(const Fixture &f) const
    {
      LOKA_VERIFY(this->ids.size() == f.lines.size());
      for (unsigned short i = 0; i < f.lines.size(); ++i)
      {
        LOKA_VERIFY(this->ids[i] == f.lines.at(i).id);
        LOKA_VERIFY(this->text[i].equals(f.lines.at(i).value));
      }
      LOKA_VERIFY(!(this->revision != f.lines.revision().get()));
      LOKA_VERIFY(this->cursor == f.cursor.state()->get());
    }
  };
  struct Observer
  {
    Fixture &fixture;
    unsigned calls;
    bool nested;
    bool deferred;
    Observer(Fixture &f)
        : fixture(f),
          calls(0),
          nested(false),
          deferred(false)
    {
      const_cast<State<ListRevision> &>(f.lines.revision()).bind(&changed, this, false);
    }
    ~Observer()
    {
      const_cast<State<ListRevision> &>(this->fixture.lines.revision()).unbind(&changed, this);
    }
    static void reenter(void *data)
    {
      Observer &self = *static_cast<Observer *>(data);
      self.fixture.edit(@"abxZcd\nabcd\nabcd", 4);
    }
    static void changed(void *data)
    {
      Observer &self = *static_cast<Observer *>(data);
      ++self.calls;
      if (self.nested)
      {
        LOKA_VERIFY(self.fixture.controller.borrowPhase().open());
        self.nested = false;
        if (self.deferred)
          loka::core::testing::PushStateTrackerTestAccess::defer(self.fixture.tracker, &reenter, &self);
        else
          reenter(&self);
      }
    }
  };
  struct CaretObserver
  {
    Fixture &fixture;
    bool nested;
    unsigned calls;
    explicit CaretObserver(Fixture &f)
        : fixture(f),
          nested(true),
          calls(0)
    {
      f.cursor.state()->bind(&changed, this, false);
    }
    ~CaretObserver()
    {
      this->fixture.cursor.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      CaretObserver &self = *static_cast<CaretObserver *>(data);
      ++self.calls;
      if (self.nested)
      {
        LOKA_VERIFY(self.fixture.controller.borrowPhase().open());
        self.nested = false;
        [self.fixture.view setSelectedRange:NSMakeRange(8, 0)];
        [[self.fixture.view delegate]
            textViewDidChangeSelection:[NSNotification notificationWithName:NSTextViewDidChangeSelectionNotification
                                                                     object:self.fixture.view]];
      }
    }
  };
  std::string bytes(const String &value)
  {
    std::string out;
    LOKA_VERIFY(loka::platform::CollectUtf8(value, out));
    return out;
  }
  void printEditState(Fixture &f, const char *stage)
  {
    const NSRange selection = [f.view selectedRange];
    const LineCursor cursor = f.cursor.state()->get();
    fprintf(stderr,
            "[MacTextEditor %s] model-line0=\"%s\" selectedRange=(%lu,%lu) "
            "cursor=(id=%u:%u,line-index=%d,column=%d)\n",
            stage, bytes(f.lines.at(0).value).c_str(),
            static_cast<unsigned long>(selection.location), static_cast<unsigned long>(selection.length),
            static_cast<unsigned>(cursor.line.generation), static_cast<unsigned>(cursor.line.seq),
            f.lines.find(cursor.line), cursor.column);
  }
  void printUndoState(Fixture &f, NSUndoManager *undo, const char *stage)
  {
    printEditState(f, stage);
    NSString *line = [[[f.view string] componentsSeparatedByString:@"\n"] objectAtIndex:0];
    fprintf(stderr,
            "[MacTextEditor undo %s] groupingLevel=%ld canUndo=%d native-line0=\"%s\" "
            "model-line0=\"%s\" restores=%u status(availability)=%d editable=%d\n",
            stage,
            static_cast<long>([undo groupingLevel]),
            static_cast<int>([undo canUndo]),
            [line UTF8String],
            bytes(f.lines.at(0).value).c_str(),
            Access::restores(*f.context),
            static_cast<int>(loka::app::testing::TextEditorAccess::document(f.node).availability()),
            static_cast<int>([f.view isEditable]));
  }
  void nextKey(Fixture &f)
  {
    const NSRange selection = [f.view selectedRange];
    NSMutableString *text = [[[f.view string] mutableCopy] autorelease];
    [text replaceCharactersInRange:selection withString:@"y"];
    const ListRevision before = f.lines.revision().get();
    f.edit(text, selection.location + 1);
    LOKA_VERIFY(f.lines.revision().get().content == before.content + 1);
    LOKA_VERIFY([f.view isEditable]);
  }

  void printSelectionWriteMismatch(LokaRequestEditorView *view, NSUInteger expected, const char *stage)
  {
    const NSUInteger actual = [view selectionWrites];
    if (actual == expected)
      return;
    const char *stack = [[[view selectionWriteStack] description] UTF8String];
    fprintf(stderr, "[MacTextEditor %s] selectionWrites expected=%lu actual=%lu; last setSelectedRange: stack:\n%s\n",
            stage, static_cast<unsigned long>(expected), static_cast<unsigned long>(actual),
            stack ? stack : "(no captured stack)");
  }

  LokaRequestEditorView *instrumentSelection(Fixture &f, bool commandRebind = false)
  {
    LokaRequestEditorView *view = commandRebind
                                    ? [[LokaCommandRebindingEditorView alloc] initWithFrame:[f.view frame]]
                                    : [[LokaRequestEditorView alloc] initWithFrame:[f.view frame]];
    [view setString:[f.view string]];
    [view setSelectedRange:[f.view selectedRange]];
    [view setDelegate:[f.view delegate]];
    [[view textStorage] setDelegate:[[f.view textStorage] delegate]];
    [f.view setDelegate:nil];
    [[f.view textStorage] setDelegate:nil];
    [f.scroll setDocumentView:view];
    f.view = view;
    [view release];
    [f.host.window makeFirstResponder:view];
    const NSUInteger before = [view selectionWrites];
    [view setSelectedRange:[view selectedRange]];
    printSelectionWriteMismatch(view, before + 1, "setter positive control");
    LOKA_VERIFY([view selectionWrites] == before + 1); // Positive control.
    LOKA_VERIFY([[view selectionWriteStack] count] > 0);
    return view;
  }

  /** Refuse one string read after the native request write, so the real seam's
      report fails allocation after validation and apply have already succeeded.
      The fixture owns the borrowed view for the entire string lifetime. */
  class RefusedReportString : public loka::platform::String
  {
  public:
    explicit RefusedReportString(LokaRequestEditorView *view)
        : view_(view),
          writesBefore_([view selectionWrites]),
          refusals_(0)
    {
    }
    virtual bool appendUtf8(std::string &out) const
    {
      if (!this->refusals_ && [this->view_ selectionWrites] > this->writesBefore_)
      {
        ++this->refusals_;
        return false;
      }
      out += "abcd";
      return true;
    }
    unsigned refusals() const
    {
      return this->refusals_;
    }

  private:
    LokaRequestEditorView *const view_;
    const NSUInteger writesBefore_;
    mutable unsigned refusals_;
  };

  void requestCaret(Fixture &f, LineCursor cursor)
  {
    StateTrackerGuard guard(&f.tracker);
    f.request.set(cursor);
  }

  /** Model synchronous owner props delivery while the rail reports/takes. */
  struct RequestObserver
  {
    Fixture &fixture;
    LokaRequestEditorView *view;
    State<LineCursor> *source;
    LineCursor repost;
    explicit RequestObserver(Fixture &f, LokaRequestEditorView *native, LineCursor next, bool onTake = false)
        : fixture(f), view(native), source(onTake ? f.request.state() : f.cursor.state()), repost(next)
    {
      this->source->bind(&changed, this, false);
    }
    ~RequestObserver()
    {
      this->source->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      RequestObserver &self = *static_cast<RequestObserver *>(data);
      const LineCursor next = self.repost;
      self.repost = LineCursor::None();
      if (!next.isNone())
        requestCaret(self.fixture, next);
      const NSUInteger before = [self.view selectionWrites];
      self.fixture.context->onPropsApplied();
      printSelectionWriteMismatch(self.view, before, "report/take observer props completion");
      LOKA_VERIFY([self.view selectionWrites] == before);
    }
  };

  struct RepeatingRequestObserver
  {
    Fixture &fixture;
    unsigned calls;
    explicit RepeatingRequestObserver(Fixture &f) : fixture(f), calls(0)
    {
      f.cursor.state()->bind(&changed, this, false);
    }
    ~RepeatingRequestObserver()
    {
      this->fixture.cursor.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      RepeatingRequestObserver &self = *static_cast<RepeatingRequestObserver *>(data);
      ++self.calls;
      // Fail promptly if an unbounded epilogue is restored by a mutation.
      LOKA_VERIFY(self.calls <= 6);
      requestCaret(self.fixture, LineCursor(self.fixture.lines.at(0).id,
                                           self.fixture.cursor.state()->get().column == 0 ? 1 : 0));
      self.fixture.context->onPropsApplied();
    }
  };

  struct RepairRequestObserver
  {
    Fixture &fixture;
    LineCursor repost;
    unsigned reports;
    explicit RepairRequestObserver(Fixture &f)
        : fixture(f), repost(f.lines.at(1).id, 3), reports(0)
    {
      f.request.state()->bind(&taken, this, false);
      f.cursor.state()->bind(&reported, this, false);
    }
    ~RepairRequestObserver()
    {
      this->fixture.request.state()->unbind(&taken, this);
      this->fixture.cursor.state()->unbind(&reported, this);
    }
    static void taken(void *data)
    {
      RepairRequestObserver &self = *static_cast<RepairRequestObserver *>(data);
      if (self.repost.isNone())
        return;
      const LineCursor next = self.repost;
      self.repost = LineCursor::None();
      LOKA_VERIFY(self.fixture.lines.update(self.fixture.lines.at(0).id, String("abcdefgh")) == EDIT_OK);
      requestCaret(self.fixture, next);
      self.fixture.context->onPropsApplied();
    }
    static void reported(void *data)
    {
      RepairRequestObserver &self = *static_cast<RepairRequestObserver *>(data);
      ++self.reports;
      LOKA_VERIFY(self.reports <= 2);
      LOKA_VERIFY(self.fixture.cursor.state()->get()
                  == LineCursor(self.fixture.lines.at(1).id, self.reports == 1 ? 1 : 3));
    }
  };
} // namespace

void testMacTextEditorQueuedRequests()
{
  // Deliberate Win32/Mac twin: one ordinary props completion per assertion group.
  typedef loka::app::testing::SettleTrace<LineCursor> Trace;
  typedef loka::app::scene::Reply<LineCursor> CaretReply;
  Fixture fixture;
  // Bare fixture: apply through the Props applier (the Definition door compares
  // Props type identity, which a prebuilt core library does not share on this rail).
  LOKA_VERIFY((loka::app::scene::NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
      &fixture.node, TextEditor(fixture.lines, fixture.cursor).moveCaretTo(fixture.queue).props)));
  fixture.context->onPropsApplied();
  Trace &trace = Trace::instance();
  const LineCursor first(fixture.lines.at(0).id, 0);
  const LineCursor second(fixture.lines.at(1).id, 1);
  const LineCursor third(fixture.lines.at(2).id, 3);
  for (int posts = 2; posts <= 3; ++posts)
  {
    const LineCursor before = fixture.cursor.state()->get();
    trace.clear();
    {
      StateTrackerGuard guard(&fixture.tracker);
      LOKA_VERIFY(fixture.queue.post(first) == POST_ACCEPTED);
      LOKA_VERIFY(fixture.queue.post(second) == POST_ACCEPTED);
      if (posts == 3)
        LOKA_VERIFY(fixture.queue.post(third) == POST_ACCEPTED);
    }
    fixture.context->onPropsApplied();
    LOKA_VERIFY(trace.size() == 1 && trace.overwritten() == 0);
    LOKA_VERIFY(trace.at(0).stimulus == SETTLE_PROPS && trace.at(0).count == 2);
    LOKA_VERIFY(trace.at(0).before == before && trace.at(0).after == second);
    for (unsigned i = 0; i < 2; ++i)
    {
      const LineCursor expected = i == 0 ? first : second;
      LOKA_VERIFY(trace.at(0).takes[i].kind() == CaretReply::GRANTED);
      LOKA_VERIFY(trace.at(0).takes[i].requested() == expected && trace.at(0).takes[i].applied() == expected);
      LOKA_VERIFY(trace.at(0).seam[i] == EDITOR_OK);
    }
    const CaretReply reply = fixture.queue.reply().state()->get();
    LOKA_VERIFY(reply.kind() == CaretReply::GRANTED && reply.requested() == second && reply.applied() == second);
    LOKA_VERIFY(fixture.cursor.state()->get() == second && fixture.queue.pending() == 0);
    if (posts == 2)
      LOKA_VERIFY(fixture.queue.state()->get().isNone());
    else
    {
      // pending() counts only the ring: the third request remains in the live slot.
      LOKA_VERIFY(fixture.queue.state()->get() == third);
      trace.clear();
      fixture.context->onPropsApplied();
      LOKA_VERIFY(trace.size() == 1 && trace.at(0).count == 1 && trace.at(0).stimulus == SETTLE_PROPS);
      LOKA_VERIFY(trace.at(0).before == second && trace.at(0).after == third);
      LOKA_VERIFY(trace.at(0).takes[0].kind() == CaretReply::GRANTED && trace.at(0).seam[0] == EDITOR_OK);
      LOKA_VERIFY(trace.at(0).takes[0].requested() == third && trace.at(0).takes[0].applied() == third);
      const CaretReply last = fixture.queue.reply().state()->get();
      LOKA_VERIFY(last.kind() == CaretReply::GRANTED && last.requested() == third && last.applied() == third);
      LOKA_VERIFY(fixture.cursor.state()->get() == third);
      LOKA_VERIFY(fixture.queue.pending() == 0 && fixture.queue.state()->get().isNone());
    }
  }
}

namespace
{
  struct PageObserver
  {
    Fixture &fixture;
    unsigned reports;
    explicit PageObserver(Fixture &f) : fixture(f), reports(0)
    {
      f.cursor.state()->bind(&changed, this, false);
    }
    ~PageObserver()
    {
      this->fixture.cursor.state()->unbind(&changed, this);
    }
    static void changed(void *data)
    {
      PageObserver &self = *static_cast<PageObserver *>(data);
      ++self.reports;
      LOKA_VERIFY(self.reports <= 2);
      Fixture &f = self.fixture;
      const LineCursor fact = f.cursor.state()->get();
      // All rows in this fixture have four characters and one separator.
      LOKA_VERIFY([f.view selectedRange].location == static_cast<NSUInteger>(f.lines.find(fact.line) * 5 + fact.column));
      // A native notification during report must not open a nested take.
      [[f.view delegate] textViewDidChangeSelection:
          [NSNotification notificationWithName:NSTextViewDidChangeSelectionNotification object:f.view]];
    }
  };
}

void testMacTextEditorPageCommands()
{
  typedef loka::app::testing::SettleTrace<EditorCommand, LineCursor> Trace;
  typedef loka::app::scene::Reply<EditorCommand> CommandReply;
  Fixture f(80);
  LOKA_VERIFY((NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
      &f.node, TextEditor(f.lines, f.cursor).moveCaretTo(f.queue).command(f.commands).props)));
  f.context->onPropsApplied();
  PageObserver observer(f);
  const LineCursor before = f.cursor.state()->get();
  const NSRect viewport = [f.scroll documentVisibleRect];
  loka::app::testing::SettleTraceCapture<LineCursor>::clear();
  {
    StateTrackerGuard guard(&f.tracker);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
  }
  f.context->onPropsApplied();
  Trace &trace = Trace::instance();
  LOKA_VERIFY(trace.size() == 1 && trace.overwritten() == 0);
  LOKA_VERIFY(trace.at(0).count == 2 && trace.at(0).stimulus == SETTLE_PROPS);
  LOKA_VERIFY(trace.at(0).before == before && trace.at(0).after == f.cursor.state()->get());
  for (unsigned i = 0; i < 2; ++i)
  {
    LOKA_VERIFY(trace.at(0).takes[i].kind() == CommandReply::GRANTED);
    LOKA_VERIFY(trace.at(0).takes[i].requested().kind() == EditorCommand::PAGE_DOWN);
    LOKA_VERIFY(trace.at(0).seam[i] == EDITOR_OK);
  }
  LOKA_VERIFY(observer.reports == 2);
  LOKA_VERIFY(f.lines.find(f.cursor.state()->get().line) > 1);
  LOKA_VERIFY(f.commands.pending() == 0 && f.commands.state()->get().isNone());
  LOKA_VERIFY(NSMinY([f.scroll documentVisibleRect]) > NSMinY(viewport));
  char diagnostic[160];
  snprintf(diagnostic, sizeof(diagnostic), "[MacTextEditor pages] row=%d column=%d reports=%u\n",
           f.lines.find(f.cursor.state()->get().line), f.cursor.state()->get().column, observer.reports);
  fputs(diagnostic, stderr);
}

void testMacTextEditorPageBindingReplacement()
{
  typedef loka::app::testing::SettleTrace<EditorCommand, LineCursor> Trace;
  Fixture f(80);
  LokaCommandRebindingEditorView *view = (LokaCommandRebindingEditorView *)instrumentSelection(f, true);
  LOKA_VERIFY((NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
      &f.node, TextEditor(f.lines, f.cursor).moveCaretTo(f.queue).command(f.commands).props)));
  f.context->onPropsApplied();
  const LineCursor before = f.cursor.state()->get();
  [view setRebindNode:&f.node];
  loka::app::testing::SettleTraceCapture<LineCursor>::clear();
  {
    StateTrackerGuard guard(&f.tracker);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
  }
  f.context->onPropsApplied();
  LOKA_VERIFY([view rebindNode] == 0 && !f.node.props.command_.isValid());
  LOKA_VERIFY(Trace::instance().size() == 1 && Trace::instance().at(0).count == 1);
  LOKA_VERIFY(Trace::instance().at(0).takes[0].kind() == Reply<EditorCommand>::REFUSED);
  LOKA_VERIFY(Trace::instance().at(0).seam[0] == EDITOR_OWNER_MISMATCH);
  LOKA_VERIFY(f.cursor.state()->get() == before);
}

void testMacTextEditorPagePriority()
{
  typedef loka::app::testing::SettleTrace<EditorCommand, LineCursor> Trace;
  Fixture f(80);
  LOKA_VERIFY((NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
      &f.node, TextEditor(f.lines, f.cursor).moveCaretTo(f.queue).command(f.commands).props)));
  f.context->onPropsApplied();
  {
    StateTrackerGuard guard(&f.tracker);
    for (unsigned short row = 0; row < 3; ++row)
      LOKA_VERIFY(f.queue.post(LineCursor(f.lines.at(row).id, 1)) == POST_ACCEPTED);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
  }
  loka::app::testing::SettleTraceCapture<LineCursor>::clear();
  f.context->onPropsApplied();
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 1));
  LOKA_VERIFY(!f.queue.state()->get().isNone() && !f.commands.state()->get().isNone());
  LOKA_VERIFY(Trace::instance().size() == 0);
  f.context->onPropsApplied();
  LOKA_VERIFY(f.queue.state()->get().isNone() && f.commands.state()->get().isNone());
  LOKA_VERIFY(Trace::instance().size() == 1 && Trace::instance().at(0).count == 1);
  LOKA_VERIFY(f.commands.reply().state()->get().kind() == Reply<EditorCommand>::GRANTED);
  LOKA_VERIFY(f.lines.find(f.cursor.state()->get().line) > 2);
}

void testMacTextEditorPageEmptyLine()
{
  Fixture f(1, "");
  LOKA_VERIFY((NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
      &f.node, TextEditor(f.lines, f.cursor).command(f.commands).props)));
  f.context->onPropsApplied();
  const LineCursor before = f.cursor.state()->get();
  {
    StateTrackerGuard guard(&f.tracker);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
  }
  f.context->onPropsApplied();
  LOKA_VERIFY(f.commands.reply().state()->get().kind() == Reply<EditorCommand>::GRANTED);
  LOKA_VERIFY(f.cursor.state()->get() == before && f.commands.state()->get().isNone());
  LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(0, 0)));
}

void testMacTextEditorPageGeometryDeclinesWrappedLine()
{
  Fixture f(3, std::string(2000, 'w'));
  LOKA_VERIFY((NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
      &f.node, TextEditor(f.lines, f.cursor).command(f.commands).props)));
  f.context->onPropsApplied();
  // Force wrapping through public AppKit doors; no logical row fits in this viewport.
  [f.view setHorizontallyResizable:NO];
  [[f.view textContainer] setWidthTracksTextView:YES];
  [[f.view textContainer] setContainerSize:NSMakeSize([f.scroll contentSize].width, CGFLOAT_MAX)];
  const LineCursor before = f.cursor.state()->get();
  {
    StateTrackerGuard guard(&f.tracker);
    LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
  }
  f.context->onPropsApplied();
  const Reply<EditorCommand> reply = f.commands.reply().state()->get();
  LOKA_VERIFY(reply.kind() == Reply<EditorCommand>::REFUSED && reply.reason() == EDITOR_UNAVAILABLE);
  LOKA_VERIFY(f.cursor.state()->get() == before && f.commands.state()->get().isNone());
}

void testMacTextEditorRequests()
{
  // Refused-report restore: predicted red on the pre-fix macOS rail.
  {
    Fixture f;
    LokaRequestEditorView *view = instrumentSelection(f);
    // The replacement view needs the same undo opt-in as testMacTextEditorUndoLocation.
    [view setAllowsUndo:YES];
    RefusedReportString *probe = new RefusedReportString(view);
    const String text(loka::core::Managed<loka::platform::String>::Wrap(probe));
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, text) == EDIT_OK);
    const LineCursor fact = f.cursor.state()->get();
    const NSRange selection = [view selectedRange];
    const NSUInteger writes = [view selectionWrites];
    NSUndoManager *undo = [view undoManager];
    LOKA_VERIFY(undo != nil && [undo isUndoRegistrationEnabled]);
    [undo setGroupsByEvent:NO];
    [undo beginUndoGrouping];
    [[undo prepareWithInvocationTarget:view] setString:@"undo control"];
    [undo endUndoGrouping];
    LOKA_VERIFY([undo canUndo]);
    const LineCursor requested(f.lines.at(1).id, 1);
    requestCaret(f, requested);
    f.context->onPropsApplied();
    const loka::app::scene::Reply<LineCursor> reply = f.request.reply().state()->get();
    LOKA_VERIFY(probe->refusals() == 1); // Positive control: report reached the failing read.
    LOKA_VERIFY(reply.kind() == loka::app::scene::Reply<LineCursor>::REFUSED);
    LOKA_VERIFY(reply.reason() == EDITOR_ALLOCATION && reply.requested() == requested);
    LOKA_VERIFY(f.request.get().isNone() && f.cursor.state()->get() == fact);
    LOKA_VERIFY([view selectionWrites] == writes + 2); // Apply, then committed selection repair.
    LOKA_VERIFY(NSEqualRanges([view selectedRange], selection));
    LOKA_VERIFY(NSEqualRanges(selection, NSMakeRange(2, 0))); // Initial fact's storage offset.
    LOKA_VERIFY([undo canUndo]);                              // Selection repair must not replace text and clear undo.
    LOKA_VERIFY([[view string] isEqualToString:@"abcd\nabcd\nabcd"]);
  }
  // #882: trace/reply reds are predicted until measured on the Tahoe rig.
  {
    Fixture f;
    LokaRequestEditorView *view = instrumentSelection(f);
    typedef loka::app::testing::SettleTrace<LineCursor> Trace;
    typedef loka::app::scene::Reply<LineCursor> CaretReply;
    Trace &trace = Trace::instance();
    trace.clear();
    const NSUInteger writes = [view selectionWrites];
    const LineCursor initial = f.cursor.state()->get();
    f.context->onPropsApplied();
    LOKA_VERIFY(trace.size() == 0);
    printSelectionWriteMismatch(view, writes, "empty settle");
    LOKA_VERIFY([view selectionWrites] == writes);
    LOKA_VERIFY(f.request.reply().state()->get().kind() == CaretReply::NO_REPLY);

    const LineCursor requested(f.lines.at(1).id, 99);
    const LineCursor clamped(f.lines.at(1).id, 4);
    requestCaret(f, requested);
    f.context->onPropsApplied();
    LOKA_VERIFY(trace.size() == 1 && trace.at(0).count == 1);
    LOKA_VERIFY(trace.at(0).stimulus == loka::app::scene::SETTLE_PROPS);
    LOKA_VERIFY(trace.at(0).admission[0] == loka::app::scene::ADMISSION_TAKE);
    LOKA_VERIFY(trace.at(0).admission[1] == loka::app::scene::ADMISSION_EMPTY);
    LOKA_VERIFY(trace.at(0).before == initial && trace.at(0).after == clamped);
    LOKA_VERIFY(trace.at(0).seam[0] == EDITOR_OK);
    LOKA_VERIFY(trace.at(0).takes[0].kind() == CaretReply::CLAMPED);
    const CaretReply reply = f.request.reply().state()->get();
    LOKA_VERIFY(reply.kind() == CaretReply::CLAMPED);
    LOKA_VERIFY(reply.requested() == requested && reply.applied() == clamped);
    LOKA_VERIFY(f.cursor.state()->get() == clamped && f.request.get().isNone());
    LOKA_VERIFY([view selectionWrites] == writes + 1); // Setter positive control.

    trace.clear();
    const LineCursor stale(ItemId(999, 999), 0);
    requestCaret(f, stale);
    f.context->onPropsApplied();
    const CaretReply refused = f.request.reply().state()->get();
    LOKA_VERIFY(refused.kind() == CaretReply::REFUSED);
    LOKA_VERIFY(refused.reason() == EDITOR_STALE_ID && refused.requested() == stale);
    LOKA_VERIFY(f.cursor.state()->get() == clamped && f.request.get().isNone());
    LOKA_VERIFY([view selectionWrites] == writes + 1);
    LOKA_VERIFY(trace.size() == 1 && trace.at(0).count == 1);
    LOKA_VERIFY(trace.at(0).seam[0] == EDITOR_STALE_ID);
    LOKA_VERIFY(trace.at(0).before == clamped && trace.at(0).after == clamped);
    f.context->onPropsApplied();
    LOKA_VERIFY(trace.size() == 1 && [view selectionWrites] == writes + 1);
  }
  {
    Fixture f;
    LokaRequestEditorView *view = instrumentSelection(f);
    RequestObserver observer(f, view, LineCursor(f.lines.at(1).id, 1));
    typedef loka::app::testing::SettleTrace<LineCursor> Trace;
    Trace &trace = Trace::instance();
    trace.clear();
    id delegate = [view delegate];
    [view setDelegate:nil];
    const NSUInteger writes = [view selectionWrites];
    [[view textStorage] replaceCharactersInRange:NSMakeRange(2, 0) withString:@"x"];
    LOKA_VERIFY(trace.size() == 0); // No settlement inside processEditing.
    LOKA_VERIFY(!f.request.get().isNone() && [view selectionWrites] == writes);
    f.context->onPropsApplied();
    LOKA_VERIFY(trace.size() == 0 && !f.request.get().isNone());
    [NSObject cancelPreviousPerformRequestsWithTarget:delegate];
    [delegate performSelector:@selector(applyHighlights)];
    LOKA_VERIFY(trace.size() == 1 && trace.at(0).count == 1);
    LOKA_VERIFY(trace.at(0).stimulus == loka::app::scene::SETTLE_DEFERRED);
    LOKA_VERIFY(f.request.get().isNone());
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 1));
    LOKA_VERIFY([view selectionWrites] == writes + 1);
    [delegate performSelector:@selector(applyHighlights)];
    LOKA_VERIFY(trace.size() == 1 && [view selectionWrites] == writes + 1);
    [view setDelegate:delegate];
  }
  // New request-delivery assertions: predicted reds, not run on the macOS rig.
  {
    Fixture f;
    // The fixture seeds the fact through the seam; props deliver app requests.
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(2, 0)));
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 2));
    LOKA_VERIFY(f.request.get().isNone());
    requestCaret(f, LineCursor(f.lines.at(1).id, 99));
    f.context->onPropsApplied();
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(9, 0)));
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 4));
    LOKA_VERIFY(f.request.get().isNone());
    requestCaret(f, LineCursor(f.lines.at(1).id, -8));
    f.context->onPropsApplied();
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(5, 0)));
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 0));
    LOKA_VERIFY(f.request.get().isNone());
  }
  // Entry-bound mutations are predicted reds, not measured on macOS here.
  for (int stale = 0; stale < 2; ++stale)
  {
    Fixture f;
    RepeatingRequestObserver observer(f);
    {
      StateTrackerGuard guard(&f.tracker);
      if (stale)
        LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("abcdefgh")) == EDIT_OK);
      f.request.set(LineCursor(f.lines.at(0).id, 0));
    }
    f.context->onPropsApplied();
    LOKA_VERIFY(observer.calls == 2);
    LOKA_VERIFY(f.request.get() == LineCursor(f.lines.at(0).id, 0));
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(1, 0)));
    f.context->onPropsApplied();
    LOKA_VERIFY(observer.calls == 4);
    LOKA_VERIFY(!f.request.get().isNone());
  }
  {
    Fixture f;
    RepeatingRequestObserver observer(f);
    id delegate = [f.view delegate];
    [f.view setDelegate:nil];
    // Storage commits text and reports its caret together. The subscriber
    // posts a request during that report, before the deferred entry runs.
    [[f.view textStorage] replaceCharactersInRange:NSMakeRange(2, 0) withString:@"x"];
    LOKA_VERIFY(observer.calls == 1);
    LOKA_VERIFY(!f.request.get().isNone());
    // Run exactly the deferred entry, without another run-loop source.
    [NSObject cancelPreviousPerformRequestsWithTarget:delegate];
    [delegate performSelector:@selector(applyHighlights)];
    LOKA_VERIFY(observer.calls == 3);
    LOKA_VERIFY(f.request.get() == LineCursor(f.lines.at(0).id, 0));
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 1));
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(1, 0)));
    LOKA_VERIFY([[f.view string] isEqualToString:@"abxcd\nabcd\nabcd"]);
    f.context->onPropsApplied();
    LOKA_VERIFY(observer.calls == 5);
    LOKA_VERIFY(f.request.get() == LineCursor(f.lines.at(0).id, 0));
    [f.view setDelegate:delegate];
  }
  for (int onTake = 0; onTake < 2; ++onTake)
  {
    Fixture f;
    requestCaret(f, LineCursor::None());
    LokaRequestEditorView *view = instrumentSelection(f);
    requestCaret(f, LineCursor(f.lines.at(0).id, 1));
    RequestObserver observer(f, view, LineCursor(f.lines.at(1).id, 3), onTake != 0);
    f.context->onPropsApplied();
    LOKA_VERIFY(NSEqualRanges([view selectedRange], NSMakeRange(8, 0)));
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 3));
    LOKA_VERIFY(f.request.get().isNone());
  }
  {
    Fixture f;
    requestCaret(f, LineCursor(f.lines.at(1).id, 1));
    RepairRequestObserver observer(f);
    f.context->onPropsApplied();
    LOKA_VERIFY(observer.reports == 2);
    LOKA_VERIFY([[f.view string] isEqualToString:@"abcdefgh\nabcd\nabcd"]);
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(12, 0)));
    LOKA_VERIFY(f.request.get().isNone());
  }
  {
    Fixture f;
    requestCaret(f, LineCursor::None());
    LokaRequestEditorView *view = instrumentSelection(f);
    RequestObserver observer(f, view, LineCursor(f.lines.at(1).id, 1));
    // Selection's synchronous report spends props delivery before its tail.
    [view setSelectedRange:NSMakeRange(0, 0)];
    MacInputDoor::textEditorSelection(*f.context);
    LOKA_VERIFY(NSEqualRanges([view selectedRange], NSMakeRange(6, 0)));
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 1));
    LOKA_VERIFY(f.request.get().isNone());
  }
  for (int storageOnly = 0; storageOnly < 2; ++storageOnly)
  {
    Fixture f;
    requestCaret(f, LineCursor::None());
    LokaRequestEditorView *view = instrumentSelection(f);
    RequestObserver observer(f, view, LineCursor(f.lines.at(1).id, 1));
    id viewDelegate = [view delegate];
    id storageDelegate = [[view textStorage] delegate];
    if (storageOnly)
      [view setDelegate:nil];
    else
      [[view textStorage] setDelegate:nil];
    MacInputDoor::textEditorCapture(*f.context);
    const NSUInteger before = [view selectionWrites];
    [[view textStorage] replaceCharactersInRange:NSMakeRange(2, 0) withString:@"x"];
    if (storageOnly)
    {
      printSelectionWriteMismatch(view, before, "storage replace returned");
      LOKA_VERIFY([view selectionWrites] == before);
      LOKA_VERIFY(f.request.get() == LineCursor(f.lines.at(1).id, 1));
      f.context->onPropsApplied();
      printSelectionWriteMismatch(view, before, "storage-pending props completion");
      LOKA_VERIFY([view selectionWrites] == before);
      LOKA_VERIFY(!f.request.get().isNone());
      // Selection reporting is allowed, but cannot open request delivery yet.
      MacInputDoor::textEditorSelection(*f.context);
      LOKA_VERIFY(!f.request.get().isNone());
      for (int i = 0; i < 20 && !f.request.get().isNone(); ++i)
        f.turn();
    }
    else
    {
      [view setSelectedRange:NSMakeRange(3, 0)];
      f.notify();
    }
    [view setDelegate:viewDelegate];
    [[view textStorage] setDelegate:storageDelegate];
    LOKA_VERIFY(bytes(f.lines.at(0).value) == "abxcd");
    LOKA_VERIFY(NSEqualRanges([view selectedRange], NSMakeRange(7, 0)));
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 1));
    LOKA_VERIFY(f.request.get().isNone());
  }
  for (int staleModel = 0; staleModel < 2; ++staleModel)
  {
    Fixture f;
    LokaRequestEditorView *view = instrumentSelection(f);
    RequestObserver observer(f, view, LineCursor(f.lines.at(1).id, 1));
    id delegate = [view delegate];
    [view setDelegate:nil];
    [[view textStorage] replaceCharactersInRange:NSMakeRange(2, 0) withString:@"x"];
    LOKA_VERIFY(!f.request.get().isNone());
    if (staleModel)
      LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String(std::string(8193, 'a'))) == EDIT_OK);
    else
    {
      [[view textStorage] setDelegate:nil];
      [view setString:@"stale native"];
      [[view textStorage] setDelegate:delegate];
    }
    const LineCursor before = f.cursor.state()->get();
    const NSUInteger writes = [view selectionWrites];
    // Invoke only the queued completion, before its separately queued repair.
    [NSObject cancelPreviousPerformRequestsWithTarget:delegate];
    [delegate performSelector:@selector(applyHighlights)];
    LOKA_VERIFY(f.request.get().isNone());
    LOKA_VERIFY(f.cursor.state()->get() == before);
    printSelectionWriteMismatch(view, writes, "stale deferred completion");
    LOKA_VERIFY([view selectionWrites] == writes);
    [view setDelegate:delegate];
  }
  {
    Fixture f;
    const LineCursor before = f.cursor.state()->get();
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String(std::string(8193, 'a'))) == EDIT_OK);
    requestCaret(f, LineCursor(f.lines.at(1).id, 1));
    f.context->onPropsApplied();
    LOKA_VERIFY(![f.view isEditable]);
    LOKA_VERIFY(f.request.get().isNone());
    LOKA_VERIFY(f.cursor.state()->get() == before);
  }
}

void testMacTextEditorRequestReverse()
{
  // Predicted, not run: setter instrumentation discriminates report echo.
  Fixture f;
  requestCaret(f, LineCursor::None());
  LokaRequestEditorView *view = instrumentSelection(f);
  [view setSelectedRange:NSMakeRange(2, 0)];
  MacInputDoor::textEditorSelection(*f.context);
  RequestObserver observer(f, view, LineCursor::None());
  [view deleteBackward:nil];
  LOKA_VERIFY(bytes(f.lines.at(0).value) == "acd");
  LOKA_VERIFY(NSEqualRanges([view selectedRange], NSMakeRange(1, 0)));
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 1));
  const NSUInteger afterDelete = [view selectionWrites];
  f.context->onPropsApplied();
  printSelectionWriteMismatch(view, afterDelete, "post-delete props completion");
  LOKA_VERIFY([view selectionWrites] == afterDelete);
  for (NSUInteger offset = 2; offset <= 5; ++offset)
  {
    [view moveRight:nil];
    LOKA_VERIFY(NSEqualRanges([view selectedRange], NSMakeRange(offset, 0)));
    const LineCursor expected = offset < 4 ? LineCursor(f.lines.at(0).id, static_cast<int>(offset))
                                          : LineCursor(f.lines.at(1).id, static_cast<int>(offset - 4));
    LOKA_VERIFY(f.cursor.state()->get() == expected);
    const NSUInteger afterArrow = [view selectionWrites];
    f.context->onPropsApplied();
    printSelectionWriteMismatch(view, afterArrow, "post-arrow props completion");
    LOKA_VERIFY([view selectionWrites] == afterArrow);
  }
  LOKA_VERIFY(f.request.get().isNone());
}

void testMacTextEditorLineActions()
{
  // Programmatic storage edits must preserve identities at the mutation,
  // even when the published cursor is parked on the other identical line.
  for (int splitB = 0; splitB < 2; ++splitB)
  {
    Fixture edge(2, "a");
    const ItemId a = edge.lines.at(0).id, b = edge.lines.at(1).id;
    [edge.view setSelectedRange:NSMakeRange(splitB ? 1 : 2, 0)];
    MacInputDoor::textEditorSelection(*edge.context);
    LOKA_VERIFY(edge.cursor.state()->get() == (splitB ? LineCursor(a, 1) : LineCursor(b, 0)));
    id viewDelegate = [edge.view delegate];
    [edge.view setDelegate:nil];
    [[edge.view textStorage] replaceCharactersInRange:NSMakeRange(splitB ? 2 : 1, 0) withString:@"\n"];
    [edge.view setDelegate:viewDelegate];
    LOKA_VERIFY(Access::restores(*edge.context) == 0);
    LOKA_VERIFY(edge.lines.size() == 3);
    LOKA_VERIFY(edge.lines.at(0).id == a && bytes(edge.lines.at(0).value) == "a");
    LOKA_VERIFY(bytes(edge.lines.at(1).value).empty());
    LOKA_VERIFY(bytes(edge.lines.at(2).value) == "a");
    LOKA_VERIFY(edge.lines.at(splitB ? 1 : 2).id == b);
    const ItemId created = edge.lines.at(splitB ? 2 : 1).id;
    LOKA_VERIFY(created != a && created != b);
  }
  // Exercise each committing callback separately, with conflicting selections.
  for (int storageOnly = 0; storageOnly < 2; ++storageOnly)
  {
    for (int join = 0; join < 2; ++join)
    {
      Fixture edge(2, "a");
      const ItemId a = edge.lines.at(0).id, b = edge.lines.at(1).id;
      id viewDelegate = [edge.view delegate];
      id storageDelegate = [[edge.view textStorage] delegate];
      [edge.view setDelegate:nil];
      [edge.view setSelectedRange:NSMakeRange(2, 0)];
      [edge.view setDelegate:viewDelegate];
      if (storageOnly)
      {
        MacInputDoor::textEditorSelection(*edge.context);
        LOKA_VERIFY(edge.cursor.state()->get() == LineCursor(b, 0));
        [edge.view setDelegate:nil];
        [edge.view setSelectedRange:NSMakeRange(0, 0)];
        MacInputDoor::textEditorCapture(*edge.context);
      }
      else
      {
        // The view hint must come from native selection, not the older cursor.
        LOKA_VERIFY(edge.cursor.state()->get() == LineCursor(a, 1));
        [[edge.view textStorage] setDelegate:nil];
        MacInputDoor::textEditorCapture(*edge.context);
      }
      [[edge.view textStorage] replaceCharactersInRange:NSMakeRange(join ? 1 : 2, join ? 1 : 0)
                                            withString:join ? @"" : @"\n"];
      if (!storageOnly)
      {
        [edge.view setSelectedRange:NSMakeRange(join ? 1 : 3, 0)];
        edge.notify();
      }
      [edge.view setDelegate:viewDelegate];
      [[edge.view textStorage] setDelegate:storageDelegate];
      LOKA_VERIFY(Access::restores(*edge.context) == 0);
      LOKA_VERIFY(edge.lines.at(0).id == a);
      if (join)
      {
        LOKA_VERIFY(edge.lines.size() == 1 && bytes(edge.lines.at(0).value) == "aa");
        LOKA_VERIFY(edge.lines.find(b) == -1);
        LOKA_VERIFY(edge.cursor.state()->get() == LineCursor(a, 1));
      }
      else
      {
        LOKA_VERIFY(edge.lines.size() == 3 && bytes(edge.lines.at(0).value) == "a");
        LOKA_VERIFY(edge.lines.at(1).id == b && bytes(edge.lines.at(1).value).empty());
        LOKA_VERIFY(edge.lines.at(2).id != a && edge.lines.at(2).id != b);
        LOKA_VERIFY(bytes(edge.lines.at(2).value) == "a");
        LOKA_VERIFY(edge.cursor.state()->get() == LineCursor(edge.lines.at(2).id, 0));
      }
    }
  }
  Fixture f;
  Observer observer(f);
  const ItemId first = f.lines.at(0).id, second = f.lines.at(1).id;
  const String unchanged = f.lines.at(2).value;
  [f.view insertText:@"x" replacementRange:[f.view selectedRange]];
  const bool firstKeySucceeded = bytes(f.lines.at(0).value) == "abxcd"
                                 && f.cursor.state()->get() == LineCursor(first, 3) && observer.calls == 1
                                 && NSEqualRanges([f.view selectedRange], NSMakeRange(3, 0));
  if (!firstKeySucceeded)
    printEditState(f, "first keystroke failure");
  LOKA_VERIFY(firstKeySucceeded);
  LOKA_VERIFY(observer.calls == 1);
  LOKA_VERIFY(f.lines.revision().get().change.kind == LIST_UPDATE);
  LOKA_VERIFY(f.lines.at(0).id == first && f.lines.at(1).id == second);
  LOKA_VERIFY(f.lines.at(2).value.compare(unchanged, false) == StringCompareEqual);
  LOKA_VERIFY(bytes(f.lines.at(0).value) == "abxcd" && f.cursor.state()->get() == LineCursor(first, 3));
  f.edit(@"abx\ncd\nabcd\nabcd", 4);
  LOKA_VERIFY(observer.calls == 2 && f.lines.size() == 4);
  LOKA_VERIFY(f.lines.revision().get().change.kind == LIST_BATCH);
  LOKA_VERIFY(f.lines.at(2).id == second && f.cursor.state()->get() == LineCursor(f.lines.at(1).id, 0));
  f.edit(@"abxcd\nabcd\nabcd", 3);
  LOKA_VERIFY(observer.calls == 3 && f.lines.size() == 3);
  LOKA_VERIFY(f.lines.revision().get().change.kind == LIST_BATCH);
  LOKA_VERIFY(f.lines.at(1).id == second && f.cursor.state()->get() == LineCursor(first, 3));
  [f.view setSelectedRange:NSMakeRange(7, 0)];
  [[f.view delegate]
      textViewDidChangeSelection:[NSNotification notificationWithName:NSTextViewDidChangeSelectionNotification
                                                               object:f.view]];
  LOKA_VERIFY(observer.calls == 3 && f.cursor.state()->get() == LineCursor(second, 1));
  // Native selection replacement and deletion remain a single line UPDATE.
  [f.view setSelectedRange:NSMakeRange(7, 2)];
  [[f.view delegate]
      textViewDidChangeSelection:[NSNotification notificationWithName:NSTextViewDidChangeSelectionNotification
                                                               object:f.view]];
  f.context->onPropsApplied();
  LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(7, 2)));
  f.edit(@"abxcd\naQd\nabcd", 8);
  LOKA_VERIFY(observer.calls == 4 && bytes(f.lines.at(1).value) == "aQd");
  LOKA_VERIFY(f.lines.revision().get().change.kind == LIST_UPDATE);
  f.edit(@"abxcd\nad\nabcd", 7);
  LOKA_VERIFY(observer.calls == 5 && bytes(f.lines.at(1).value) == "ad");
  LOKA_VERIFY(Access::restores(*f.context) == 0);

  // Structural notifications also use text, including endpoint splits whose
  // unchanged source is consumed by the prefix/suffix comparison.
  NSString *splits[] = {@"\nabcd\nabcd\nabcd", @"abcd\n\nabcd\nabcd", @"ab\ncd\nabcd\nabcd"};
  for (unsigned i = 0; i < sizeof(splits) / sizeof(splits[0]); ++i)
  {
    Fixture structural;
    [structural.view setSelectedRange:NSMakeRange(10, 0)];
    structural.edit(splits[i], 0);
    LOKA_VERIFY(structural.lines.size() == 4);
    LOKA_VERIFY([[structural.view string] isEqualToString:splits[i]]);
    [structural.view setSelectedRange:NSMakeRange(11, 0)];
    structural.edit(@"abcd\nabcd\nabcd", 0);
    LOKA_VERIFY(structural.lines.size() == 3);
    for (unsigned short line = 0; line < structural.lines.size(); ++line)
      LOKA_VERIFY(bytes(structural.lines.at(line).value) == "abcd");
    LOKA_VERIFY(Access::restores(*structural.context) == 0 && [structural.view isEditable]);
  }
}

void testMacTextEditorUndoLocation()
{
  Fixture f(12);
  Observer observer(f);
  const ItemId first = f.lines.at(0).id, distant = f.lines.at(10).id;
  [f.view setAllowsUndo:YES];
  NSUndoManager *undo = [f.view undoManager];
  LOKA_VERIFY(undo != nil && [undo isUndoRegistrationEnabled]);
  // Manual grouping only: an event group would be closed by the run loop after
  // the test already closed it, and NSUndoManager raises on the extra end.
  [undo setGroupsByEvent:NO];
  [undo beginUndoGrouping];
  [f.view insertText:@"x" replacementRange:[f.view selectedRange]];
  [undo endUndoGrouping];
  LOKA_VERIFY([undo canUndo]);
  if (bytes(f.lines.at(0).value) != "abxcd" || f.cursor.state()->get() != LineCursor(first, 3) || observer.calls != 1)
    printEditState(f, "undo setup keystroke failure");
  LOKA_VERIFY(bytes(f.lines.at(0).value) == "abxcd" && f.cursor.state()->get() == LineCursor(first, 3));
  LOKA_VERIFY(observer.calls == 1);
  [f.view setSelectedRange:NSMakeRange(51, 0)];
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(distant, 0));
  // The fixture has not yielded to an event boundary. Close any remaining
  // group, including the outer group supplied by event grouping.
  while ([undo groupingLevel] > 0)
    [undo endUndoGrouping];
  LOKA_VERIFY([undo groupingLevel] == 0 && [undo canUndo]);
  [undo undo];
  // The cursor fact follows the native caret wherever AppKit leaves it after
  // undo (the ruling makes the caret a reported fact, not a decision here).
  const NSUInteger undoCaret = [f.view selectedRange].location;
  NSString *undoPrefix = [[f.view string] substringToIndex:undoCaret];
  const NSRange lastBreak = [undoPrefix rangeOfString:@"\n" options:NSBackwardsSearch];
  const NSUInteger undoLine = [[undoPrefix componentsSeparatedByString:@"\n"] count] - 1;
  const NSUInteger undoColumn = undoCaret - (lastBreak.location == NSNotFound ? 0 : lastBreak.location + 1);
  const bool undoSucceeded =
      bytes(f.lines.at(0).value) == "abcd" && observer.calls == 2
      && f.cursor.state()->get()
             == LineCursor(f.lines.at(static_cast<unsigned short>(undoLine)).id, static_cast<int>(undoColumn))
      && Access::restores(*f.context) == 0 && [f.view isEditable];
  if (!undoSucceeded)
  {
    printUndoState(f, undo, "failure before restore turn");
    f.turn();
    printUndoState(f, undo, "failure after restore turn");
  }
  LOKA_VERIFY(undoSucceeded);
  LOKA_VERIFY(bytes(f.lines.at(0).value) == "abcd");
  LOKA_VERIFY(observer.calls == 2);
  LOKA_VERIFY(f.cursor.state()->get()
              == LineCursor(f.lines.at(static_cast<unsigned short>(undoLine)).id, static_cast<int>(undoColumn)));
  LOKA_VERIFY(Access::restores(*f.context) == 0 && [f.view isEditable]);

  // Also pin a notification before native selection returns to the edit.
  [f.view setSelectedRange:NSMakeRange(50, 0)];
  NSMutableString *text = [[[f.view string] mutableCopy] autorelease];
  [text insertString:@"x" atIndex:2];
  f.edit(text, 51);
  LOKA_VERIFY(bytes(f.lines.at(0).value) == "abxcd");
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(distant, 0));
  LOKA_VERIFY(observer.calls == 3 && [f.view isEditable]);
  LOKA_VERIFY(Access::restores(*f.context) == 0);

  // Undo restores several removed rows through the storage delegate range door.
  Fixture multi;
  Observer multiObserver(multi);
  const ItemId retained = multi.lines.at(0).id;
  [multi.view setAllowsUndo:YES];
  NSUndoManager *multiUndo = [multi.view undoManager];
  LOKA_VERIFY(multiUndo != nil && [multiUndo isUndoRegistrationEnabled]);
  [multiUndo setGroupsByEvent:NO];
  [multi.view setSelectedRange:NSMakeRange(1, 12)];
  [multiUndo beginUndoGrouping];
  [multi.view insertText:@"X" replacementRange:[multi.view selectedRange]];
  [multiUndo endUndoGrouping];
  LOKA_VERIFY(multi.lines.size() == 1 && bytes(multi.lines.at(0).value) == "aXd");
  LOKA_VERIFY(multi.cursor.state()->get() == LineCursor(retained, 2) && multiObserver.calls == 1);
  [multi.view setSelectedRange:NSMakeRange(0, 0)];
  while ([multiUndo groupingLevel] > 0)
    [multiUndo endUndoGrouping];
  LOKA_VERIFY([multiUndo canUndo]);
  [multiUndo undo];
  LOKA_VERIFY(multi.lines.size() == 3 && multi.lines.at(0).id == retained);
  for (unsigned short i = 0; i < multi.lines.size(); ++i)
    LOKA_VERIFY(bytes(multi.lines.at(i).value) == "abcd");
  LOKA_VERIFY([[multi.view string] isEqualToString:@"abcd\nabcd\nabcd"]);
  const NSRange afterUndo = [multi.view selectedRange];
  if (NSEqualRanges(afterUndo, NSMakeRange(0, 0)))
  {
    // Undo reaches the rail through the storage delegate only, and AppKit leaves
    // a selection outside the restored span where it was (offset 0 here), so no
    // selection notification follows. The storage path derives the caret from
    // the change span: the end of the restored text, "abcd\nabcd\nabc|d".
    LOKA_VERIFY(multi.cursor.state()->get() == LineCursor(multi.lines.at(2).id, 3));
  }
  else
  {
    // Mac OS X 10.9's AppKit selects the restored span on undo (measured on
    // 10.9.5), and the rail reports that native caret: the span's start.
    std::printf("[skip] undo keeps an outside selection: this AppKit selects the restored span (10.9); "
                "its caret is pinned instead\n");
    LOKA_VERIFY(NSEqualRanges(afterUndo, NSMakeRange(1, 12)));
    LOKA_VERIFY(multi.cursor.state()->get() == LineCursor(retained, 1));
  }
  LOKA_VERIFY(multiObserver.calls == 2 && Access::restores(*multi.context) == 0 && [multi.view isEditable]);
  // The next selection notification reports the native caret over the committed text.
  [[multi.view delegate]
      textViewDidChangeSelection:[NSNotification notificationWithName:NSTextViewDidChangeSelectionNotification
                                                               object:multi.view]];
  LOKA_VERIFY(multi.cursor.state()->get() == LineCursor(retained, static_cast<int>(afterUndo.location))
              && multiObserver.calls == 2);
}

void testMacTextEditorStorageChanges()
{
  Fixture f;
  Observer observer(f);
  // Disable the view delegate: the storage callback alone must commit.
  id delegate = [f.view delegate];
  [f.view setDelegate:nil];
  [[f.view textStorage] replaceCharactersInRange:NSMakeRange(2, 0) withString:@"x"];
  [f.view setDelegate:delegate];
  if (bytes(f.lines.at(0).value) != "abxcd" || f.cursor.state()->get() != LineCursor(f.lines.at(0).id, 3))
    printEditState(f, "storage insertion failure");
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 3));
  LOKA_VERIFY(bytes(f.lines.at(0).value) == "abxcd");
  LOKA_VERIFY([[f.view string] isEqualToString:@"abxcd\nabcd\nabcd"]);
  LOKA_VERIFY(observer.calls == 1 && [f.view isEditable]);
  LOKA_VERIFY(Access::restores(*f.context) == 0);
  // Only textDidChange can publish this differing caret: selection callbacks
  // are disconnected, and the committed text is already identical.
  [f.view setDelegate:nil];
  [f.view setSelectedRange:NSMakeRange(1, 0)];
  [f.view setDelegate:delegate];
  f.notify();
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 1));
  const Snapshot committed(f);
  f.notify();
  f.turn();
  committed.unchanged(f);
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 1));
  LOKA_VERIFY(observer.calls == 1 && [f.view isEditable]);
  LOKA_VERIFY(Access::restores(*f.context) == 0);

  // A replacement at a different location gets its caret from the text diff.
  [f.view setDelegate:nil];
  [[f.view textStorage] replaceCharactersInRange:NSMakeRange(2, 2) withString:@"YZQ"];
  [f.view setDelegate:delegate];
  if (bytes(f.lines.at(0).value) != "abYZQd" || f.cursor.state()->get() != LineCursor(f.lines.at(0).id, 5))
    printEditState(f, "storage replacement failure");
  LOKA_VERIFY(bytes(f.lines.at(0).value) == "abYZQd");
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 5));
  LOKA_VERIFY(observer.calls == 2 && Access::restores(*f.context) == 0);

  // Storage-only multi-line edits must derive the caret before AppKit updates
  // selection, including a caret on a newly inserted row.
  NSString *insertions[] = {@"X", @"", @"\n", @"one\ntwo\nthree"};
  for (unsigned i = 0; i < sizeof(insertions) / sizeof(insertions[0]); ++i)
  {
    Fixture multi;
    Observer multiObserver(multi);
    const ItemId first = multi.lines.at(0).id;
    id viewDelegate = [multi.view delegate];
    [multi.view setDelegate:nil];
    [multi.view setSelectedRange:NSMakeRange(0, 0)];
    [[multi.view textStorage] replaceCharactersInRange:NSMakeRange(1, 12) withString:insertions[i]];
    [multi.view setDelegate:viewDelegate];
    LOKA_VERIFY(multi.lines.size() == (i < 2 ? 1 : i == 2 ? 2 : 3));
    LOKA_VERIFY(multi.lines.at(0).id == first);
    LOKA_VERIFY(bytes(multi.lines.at(0).value) == (i == 0 ? "aXd" : i == 1 ? "ad" : i == 2 ? "a" : "aone"));
    if (i >= 2)
      LOKA_VERIFY(bytes(multi.lines.at(multi.lines.size() - 1).value) == (i == 2 ? "d" : "threed"));
    LOKA_VERIFY(multi.cursor.state()->get()
                == LineCursor(multi.lines.at(multi.lines.size() - 1).id,
                              i == 0   ? 2
                              : i == 1 ? 1
                              : i == 2 ? 0
                                       : 5));
    LOKA_VERIFY(multiObserver.calls == 1 && Access::restores(*multi.context) == 0);
    LOKA_VERIFY([multi.view isEditable]);
  }
}

void testMacTextEditorStorageAttributes()
{
  Fixture f;
  Observer observer(f);
  const Snapshot committed(f);
  // Leave a native-only selection to discriminate ignoring attributes from
  // incorrectly routing them through even the unchanged-text caret path.
  id delegate = [f.view delegate];
  [f.view setDelegate:nil];
  [f.view setSelectedRange:NSMakeRange(1, 0)];
  NSTextStorage *storage = [f.view textStorage];
  [storage beginEditing];
  [storage addAttribute:NSForegroundColorAttributeName value:[NSColor redColor] range:NSMakeRange(0, 1)];
  [storage endEditing];
  [f.view setDelegate:delegate];
  committed.unchanged(f);
  f.turn();
  committed.unchanged(f);
  LOKA_VERIFY(observer.calls == 0 && [f.view isEditable]);
  LOKA_VERIFY(Access::restores(*f.context) == 0);
}

void testMacTextEditorMultilinePasteRefusal()
{
  // Keep the registered entry point; the former refusal cases now accept.
  for (int sameCount = 0; sameCount < 2; ++sameCount)
  {
    Fixture f;
    Observer observer(f);
    const ItemId first = f.lines.at(0).id, second = f.lines.at(1).id;
    [f.view setSelectedRange:NSMakeRange(1, sameCount ? 12 : 2)];
    // Use the native replacement path used by plain-text paste, without
    // changing the user's global pasteboard.
    [f.view insertText:@"one\ntwo\nthree" replacementRange:[f.view selectedRange]];
    LOKA_VERIFY(f.lines.size() == (sameCount ? 3 : 5));
    LOKA_VERIFY(f.lines.at(0).id == first && bytes(f.lines.at(0).value) == "aone");
    LOKA_VERIFY(bytes(f.lines.at(1).value) == "two" && bytes(f.lines.at(2).value) == "threed");
    if (!sameCount)
      LOKA_VERIFY(f.lines.at(3).id == second && bytes(f.lines.at(4).value) == "abcd");
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(2).id, 5));
    LOKA_VERIFY(observer.calls == 1 && f.lines.revision().get().change.kind == LIST_BATCH);
    LOKA_VERIFY([f.view isEditable] && Access::restores(*f.context) == 0);
  }
  for (int action = 0; action < 3; ++action)
  {
    Fixture f;
    Observer observer(f);
    const ItemId first = f.lines.at(0).id;
    [f.view setSelectedRange:NSMakeRange(1, 12)];
    if (action == 0)
      [f.view insertText:@"X" replacementRange:[f.view selectedRange]];
    else if (action == 1)
      [f.view deleteBackward:nil];
    else
      [f.view insertNewline:nil];
    LOKA_VERIFY(f.lines.size() == (action == 2 ? 2 : 1));
    LOKA_VERIFY(f.lines.at(0).id == first);
    LOKA_VERIFY(bytes(f.lines.at(0).value) == (action == 0 ? "aXd" : action == 1 ? "ad" : "a"));
    if (action == 2)
      LOKA_VERIFY(bytes(f.lines.at(1).value) == "d");
    LOKA_VERIFY(f.cursor.state()->get()
                == (action == 2 ? LineCursor(f.lines.at(1).id, 0) : LineCursor(first, action == 0 ? 2 : 1)));
    LOKA_VERIFY(observer.calls == 1 && f.lines.revision().get().change.kind == LIST_BATCH);
    LOKA_VERIFY([f.view isEditable] && Access::restores(*f.context) == 0);
  }
  // A view-only notification reports its final selection, which deliberately
  // differs from the diff-derived insertion end on the newly inserted row.
  Fixture viewOnly;
  Observer observer(viewOnly);
  id storageDelegate = [[viewOnly.view textStorage] delegate];
  [[viewOnly.view textStorage] setDelegate:nil];
  viewOnly.edit(@"aone\ntwo\nthreed", 0);
  [[viewOnly.view textStorage] setDelegate:storageDelegate];
  LOKA_VERIFY(viewOnly.lines.size() == 3 && bytes(viewOnly.lines.at(2).value) == "threed");
  LOKA_VERIFY(viewOnly.cursor.state()->get() == LineCursor(viewOnly.lines.at(0).id, 0));
  LOKA_VERIFY(observer.calls == 1 && Access::restores(*viewOnly.context) == 0);
}

void testMacTextEditorRefusals()
{
  MacPlatformContext rail;
  TextCapacity maximum;
  LOKA_VERIFY(rail.queryTextEditorCapacity(maximum));
  LOKA_VERIFY(maximum.lines == 256 && maximum.bytes == 8192);
  for (int failure = 0; failure < 5; ++failure)
  {
    Fixture f(failure == 0 ? 256 : 3);
    Observer observer(f);
    if (failure == 1)
    {
      LOKA_VERIFY(f.lines.replaceWithOne(String("fresh")) == EDIT_OK);
      observer.calls = 0;
    }
    const Snapshot snapshot(f);
    const NSRange selection = [f.view selectedRange];
    const NSRect visible = [f.scroll documentVisibleRect];
    // Positive undo control, before proving that cancellation removes it.
    NSUndoManager *undo = [f.view undoManager];
    LOKA_VERIFY(undo != nil);
    [undo setGroupsByEvent:NO];
    [undo beginUndoGrouping];
    [[undo prepareWithInvocationTarget:f.view] setString:@"refused undo payload"];
    [undo endUndoGrouping];
    LOKA_VERIFY([undo canUndo]);
    NSMutableString *tentative = [[[f.view string] mutableCopy] autorelease];
    [tentative insertString:(failure == 0 || failure == 2 ? @"\n" : @"x") atIndex:selection.location];
    if (failure == 2)
      loka::core::testing::failLokaAllocRaw("TextEditor", "Scratch", 1);
    if (failure == 3)
      [tentative appendString:[@"x" stringByPaddingToLength:8193 withString:@"x" startingAtIndex:0]];
    if (failure == 4)
      [tentative appendString:loneSurrogate()];
    f.edit(tentative, selection.location + 1);
    loka::core::testing::allowLokaAllocRaw();
    snapshot.unchanged(f);
    LOKA_VERIFY(observer.calls == 0);
    LOKA_VERIFY(![f.view isEditable]);
    f.restored(1);
    snapshot.unchanged(f);
    LOKA_VERIFY(observer.calls == 0 && ![undo canUndo]);
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], selection));
    LOKA_VERIFY(NSEqualPoints([f.scroll documentVisibleRect].origin, visible.origin));
    nextKey(f);
  }
}

void testMacTextEditorCompositionRefusal()
{
  // #1264: an input method composes through setMarkedText: and commits
  // through insertText:. Stage 1 refuses the composition before it marks
  // anything, so the view never holds marked text while it is read-only.
  Fixture f;
  Observer observer(f);
  const Snapshot snapshot(f);
  const NSRange selection = [f.view selectedRange];
  const NSRange none = NSMakeRange(NSNotFound, 0);
  // A converted preedit is non-ASCII; a romaji preedit is ASCII. Both are refused.
  NSString *preedits[] = {@"\u304b", @"k"};
  for (int i = 0; i < 2; ++i)
  {
    [f.view setMarkedText:preedits[i] selectedRange:NSMakeRange(1, 0) replacementRange:none];
    // The run loop turns between keystrokes. On the unfixed rail the refusal's
    // restore replaced the storage here, and AppKit's commit of the stale
    // marked text recursed until the stack overflowed.
    for (int turn = 0; turn < 5; ++turn)
      f.turn();
    LOKA_VERIFY(![f.view hasMarkedText]);
    LOKA_VERIFY([f.view isEditable]);
    LOKA_VERIFY(Access::restores(*f.context) == 0);
    snapshot.unchanged(f);
    LOKA_VERIFY(observer.calls == 0);
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], selection));
  }
  // A lone-surrogate commit is refused even with the UTF-8 alphabet open.
  [f.view insertText:loneSurrogate() replacementRange:none];
  LOKA_VERIFY(![f.view isEditable]);
  f.restored(1);
  snapshot.unchanged(f);
  LOKA_VERIFY(observer.calls == 0);
  LOKA_VERIFY(NSEqualRanges([f.view selectedRange], selection));
  nextKey(f);
}

void testMacTextEditorNestedInput()
{
  // PR C2 (#1057): only the immediate variant remains. The deferred variant
  // modelled a callback deferred through the document ledger re-entering
  // native input while the guard's own end() still ran it inside the input
  // borrow; with the guard joining the collection turn that callback runs at
  // the turn's settle, outside the borrow, and the path it pinned no longer
  // exists. Production defers only BoundaryNode release thunks.
  for (int deferred = 0; deferred < 1; ++deferred)
  {
    Fixture f;
    Observer observer(f);
    observer.nested = true;
    observer.deferred = deferred != 0;
    f.edit(@"abxcd\nabcd\nabcd", 3);
    LOKA_VERIFY(!f.controller.borrowPhase().open());
    // An immediate observer re-enters inside the input borrow: refused, as #977 says.
    LOKA_VERIFY(observer.calls == 1 && bytes(f.lines.at(0).value) == "abxcd");
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 3));
    f.restored(1);
    LOKA_VERIFY(observer.calls == 1);
    nextKey(f);
  }
  {
    Fixture f;
    Observer listObserver(f);
    CaretObserver observer(f);
    [f.view setSelectedRange:NSMakeRange(1, 0)];
    [[f.view delegate]
        textViewDidChangeSelection:[NSNotification notificationWithName:NSTextViewDidChangeSelectionNotification
                                                                 object:f.view]];
    LOKA_VERIFY(observer.calls == 1 && listObserver.calls == 0);
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 1));
    f.restored(1);
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(1, 0)));
    nextKey(f);
  }
}

void testMacTextEditorReplacementFailure()
{
  {
    Fixture empty(1, "");
    empty.edit(loneSurrogate(), 1);
    empty.restored(1);
  }
  Fixture f;
  LokaRefusingEditorView *view = [[LokaRefusingEditorView alloc] initWithFrame:[f.view frame]];
  [view setString:[f.view string]];
  [view setSelectedRange:[f.view selectedRange]];
  [view setDelegate:[f.view delegate]];
  [[view textStorage] setDelegate:[[f.view textStorage] delegate]];
  [f.view setDelegate:nil];
  [[f.view textStorage] setDelegate:nil];
  [f.scroll setDocumentView:view];
  f.view = view;
  [view release];
  f.edit(loneSurrogate(), 1);
  [view setRefuse:YES];
  // Invoke the scheduled callback after the notification has unwound, without
  // spinning the run loop into its retry. The retry must remain deferred.
  id delegate = [view delegate];
  [NSObject cancelPreviousPerformRequestsWithTarget:delegate];
  [delegate performSelector:@selector(restoreProjection)];
  LOKA_VERIFY(Access::restores(*f.context) == 1);
  LOKA_VERIFY(![view isEditable] && [[view string] isEqualToString:loneSurrogate()]);
  f.restored(2);
  nextKey(f);
}

namespace
{
  class Highlighter : public LineHighlighter
  {
  public:
    mutable unsigned calls;
    bool fail;
    Highlighter()
        : calls(0),
          fail(false)
    {
    }
    virtual bool highlight(const String &line, AttributedString::Builder &out) const
    {
      ++this->calls;
      return !this->fail && out.append(line, Bold + Italic + FontSize<18>());
    }
  };
} // namespace
void testMacTextEditorHighlightAndLifecycle()
{
  {
    Highlighter policy;
    Fixture pending;
    pending.node.props.highlighter(policy);
    pending.context->onPropsApplied();
    id delegate = [pending.view delegate];
    [pending.view setDelegate:nil];
    [[pending.view textStorage] replaceCharactersInRange:NSMakeRange(2, 0) withString:@"x"];
    [pending.view setDelegate:delegate];
    LOKA_VERIFY(policy.calls == 3);
    // The owner changes structure before the queued native style pass.
    LOKA_VERIFY(pending.lines.insert(0, String("owner")) == EDIT_OK);
    for (int i = 0; i < 20 && Access::restores(*pending.context) == 0; ++i)
      pending.turn();
    LOKA_VERIFY(Access::restores(*pending.context) == 1);
    LOKA_VERIFY([[pending.view string] isEqualToString:@"owner\nabxcd\nabcd\nabcd"]);
    LOKA_VERIFY(policy.calls == 5);
  }
  Highlighter highlighter;
  Fixture f;
  f.node.props.highlighter(highlighter);
  f.context->onPropsApplied();
  LOKA_VERIFY(highlighter.calls == 3);
  f.context->onPropsApplied();
  LOKA_VERIFY(highlighter.calls == 3);
  f.edit(@"abxcd\nabcd\nabcd", 3);
  LOKA_VERIFY(highlighter.calls == 4);
  NSFont *styled = [[f.view textStorage] attribute:NSFontAttributeName atIndex:0 effectiveRange:0];
  verifyFont(styled, (NSFont *)f.controller.textFont(Bold + Italic + FontSize<18>(), true));
  verifyTextColor([[f.view textStorage] attribute:NSForegroundColorAttributeName atIndex:0 effectiveRange:0]);
  highlighter.fail = true;
  f.edit(@"abxycd\nabcd\nabcd", 4);
  LOKA_VERIFY(highlighter.calls == 5 && bytes(f.lines.at(0).value) == "abxycd");
  verifyFont([[f.view textStorage] attribute:NSFontAttributeName atIndex:0 effectiveRange:0],
             (NSFont *)f.controller.textFont(TextStyle(), true));
  verifyTextColor([[f.view textStorage] attribute:NSForegroundColorAttributeName atIndex:0 effectiveRange:0]);
  highlighter.fail = false;
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  f.edit(@"abxyQcd\nabcd\nabcd", 5);
  loka::core::testing::allowLokaAllocRaw();
  LOKA_VERIFY(highlighter.calls == 6 && bytes(f.lines.at(0).value) == "abxyQcd");
  verifyFont([[f.view textStorage] attribute:NSFontAttributeName atIndex:0 effectiveRange:0],
             (NSFont *)f.controller.textFont(TextStyle(), true));
  // Storage-only input queues one style pass and never styles in the delegate.
  id delegate = [f.view delegate];
  [f.view setDelegate:nil];
  NSTextStorage *storage = [f.view textStorage];
  [storage replaceCharactersInRange:NSMakeRange(2, 0) withString:@"R"];
  [storage replaceCharactersInRange:NSMakeRange(3, 0) withString:@"S"];
  [f.view setDelegate:delegate];
  LOKA_VERIFY(highlighter.calls == 6);
  LOKA_VERIFY(bytes(f.lines.at(0).value) == "abRSxyQcd");
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 4));
  // A run-loop slice may service another source before the queued selector.
  // Both edits coalesce; the line cache skips the two unchanged lines, even
  // if AppKit also delivers a view notification.
  NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:2.0];
  while (highlighter.calls < 7 && [deadline timeIntervalSinceNow] > 0)
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
  LOKA_VERIFY(highlighter.calls == 7);
  verifyFont([storage attribute:NSFontAttributeName atIndex:2 effectiveRange:0],
             (NSFont *)f.controller.textFont(Bold + Italic + FontSize<18>(), true));
  LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 4));
  f.turn();
  LOKA_VERIFY(highlighter.calls == 7);

  // A queued style pass loses its owner on retained detach.
  [f.view setDelegate:nil];
  [storage replaceCharactersInRange:NSMakeRange(2, 2) withString:@""];
  [f.view setDelegate:delegate];
  LOKA_VERIFY(highlighter.calls == 7);
  NotifySubtreeNodeDetached(&f.node);
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  f.turn();
  LOKA_VERIFY(highlighter.calls == 7);
  NotifySubtreeNodeAttached(&f.node);
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY(highlighter.calls == 10);

  // Pending restore is invalidated by retained detach, as is the style cache.
  f.edit(loneSurrogate(), 1);
  NotifySubtreeNodeDetached(&f.node);
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY([f.scroll superview] == [f.host.window contentView] && [f.scroll isHidden]);
  LOKA_VERIFY([f.view delegate] == nil && [[f.view textStorage] delegate] == nil);
  f.turn();
  LOKA_VERIFY(Access::restores(*f.context) == 0);
  NotifySubtreeNodeAttached(&f.node);
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY([f.scroll superview] == [f.host.window contentView] && [f.view delegate] != nil);
  LOKA_VERIFY((id)[[f.view textStorage] delegate] == (id)[f.view delegate]);
  LOKA_VERIFY([[f.view string] isEqualToString:@"abxyQcd\nabcd\nabcd"]);
  nextKey(f);
  // An external over-cap value makes the editor unavailable, never truncated.
  LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String(std::string(8193, 'a'))) == EDIT_OK);
  f.context->onPropsApplied();
  LOKA_VERIFY(![f.view isEditable] && [[f.view string] length] == 0);
  LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("recovered")) == EDIT_OK);
  f.context->onPropsApplied();
  LOKA_VERIFY([f.view isEditable]);
  LifecycleFactTestAccess::MarkSubtreeRetired(&f.node);
  LifecycleFactTestAccess::DeliverFacts(&f.node);
  LOKA_VERIFY([f.scroll superview] == nil && [f.view delegate] == nil);
  LOKA_VERIFY([[f.view textStorage] delegate] == nil);
}

void testMacTextEditorDetachHidesAndRetireRemoves()
{
  int released = 0;
  NativeHost host;
  if (!host.window)
  {
    std::printf("[skip] TextEditor lifecycle: window creation refused\n");
    return;
  }
  loka::app::testing::TextEditorStateOwner owner;
  loka::testing::TextBufferFixture lines;
  LOKA_VERIFY(lines.attach(&owner.tracker, 4) == ATTACH_OK);
  LOKA_VERIFY(lines.insert(0, String("abcd")) == EDIT_OK);
  NSView *parent = [host.window contentView];
  MacScenePlatformController controller(parent, RailMetrics());
  TextEditorNode node(TextEditorProps(lines, owner.cursor).moveCaretTo(owner.request));
  NSAutoreleasePool *inner = [[NSAutoreleasePool alloc] init];
  LayoutState state;
  state.x = 0;
  state.y = 0;
  state.width = 240;
  state.height = 96;
  state.spacing = 4;
  LOKA_VERIFY(controller.prepareProjectedLayout(&node, state));
  MacTextEditorContext *context = static_cast<MacTextEditorContext *>(node.getContext());
  LOKA_VERIFY(context && context->layout(&controller, state) == 96);
  NSScrollView *scroll = (NSScrollView *)[[parent subviews] objectAtIndex:0];
  NSTextView *view = (NSTextView *)[scroll documentView];
  observeMacNativeDeallocation(scroll, released);
  NSView *sibling = [[NSView alloc] initWithFrame:NSZeroRect];
  [parent addSubview:sibling];
  [sibling release];
  const NSUInteger index = [[parent subviews] indexOfObject:scroll];
  const bool focused = [host.window makeFirstResponder:view] && [host.window firstResponder] == view;
  if (!focused)
    std::printf("[skip] TextEditor detach focus: window could not acquire first responder\n");
  NotifySubtreeNodeDetached(&node);
  LifecycleFactTestAccess::DeliverFacts(&node);
  LOKA_VERIFY([scroll isHidden] && [scroll superview] == parent);
  LOKA_VERIFY([[parent subviews] indexOfObject:scroll] == index);
  LOKA_VERIFY(![view isEditable] && ![view delegate]);
  if (focused)
    LOKA_VERIFY([host.window firstResponder] != view);
  const LineCursor requested(lines.at(0).id, 3);
  {
    StateTrackerGuard guard(&owner.tracker);
    owner.request.set(requested);
  }
  NotifySubtreeNodeAttached(&node);
  LifecycleFactTestAccess::DeliverFacts(&node);
  LOKA_VERIFY(![scroll isHidden] && [scroll superview] == parent);
  LOKA_VERIFY([[parent subviews] indexOfObject:scroll] == index);
  LOKA_VERIFY([view isEditable] && [view delegate]);
  // SETTLE_ATTACH still consumes requests after unhiding the existing view.
  LOKA_VERIFY(owner.request.get().isNone() && owner.cursor.state()->get() == requested);
  LOKA_VERIFY(NSEqualRanges([view selectedRange], NSMakeRange(3, 0)));
  LifecycleFactTestAccess::MarkSubtreeRetired(&node);
  LifecycleFactTestAccess::DeliverFacts(&node);
  LOKA_VERIFY(![scroll superview] && [scroll isHidden] && released == 0);
  // Discard removeFromSuperview's autorelease before testing queue ownership.
  [inner drain];
  LOKA_VERIFY(released == 0);
  inner = [[NSAutoreleasePool alloc] init];
  controller.drainNativeRetirements();
  [inner drain];
  // The controller's queue has released its retain here. 10.9 AppKit keeps one
  // more reference to a removed NSScrollView until the next run-loop turn
  // (observed on Mavericks: retainCount 1 after the drain, deallocated after
  // one turn, with or without first responder); 26.x releases at once. One
  // bounded turn, so the pin reads the queue's release on both.
  if (released != 1)
  {
    std::printf("[note] TextEditor retire: AppKit deferred the last release to the run loop\n");
    NSAutoreleasePool *turn = [[NSAutoreleasePool alloc] init];
    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
    [turn drain];
  }
  LOKA_VERIFY(released == 1);
}

void testMacTextEditorUtf8Boundary()
{
  for (std::size_t i = 0; i < sizeof(text_alphabet_cases::cases) / sizeof(text_alphabet_cases::cases[0]); ++i)
  {
    const text_alphabet_cases::Case &entry = text_alphabet_cases::cases[i];
    NSString *text = [[[NSString alloc] initWithBytes:entry.bytes length:entry.length
                                             encoding:NSUTF8StringEncoding] autorelease];
    if (!text)
      continue; // Malformed UTF-8 has no native snapshot; lone UTF-16 is pinned by the helpers.
    Fixture f(1);
    const Snapshot snapshot(f);
    f.edit(text, [text length]);
    if (entry.*text_alphabet_cases::expected != EDITOR_OK)
    {
      snapshot.unchanged(f);
      LOKA_VERIFY(![f.view isEditable]);
      f.restored(1);
      snapshot.unchanged(f);
    }
    else
      LOKA_VERIFY(bytes(f.lines.at(0).value) == std::string(entry.bytes, entry.length));
  }
  {
    Fixture f(1);
    std::size_t offset = 2;
    loka::core::testing::failLokaAllocRaw("unused", "unused", 0);
    const EditorResult result = Access::observe(*f.context, offset);
    const int attempts = loka::core::testing::lokaAllocRawAttempts();
    loka::core::testing::allowLokaAllocRaw();
    LOKA_VERIFY(result == EDITOR_OK && attempts == 0);
    const ItemId old = f.lines.at(0).id;
    LOKA_VERIFY(f.lines.replaceWithOne(String("abcd")) == EDIT_OK);
    LOKA_VERIFY(f.lines.at(0).id != old);
    LOKA_VERIFY(Access::observe(*f.context, offset) == EDITOR_STALE_ID);
  }
  loka::core::testing::failLokaAllocRaw("MacTextEditor", "NativeTextBytes", 0);
  {
    Fixture f(1);
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("changed")) == EDIT_OK);
    loka::core::testing::failLokaAllocRaw("MacTextEditor", "NativeTextBytes", 1);
    f.context->onPropsApplied();
    LOKA_VERIFY(loka::core::testing::lokaAllocRawAttempts() == 1);
    LOKA_VERIFY(![f.view isEditable]);
    f.restored(1);
    LOKA_VERIFY([[f.view string] isEqualToString:@"changed"]);
  }
  LOKA_VERIFY(loka::core::testing::lokaAllocRawLive() == 0);
  loka::core::testing::allowLokaAllocRaw();
}

namespace
{
  /** Fault source: comparison stays stable; the second collection corrupts the run.
      This deliberately violates String's stable-content contract to probe the
      native decode/range walls after an earlier run has already been styled. */
  class FaultRunString : public loka::platform::String
  {
  public:
    explicit FaultRunString(bool excess)
        : excess_(excess),
          collections_(0)
    {
    }
    virtual bool queryUtf8(loka::platform::Utf8View &out) const
    {
      out.bytes = "cd";
      out.length = 2;
      return true;
    }
    virtual bool appendUtf8(std::string &out) const
    {
      ++this->collections_;
      out += this->collections_ == 1 ? "cd" : this->excess_ ? "cdef" : "\200";
      return true;
    }
    unsigned collections() const
    {
      return this->collections_;
    }

  private:
    const bool excess_;
    mutable unsigned collections_;
  };
  class FaultRunHighlighter : public LineHighlighter
  {
  public:
    explicit FaultRunHighlighter(const String &tail)
        : tail_(tail)
    {
    }
    virtual bool highlight(const String &, AttributedString::Builder &out) const
    {
      return out.append(String("ab"), Bold + FontSize<18>()) && out.append(this->tail_, Italic);
    }

  private:
    const String tail_;
  };
} // namespace

void testMacTextEditorUtf8StyleFallback()
{
  for (int excess = 0; excess < 2; ++excess)
  {
    FaultRunString *fault = new FaultRunString(excess != 0);
    const String tail(Managed<loka::platform::String>::Wrap(fault));
    FaultRunHighlighter policy(tail);
    Fixture f(1);
    f.node.props.highlighter(policy);
    f.context->onPropsApplied();
    LOKA_VERIFY(fault->collections() == 2);
    f.verifyPlainAttributes();
    LOKA_VERIFY([[f.view string] isEqualToString:@"abcd"]);
  }
}

void testMacTextEditorUtf8OpenCarets()
{
  using namespace text_alphabet_cases;
  for (std::size_t i = 0; i < sizeof(nativeCases) / sizeof(nativeCases[0]); ++i)
  {
    const NativeCase &entry = nativeCases[i];
    Fixture f(1, "", entry.capacity);
    std::vector<unichar> units;
    for (const wchar_t *unit = entry.wide; *unit; ++unit)
      units.push_back(static_cast<unichar>(*unit));
    NSString *text = [NSString stringWithCharacters:&units[0] length:units.size()];
    // The plain-text paste replacement door, without touching the global pasteboard.
    [f.view insertText:text replacementRange:NSMakeRange(0, 0)];
    if (entry.open != EDITOR_OK)
    {
      LOKA_VERIFY(![f.view isEditable]);
      std::size_t offset = units.size();
      LOKA_VERIFY(Access::observe(*f.context, offset) == entry.open);
      f.restored(1);
    }
    std::string actual;
    LOKA_VERIFY(loka::app::testing::TextEditorAccess::document(f.node).project(actual) == EDITOR_OK);
    LOKA_VERIFY(actual == entry.logical);
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(entry.caretRow).id, entry.caretByte));
    if (entry.open == EDITOR_OK)
    {
      LOKA_VERIFY([f.view isEditable] && Access::restores(*f.context) == 0);
      [f.view setSelectedRange:NSMakeRange(0, [[f.view string] length])];
      [f.view deleteBackward:nil];
      LOKA_VERIFY(f.lines.size() == 1 && bytes(f.lines.at(0).value).empty());
    }
  }
  {
    Highlighter highlighter;
    Fixture f(1, "");
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("a\360\237\230\200z")) == EDIT_OK);
    f.context->onPropsApplied();
    {
      StateTrackerGuard guard(&f.tracker);
      f.request.set(LineCursor(f.lines.at(0).id, 3));
    }
    f.context->onPropsApplied();
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, 1));
    LOKA_VERIFY(NSEqualRanges([f.view selectedRange], NSMakeRange(1, 0)));
    f.node.props.highlighter(highlighter);
    f.context->onPropsApplied();
    f.turn();
    LOKA_VERIFY([[f.view string] isEqualToString:@"a\U0001f600z"]);
    verifyFont([[f.view textStorage] attribute:NSFontAttributeName atIndex:2 effectiveRange:0],
               (NSFont *)f.controller.textFont(Bold + Italic + FontSize<18>(), true));
    // Reload the same multibyte row through the logical projection.
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("\346\227\245")) == EDIT_OK);
    f.context->onPropsApplied();
    LOKA_VERIFY([[f.view string] isEqualToString:@"\u65e5"]);
  }
  // A storage-only split uses the multibyte BEFORE row, not the parked caret.
  for (int splitSecond = 0; splitSecond < 2; ++splitSecond)
  {
    Fixture f(2, "");
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String("\346\227\245")) == EDIT_OK);
    LOKA_VERIFY(f.lines.update(f.lines.at(1).id, String("\346\227\245")) == EDIT_OK);
    f.context->onPropsApplied();
    const ItemId a = f.lines.at(0).id, b = f.lines.at(1).id;
    [f.view setSelectedRange:NSMakeRange(splitSecond ? 1 : 2, 0)];
    MacInputDoor::textEditorSelection(*f.context);
    id delegate = [f.view delegate];
    [f.view setDelegate:nil];
    [[f.view textStorage] replaceCharactersInRange:NSMakeRange(splitSecond ? 2 : 1, 0) withString:@"\n"];
    [f.view setDelegate:delegate];
    LOKA_VERIFY(f.lines.size() == 3 && f.lines.at(0).id == a);
    LOKA_VERIFY(f.lines.at(static_cast<unsigned short>(splitSecond ? 1 : 2)).id == b);
    LOKA_VERIFY(bytes(f.lines.at(0).value) == "\346\227\245" && bytes(f.lines.at(1).value).empty());
    LOKA_VERIFY(bytes(f.lines.at(2).value) == "\346\227\245");
    LOKA_VERIFY(Access::restores(*f.context) == 0);
  }
  // NEL, U+2028 and U+2029 are row content even when AppKit splits visual fragments.
  for (unsigned i = 9; i <= 11; ++i)
  {
    Fixture f(1, "");
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String(std::string(cases[i].bytes, cases[i].length))) == EDIT_OK);
    LOKA_VERIFY((NodePropsApplier<TextEditorNode, TextEditorProps>::apply(
        &f.node, TextEditor(f.lines, f.cursor).command(f.commands).props)));
    f.context->onPropsApplied();
    NSLayoutManager *layout = [f.view layoutManager];
    const NSRange glyphs = [layout glyphRangeForTextContainer:[f.view textContainer]];
    LOKA_VERIFY(glyphs.length > 0);
    const NSRect last = [layout lineFragmentUsedRectForGlyphAtIndex:NSMaxRange(glyphs) - 1 effectiveRange:0];
    const CGFloat bottom = NSMaxY(last) + [f.view textContainerOrigin].y;
    for (int full = 0; full < 2; ++full)
    {
      NSClipView *clip = [f.scroll contentView];
      [clip setBoundsOrigin:NSMakePoint(0, 0)];
      [clip setBoundsSize:NSMakeSize([clip bounds].size.width, full ? bottom + 1 : bottom - last.size.height / 2)];
      {
        StateTrackerGuard guard(&f.tracker);
        LOKA_VERIFY(f.commands.post(EditorCommand(EditorCommand::PAGE_DOWN)) == POST_ACCEPTED);
      }
      f.context->onPropsApplied();
      const Reply<EditorCommand> reply = f.commands.reply().state()->get();
      LOKA_VERIFY(reply.kind() == (full ? Reply<EditorCommand>::GRANTED : Reply<EditorCommand>::REFUSED));
      if (!full)
        LOKA_VERIFY(reply.reason() == EDITOR_UNAVAILABLE);
      LOKA_VERIFY(f.lines.size() == 1 && bytes(f.lines.at(0).value) == std::string(cases[i].bytes, cases[i].length));
    }
  }
  for (std::size_t i = 0; i < sizeof(text_alphabet_cases::macCarets) / sizeof(text_alphabet_cases::macCarets[0]); ++i)
  {
    const text_alphabet_cases::MacCaretCase &entry = text_alphabet_cases::macCarets[i];
    Fixture f(1);
    LOKA_VERIFY(f.lines.update(f.lines.at(0).id, String(entry.before)) == EDIT_OK);
    f.context->onPropsApplied();
    id viewDelegate = [f.view delegate], storageDelegate = [[f.view textStorage] delegate];
    [f.view setDelegate:nil];
    [[f.view textStorage] setDelegate:nil];
    const std::string after(entry.after);
    NSString *text = [[[NSString alloc] initWithBytes:after.data() length:after.size()
                                             encoding:NSUTF8StringEncoding] autorelease];
    [f.view setString:text];
    std::size_t offset = entry.inputUnits;
    LOKA_VERIFY(Access::observe(*f.context,
                                offset,
                                entry.storage ? MacTextEditorContext::STORAGE_EDIT : MacTextEditorContext::VIEW_CHANGE)
                == EDITOR_OK);
    LOKA_VERIFY(offset == entry.outputUnits);
    LOKA_VERIFY(f.cursor.state()->get() == LineCursor(f.lines.at(0).id, entry.byteColumn));
    LOKA_VERIFY(bytes(f.lines.at(0).value) == after);
    [[f.view textStorage] setDelegate:storageDelegate];
    [f.view setDelegate:viewDelegate];
  }
}
