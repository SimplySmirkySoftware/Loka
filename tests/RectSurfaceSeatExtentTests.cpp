#include "RectSurfaceSeatExtentTests.hpp"

#include "app/RectSurface.hpp"
#include "app/layout/FallbackControlMetrics.hpp"
#include "app/layout/LayoutHeuristics.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/controls/EditText.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "core/StateTracker.hpp"
#include "core/util/StateTrackerGuard.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "support/TestVerify.hpp"

namespace
{
  struct ChangeCount
  {
    ChangeCount()
        : value(0)
    {
    }

    static void Increment(void *userData)
    {
      ChangeCount *count = static_cast<ChangeCount *>(userData);
      if (count)
      {
        ++count->value;
      }
    }

    int value;
  };

  loka::app::scene::LayoutState layoutState(short x, short y, short width, short height)
  {
    loka::app::scene::LayoutState state;
    state.x = x;
    state.y = y;
    state.width = width;
    state.height = height;
    state.lineHeight = 10;
    state.spacing = 0;
    return state;
  }

  void verifyFrame(const loka::core::Frame &actual, int x, int y, int width, int height)
  {
    LOKA_VERIFY(actual == loka::core::Frame(x, y, width, height));
  }

  loka::app::BoxNode *fixedBox(short width, short height)
  {
    loka::app::BoxProps props;
    props.setSize(width, height);
    return new loka::app::BoxNode(props);
  }

  struct LayoutSequence
  {
    LayoutSequence()
        : next(0),
          tail(0),
          publication(0)
    {
    }

    int next;
    int tail;
    int publication;
  };

  class LayoutTailNode : public loka::app::scene::Node, public loka::app::scene::IProjectedLayoutNode
  {
  public:
    explicit LayoutTailNode(LayoutSequence *sequence)
        : sequence_(sequence)
    {
    }

    virtual loka::app::scene::IProjectedLayoutNode *asProjectedLayoutNode()
    {
      return this;
    }

    virtual short layoutProjected(loka::app::scene::IPlatformController *, loka::app::scene::LayoutState &state)
    {
      if (this->sequence_)
      {
        this->sequence_->tail = ++this->sequence_->next;
      }
      return state.y;
    }

  private:
    LayoutSequence *sequence_;
  };

  struct ReleaseContextOnExtent
  {
    ReleaseContextOnExtent()
        : surface(0),
          tailFinished(0),
          observedTailFinished(false),
          calls(0)
    {
    }

    static void OnExtent(void *userData)
    {
      ReleaseContextOnExtent *observer = static_cast<ReleaseContextOnExtent *>(userData);
      if (!observer)
      {
        return;
      }
      observer->observedTailFinished = observer->tailFinished && observer->tailFinished->tail > 0;
      ++observer->calls;
      if (observer->surface)
      {
        observer->surface->setContext(0);
      }
    }

    loka::app::RectSurfaceNode *surface;
    LayoutSequence *tailFinished;
    bool observedTailFinished;
    int calls;
  };

  void RecordExtentPublication(void *userData)
  {
    LayoutSequence *sequence = static_cast<LayoutSequence *>(userData);
    if (sequence)
    {
      sequence->publication = ++sequence->next;
    }
  }
} // namespace

void testRectSurfaceFillSeatReportsRowAllocation()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps surfaceProps;
  surfaceProps.laidOutExtent(extentState);

  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  row.addChild(fixedBox(200, 40));
  row.addChild(new loka::app::RectSurfaceNode(surfaceProps));

  platform.projectLayoutForTesting(&row, layoutState(0, 7, 500, 80));

  verifyFrame(extent.get(), 204, 7, 296, 80);
}

void testRectSurfaceExplicitSizeKeepsRowConsultationAndReportsDeclaredExtent()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> extents[2];
  loka::core::PushStateTracker tracker;
  tracker.addState(&extents[0]);
  tracker.addState(&extents[1]);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extents[0], &tracker);
  loka::app::scene::NodeState<loka::core::Frame> otherExtentState(&extents[1], &tracker);
  loka::app::RectSurfaceProps surfaceProps;
  surfaceProps.size(100, 30).laidOutExtent(extentState);
  loka::app::RectSurfaceProps otherExtentProps;
  otherExtentProps.size(100, 30).laidOutExtent(otherExtentState);
  loka::app::RectSurfaceNode *surface = new loka::app::RectSurfaceNode(surfaceProps);

  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  row.addChild(fixedBox(200, 40));
  row.addChild(surface);

  LOKA_VERIFY(loka::app::layout::preferredChildWidthForRow(surface) == 100);
  LOKA_VERIFY(surfaceProps < otherExtentProps);

  platform.projectLayoutForTesting(&row, layoutState(0, 7, 500, 80));

  verifyFrame(extents[0].get(), 204, 7, 100, 30);
}

void testRectSurfaceFillSeatReportsColumnRemainingHeight()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps surfaceProps;
  surfaceProps.laidOutExtent(extentState);

  loka::app::StackNode column((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
  column.addChild(fixedBox(100, 40));
  column.addChild(new loka::app::RectSurfaceNode(surfaceProps));

  platform.projectLayoutForTesting(&column, layoutState(5, 9, 300, 100));

  verifyFrame(extent.get(), 5, 61, 300, 48);
}

void testRectSurfaceExtentChangesOnlyDuringRailLayout()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::app::RectSurfaceModel> model;
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&model);
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  ChangeCount extentChanges;
  extent.bind(&ChangeCount::Increment, &extentChanges, false);

  loka::app::RectSurfaceProps surfaceProps;
  surfaceProps.model(&model).size(120, 70).laidOutExtent(extentState);
  loka::app::RectSurfaceNode surface(surfaceProps);
  const loka::app::scene::LayoutState state = layoutState(3, 6, 120, 70);

  platform.projectLayoutForTesting(&surface, state);
  verifyFrame(extent.get(), 3, 6, 120, 70);
  LOKA_VERIFY(extentChanges.value == 1);

  platform.projectLayoutForTesting(&surface, state);
  LOKA_VERIFY(extentChanges.value == 1);

  const loka::core::Frame sentinel(1, 2, 3, 4);
  {
    loka::core::StateTrackerGuard guard(&tracker);
    extent.set(sentinel);
  }
  LOKA_VERIFY(extentChanges.value == 2);

  {
    loka::core::StateTrackerGuard guard(&tracker);
    loka::app::RectSurfaceModel changed;
    changed.rectCount = 1;
    changed.rects[0] = loka::app::RectSprite(2, 3, 4, 5);
    model.set(changed);
  }
  surface.render(&platform);

  LOKA_VERIFY(extent.get() == sentinel);
  LOKA_VERIFY(extentChanges.value == 2);
}

void testRectSurfaceFillSeatBelowButtonInColumnTakesTheRemainder()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps surfaceProps;
  surfaceProps.laidOutExtent(extentState);

  // A Button above a fill-seat surface: every native rail advances a Column
  // past a Button by its control height plus the vertical spacing, so the
  // surface takes the rest. The Null rail must not hand the Button the
  // whole seat (SmirkBench's narrow layout, #569).
  loka::app::StackNode column((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
  column.addChild(new loka::app::ButtonNode((loka::app::ButtonProps())));
  column.addChild(new loka::app::RectSurfaceNode(surfaceProps));

  platform.projectLayoutForTesting(&column, layoutState(0, 0, 479, 300));

  const int advance = loka::app::layout::FallbackControlMetrics::kButtonHeight
                      + loka::app::layout::FallbackControlMetrics::kVerticalSpacing;
  verifyFrame(extent.get(), 0, advance, 479, 300 - advance);
}

void testRectSurfaceFillSeatBelowEditTextInColumnTakesTheRemainder()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps surfaceProps;
  surfaceProps.laidOutExtent(extentState);

  // An EditText above a fill-seat surface: the Win32 and macOS EditText
  // contexts seat the control at kEditTextHeight and advance the Column by
  // that plus the vertical spacing, so the surface takes the rest. The Null
  // rail must not hand the EditText the whole seat (the #591 shape).
  loka::app::StackNode column((loka::app::StackProps(loka::app::STACK_AXIS_COLUMN)));
  column.addChild(new loka::app::EditTextNode((loka::app::EditTextProps())));
  column.addChild(new loka::app::RectSurfaceNode(surfaceProps));

  platform.projectLayoutForTesting(&column, layoutState(0, 0, 479, 300));

  const int advance = loka::app::layout::FallbackControlMetrics::kEditTextHeight
                      + loka::app::layout::FallbackControlMetrics::kVerticalSpacing;
  verifyFrame(extent.get(), 0, advance, 479, 300 - advance);
}

void testRectSurfaceWithoutExtentStateLaysOutNormally()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::app::RectSurfaceNode surface((loka::app::RectSurfaceProps()));

  const int resultY = platform.projectLayoutForTesting(&surface, layoutState(3, 6, 120, 70));

  LOKA_VERIFY(surface.getContext() != 0);
  LOKA_VERIFY(resultY == 76);
}

void testRectSurfaceNodeStatePublicationUsesOwnerTracker()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  extentState.set(loka::core::Frame(1, 2, 3, 4));
  const bool controlDirty = tracker.consumeDirty();
  LOKA_VERIFY(controlDirty);
  loka::app::RectSurfaceProps props;
  props.laidOutExtent(extentState);
  loka::app::RectSurfaceNode surface(props);
  loka::app::scene::LayoutState seat;
  seat.x = 3;
  seat.y = 6;
  seat.width = 120;
  seat.height = 70;
  seat.lineHeight = 10;
  seat.spacing = 0;
  platform.projectLayoutForTesting(&surface, seat);
  LOKA_VERIFY(extent.get() == loka::core::Frame(3, 6, 120, 70));
  const bool publishedDirty = tracker.consumeDirty();
  LOKA_VERIFY(publishedDirty);
}

void testRectSurfaceExtentPublicationCanReleaseContextAfterLayout()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps props;
  props.size(120, 70).laidOutExtent(extentState);
  loka::app::RectSurfaceNode *surface = new loka::app::RectSurfaceNode(props);
  LayoutSequence sequence;
  ReleaseContextOnExtent observer;
  observer.surface = surface;
  observer.tailFinished = &sequence;
  extent.bind(&ReleaseContextOnExtent::OnExtent, &observer, false);

  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  row.addChild(surface);
  row.addChild(new LayoutTailNode(&sequence));
  platform.projectLayoutForTesting(&row, layoutState(3, 6, 240, 70));

  LOKA_VERIFY(observer.calls == 1);
  LOKA_VERIFY(observer.observedTailFinished);
  LOKA_VERIFY(surface->getContext() == 0);
}

void testRectSurfaceExtentPublicationFollowsLayoutTraversal()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps props;
  props.size(120, 70).laidOutExtent(extentState);
  LayoutSequence sequence;
  extent.bind(&RecordExtentPublication, &sequence, false);

  loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
  row.addChild(new loka::app::RectSurfaceNode(props));
  row.addChild(new LayoutTailNode(&sequence));
  platform.projectLayoutForTesting(&row, layoutState(3, 6, 240, 70));

  LOKA_VERIFY(sequence.tail > 0);
  LOKA_VERIFY(sequence.publication > sequence.tail);
}

void testRectSurfaceScrollViewExtentUsesContentCoordinates()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> offset(loka::core::Frame(0, 7, 0, 0));
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&offset);
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> offsetState(&offset, &tracker);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps props;
  props.laidOutExtent(extentState);
  loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offsetState)));
  scrollView.addChild(new loka::app::RectSurfaceNode(props));

  platform.projectLayoutForTesting(&scrollView, layoutState(10, 20, 100, 40));

  verifyFrame(extent.get(), 10, 20, 100, 40);
}

namespace
{
  // A watcher on the first surface's fact records a newer extent for the
  // second surface and flushes it (a nested layout pass), while the outer
  // flush still holds the second surface's older entry.
  struct NestedLedgerRecorder
  {
    NestedLedgerRecorder()
        : ledger(0),
          later(0),
          calls(0)
    {
    }

    static void OnFirstExtent(void *userData)
    {
      NestedLedgerRecorder *self = static_cast<NestedLedgerRecorder *>(userData);
      if (!self || !self->ledger || !self->later)
      {
        return;
      }
      ++self->calls;
      if (self->calls == 1)
      {
        self->ledger->record(self->later, loka::core::Frame(0, 0, 20, 20));
        self->ledger->flush();
      }
    }

    loka::app::RectSurfaceExtentLedger *ledger;
    loka::app::RectSurfaceNode *later;
    int calls;
  };
} // namespace

void testRectSurfaceExtentLedgerNestedFlushKeepsNewerEntry()
{
  loka::core::MutableState<loka::core::Frame> firstExtent;
  loka::core::MutableState<loka::core::Frame> laterExtent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&firstExtent);
  tracker.addState(&laterExtent);
  loka::app::scene::NodeState<loka::core::Frame> firstState(&firstExtent, &tracker);
  loka::app::scene::NodeState<loka::core::Frame> laterState(&laterExtent, &tracker);
  loka::app::RectSurfaceProps firstProps;
  firstProps.laidOutExtent(firstState);
  loka::app::RectSurfaceProps laterProps;
  laterProps.laidOutExtent(laterState);
  loka::app::RectSurfaceNode first(firstProps);
  loka::app::RectSurfaceNode later(laterProps);

  loka::app::RectSurfaceExtentLedger ledger;
  NestedLedgerRecorder recorder;
  recorder.ledger = &ledger;
  recorder.later = &later;
  firstExtent.bind(&NestedLedgerRecorder::OnFirstExtent, &recorder, false);

  ledger.record(&first, loka::core::Frame(0, 0, 10, 10));
  ledger.record(&later, loka::core::Frame(0, 0, 2, 2));
  ledger.flush();

  LOKA_VERIFY(recorder.calls == 1);
  LOKA_VERIFY(firstExtent.get() == loka::core::Frame(0, 0, 10, 10));
  LOKA_VERIFY(laterExtent.get() == loka::core::Frame(0, 0, 20, 20));
}

// A refused layout (seat minus clamped origin beyond short range) places nothing,
// so no seat fact is published for the surface.
void testRectSurfaceRefusedProjectionPublishesNoExtent()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> offset(loka::core::Frame(0, 40000, 0, 0));
  loka::core::MutableState<loka::core::Frame> extent(loka::core::Frame(1, 2, 3, 4));
  loka::core::PushStateTracker tracker;
  tracker.addState(&offset);
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> offsetState(&offset, &tracker);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps props;
  props.laidOutExtent(extentState);
  loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offsetState)));
  scrollView.addChild(new loka::app::RectSurfaceNode(props));

  platform.projectLayoutForTesting(&scrollView, layoutState(10, -10, 100, 40));

  verifyFrame(extent.get(), 1, 2, 3, 4);
}

// A ScrollView whose content overflows the short range refuses after its
// children were placed; the seats recorded under that scope are not facts.
void testRectSurfaceRefusedScrollViewContentPublishesNoExtent()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> offset(loka::core::Frame(0, 0, 0, 0));
  loka::core::MutableState<loka::core::Frame> extent(loka::core::Frame(1, 2, 3, 4));
  loka::core::PushStateTracker tracker;
  tracker.addState(&offset);
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> offsetState(&offset, &tracker);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps props;
  props.size(100, 40).laidOutExtent(extentState);
  loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offsetState)));
  scrollView.addChild(fixedBox(100, 32730));
  scrollView.addChild(new loka::app::RectSurfaceNode(props));

  platform.projectLayoutForTesting(&scrollView, layoutState(10, 20, 100, 0));

  verifyFrame(extent.get(), 1, 2, 3, 4);
}

namespace
{
  // A watcher on the first surface's fact detaches or retires the second
  // surface (a synchronous recomposition parking or removing it) while the
  // flush that delivered the first is still holding the second's row.
  struct RetireSiblingOnExtent
  {
    RetireSiblingOnExtent()
        : sibling(0),
          detachFact(0),
          calls(0)
    {
    }

    static void OnExtent(void *userData)
    {
      RetireSiblingOnExtent *self = static_cast<RetireSiblingOnExtent *>(userData);
      if (!self)
      {
        return;
      }
      ++self->calls;
      if (self->calls == 1 && self->sibling && self->detachFact)
      {
        // A retained detach (or a retire) delivers its fact to the context,
        // which stays alive; the node remains in the tree.
        loka::app::scene::NodeContext *context = self->sibling->getContext();
        if (context)
        {
          context->onFactChanged(loka::app::scene::NODE_FACT_ATTACHED, *self->detachFact);
        }
      }
    }

    loka::app::RectSurfaceNode *sibling;
    const loka::app::scene::NodeLifecycleFact *detachFact;
    int calls;
  };
} // namespace


namespace
{
  void verifySiblingDetachedDuringDeliveryPublishesNoExtent(loka::app::scene::NodeLifecycleFact fact)
  {
    // Declared first so it outlives the nodes: their contexts deliver the
    // terminal fact to this controller when the nodes are destroyed.
    NullScenePlatformController platform;
    loka::core::MutableState<loka::core::Frame> firstExtent;
    loka::core::MutableState<loka::core::Frame> siblingExtent(loka::core::Frame(1, 2, 3, 4));
    loka::core::PushStateTracker tracker;
    tracker.addState(&firstExtent);
    tracker.addState(&siblingExtent);
    loka::app::scene::NodeState<loka::core::Frame> firstState(&firstExtent, &tracker);
    loka::app::scene::NodeState<loka::core::Frame> siblingState(&siblingExtent, &tracker);
    loka::app::RectSurfaceProps firstProps;
    firstProps.size(100, 40).laidOutExtent(firstState);
    loka::app::RectSurfaceProps siblingProps;
    siblingProps.size(100, 40).laidOutExtent(siblingState);
    loka::app::RectSurfaceNode *sibling = new loka::app::RectSurfaceNode(siblingProps);
    RetireSiblingOnExtent observer;
    observer.sibling = sibling;
    observer.detachFact = &fact;
    firstExtent.bind(&RetireSiblingOnExtent::OnExtent, &observer, false);

    loka::app::StackNode row((loka::app::StackProps(loka::app::STACK_AXIS_ROW)));
    row.addChild(new loka::app::RectSurfaceNode(firstProps));
    row.addChild(sibling);
    platform.projectLayoutForTesting(&row, layoutState(0, 0, 300, 40));

    LOKA_VERIFY(observer.calls == 1);
    LOKA_VERIFY(firstExtent.get() == loka::core::Frame(0, 0, 100, 40));
    verifyFrame(siblingExtent.get(), 1, 2, 3, 4);
  }
} // namespace

// The detach facts reach the context while it stays alive (a retained branch
// parks the surface; a retire runs the ritual before reclaim), so the
// cancellation must ride the fact, not the destructor.
void testRectSurfaceDetachedRetainedDuringDeliveryPublishesNoExtent()
{
  verifySiblingDetachedDuringDeliveryPublishesNoExtent(loka::app::scene::NODE_FACT_DETACHED_RETAINED);
}

void testRectSurfaceRetireFactDuringDeliveryPublishesNoExtent()
{
  verifySiblingDetachedDuringDeliveryPublishesNoExtent(loka::app::scene::NODE_FACT_RETIRED);
}

namespace
{
  // The first surface's watcher cancels that same surface's rows while two
  // more surfaces are still pending (the surface detached itself).
  struct CancelSelfOnExtent
  {
    CancelSelfOnExtent()
        : ledger(0),
          self(0),
          calls(0)
    {
    }

    static void OnExtent(void *userData)
    {
      CancelSelfOnExtent *observer = static_cast<CancelSelfOnExtent *>(userData);
      if (!observer)
      {
        return;
      }
      ++observer->calls;
      if (observer->ledger && observer->self)
      {
        observer->ledger->cancel(observer->self);
      }
    }

    loka::app::RectSurfaceExtentLedger *ledger;
    loka::app::RectSurfaceNode *self;
    int calls;
  };
} // namespace

void testRectSurfaceExtentLedgerCancelDuringDeliveryKeepsNextRow()
{
  loka::core::MutableState<loka::core::Frame> extents[3];
  loka::core::PushStateTracker tracker;
  loka::app::RectSurfaceProps props[3];
  for (int i = 0; i < 3; ++i)
  {
    tracker.addState(&extents[i]);
    props[i].laidOutExtent(loka::app::scene::NodeState<loka::core::Frame>(&extents[i], &tracker));
  }
  loka::app::RectSurfaceNode first(props[0]);
  loka::app::RectSurfaceNode second(props[1]);
  loka::app::RectSurfaceNode third(props[2]);

  loka::app::RectSurfaceExtentLedger ledger;
  CancelSelfOnExtent observer;
  observer.ledger = &ledger;
  observer.self = &first;
  extents[0].bind(&CancelSelfOnExtent::OnExtent, &observer, false);

  ledger.record(&first, loka::core::Frame(0, 0, 1, 1));
  ledger.record(&second, loka::core::Frame(0, 0, 2, 2));
  ledger.record(&third, loka::core::Frame(0, 0, 3, 3));
  ledger.flush();

  LOKA_VERIFY(observer.calls == 1);
  LOKA_VERIFY(extents[0].get() == loka::core::Frame(0, 0, 1, 1));
  LOKA_VERIFY(extents[1].get() == loka::core::Frame(0, 0, 2, 2));
  LOKA_VERIFY(extents[2].get() == loka::core::Frame(0, 0, 3, 3));
}

#include "app/nodes/boundary/StdComposition.hpp"
#include "platform/null/NullWindow.hpp"
#include "platform/null/NullPlatformContext.hpp"
#include "support/WindowAdmissionTestApp.hpp"
#include "testing/scene/SceneTestFlow.hpp"
namespace
{
  using namespace loka::core;
  using namespace loka::app;
  using namespace loka::app::scene;
  class RailFactRoot : public BoundaryNodeFor<RailFactRoot>
  {
    struct Width : DerivedState<int>::EvalFn
    {
      const NodeState<Frame> &fact;
      explicit Width(const NodeState<Frame> &value) : fact(value) {}
      virtual int operator()() { return this->fact.get().width + 100; }
    };
  public:
    NodeState<Frame> extent;
    DerivedNodeState<int> width;
    explicit RailFactRoot(const BoundaryPropsFor<RailFactRoot> &p) : BoundaryNodeFor<RailFactRoot>(p)
    {
      this->state(this->extent, Frame(0, 0, 0, 0));
      this->derived(this->width, this->extent, new Width(this->extent));
    }
    virtual void composeNode(NodeComposition &c) { c.declare(Box().width(this->width.state())); }
  };
  class RailFactPresenter : public NullScenePlatformController
  {
  public:
    Node *layoutRoot;
    unsigned applies;
    RailFactPresenter() : layoutRoot(0), applies(0) {}
    virtual bool canSkipGlobalChangeForBoundaryLocalPaint() const { return false; }
    virtual void beginApplyCycle()
    {
      ++this->applies;
      if (this->layoutRoot)
        this->projectLayoutForTesting(this->layoutRoot, layoutState(0, 0, 50, 20));
    }
  };
}
void testSeatRailFactSettlesAtCloseAndProjectsNextAdmission()
{
  NullPlatformContext context;
  RailFactPresenter platform;
  NullWindow window(&context, WindowProps().scene(new Scene(Boundary<RailFactRoot>())), &platform);
  WindowAdmissionTestApp app(window);
  app.flush();
  Scene &scene = *window.scene();
  RailFactRoot &root = *static_cast<RailFactRoot *>(loka::dsl::testing::SceneTestAccess::rootBoundary(scene));
  RectSurfaceProps props;
  props.size(50, 20).laidOutExtent(root.extent);
  LayoutSequence sequence;
  ChangeCount publications;
  root.extent.bind(&RecordExtentPublication, &sequence, false);
  root.extent.bind(&ChangeCount::Increment, &publications, false);
  StackNode row((StackProps(STACK_AXIS_ROW)));
  RectSurfaceNode *surface = new RectSurfaceNode(props);
  row.addChild(surface);
  row.addChild(new LayoutTailNode(&sequence));
  platform.layoutRoot = &row;
  const unsigned before = platform.applies;
  Operation turn;
  scene.requestInvalidate();
  turn.settle();
  app.admitAndApplyWindows();
  // The Null layout pass (including its tail) completed before fact delivery.
  LOKA_VERIFY(surface->getContext() && sequence.tail > 0 && sequence.publication > sequence.tail);
  LOKA_VERIFY(root.extent.get() == Frame(0, 0, 50, 20) && root.width.get() == 100);
  LOKA_VERIFY(publications.value == 1 && platform.applies == before + 1);
  app.admitAndApplyWindows();
  LOKA_VERIFY(platform.applies == before + 1 && root.width.get() == 100);
  turn.close();
  LOKA_VERIFY(root.width.get() == 150 && scene.hasPendingInvalidation());
  app.reclaimWindows();
  app.operationLoop();
  LOKA_VERIFY(platform.applies == before + 2 && !scene.hasPendingInvalidation());
  LOKA_VERIFY(publications.value == 1); // Equal extent did not re-enroll or loop.
  app.operationLoop();
  LOKA_VERIFY(platform.applies == before + 2 && publications.value == 1);
  root.extent.unbind(&RecordExtentPublication, &sequence);
  root.extent.unbind(&ChangeCount::Increment, &publications);
  platform.layoutRoot = 0;
}

void testRectSurfaceScrollViewXExtentUsesContentCoordinates()
{
  // Declared first so it outlives the nodes: their contexts deliver the
  // terminal fact to this controller when the nodes are destroyed.
  NullScenePlatformController platform;
  loka::core::MutableState<loka::core::Frame> offset(loka::core::Frame(7, 0, 0, 0));
  loka::core::MutableState<loka::core::Frame> extent;
  loka::core::PushStateTracker tracker;
  tracker.addState(&offset);
  tracker.addState(&extent);
  loka::app::scene::NodeState<loka::core::Frame> offsetState(&offset, &tracker);
  loka::app::scene::NodeState<loka::core::Frame> extentState(&extent, &tracker);
  loka::app::RectSurfaceProps props;
  props.laidOutExtent(extentState);
  loka::app::ScrollViewNode scrollView((loka::app::ScrollViewProps(offsetState)));
  scrollView.addChild(new loka::app::RectSurfaceNode(props));

  platform.projectLayoutForTesting(&scrollView, layoutState(10, 20, 100, 40));

  verifyFrame(extent.get(), 10, 20, 100, 40);
}
