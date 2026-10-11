#include "ObservableListTests.hpp"
#include "support/TestVerify.hpp"
#include "core/MirroredList.hpp"
#include <cstdlib>
#include <cstring>

namespace loka
{
  namespace core
  {
    namespace testing
    {
      struct ObservableListAccessForTesting
      {
        template <class T> static void exhaustSequenceForTesting(ObservableList<T> &list)
        {
          list.seq_ = 65534;
        }
        template <class T> static void exhaustGenerationForTesting(ObservableList<T> &list)
        {
          list.generation_ = 65534;
        }
        template <class T> static void exhaustProvisionalSequenceForTesting(MirroredList<T> &mirror)
        {
          mirror.provisionalSeq_ = 65534;
        }
      };
    } // namespace testing
  } // namespace core
} // namespace loka

namespace
{
  using namespace loka::core;
  int allocCalls = 0, freeCalls = 0, liveItems = 0, livePages = 0, refuseCall = 0;
  bool refusePages = false;
  void *allocateListTest(std::size_t size, const LokaAllocationSite &site)
  {
    ++allocCalls;
    if (allocCalls == refuseCall || (refusePages && std::strcmp(site.typeTag, "OpPage") == 0))
      return 0;
    void *p = std::malloc(size);
    if (p)
    {
      if (std::strcmp(site.typeTag, "OpPage") == 0)
        ++livePages;
      else
        ++liveItems;
      LOKA_VERIFY(std::strcmp(site.ownerTag, "ObservableList") == 0 || std::strcmp(site.ownerTag, "MirroredList") == 0);
    }
    return p;
  }
  void freeListTest(void *p, const LokaAllocationSite &site)
  {
    ++freeCalls;
    if (std::strcmp(site.typeTag, "OpPage") == 0)
      --livePages;
    else
      --liveItems;
    std::free(p);
  }
  struct GateScope
  {
    GateScope()
    {
      allocCalls = freeCalls = liveItems = livePages = refuseCall = 0;
      refusePages = false;
      LokaAllocSetBackend(&allocateListTest, &freeListTest);
    }
    ~GateScope()
    {
      LOKA_VERIFY(liveItems == 0 && livePages == 0);
      LokaAllocSetBackend(0, 0);
    }
  };
  struct RevisionProbe
  {
    ObservableList<int> &list;
    ListRevision expected;
    unsigned short expectedSize;
    int expectedFirstValue;
    int calls;
    bool reenter;
    RevisionProbe(ObservableList<int> &value)
        : list(value),
          expected(),
          expectedSize(0),
          expectedFirstValue(0),
          calls(0),
          reenter(false)
    {
      const_cast<State<ListRevision> &>(this->list.revision()).bind(&changed, this, false);
    }
    ~RevisionProbe()
    {
      const_cast<State<ListRevision> &>(this->list.revision()).unbind(&changed, this);
    }
    void expect(unsigned long structure,
                unsigned long content,
                ListChangeKind kind,
                unsigned short first,
                unsigned short count,
                unsigned short size,
                int firstValue)
    {
      this->expected.structure = structure;
      this->expected.content = content;
      this->expected.change = ListChange(kind, first, count);
      this->expectedSize = size;
      this->expectedFirstValue = firstValue;
    }
    static void changed(void *data)
    {
      RevisionProbe &p = *static_cast<RevisionProbe *>(data);
      ++p.calls;
      LOKA_VERIFY(!(p.list.revision().get() != p.expected));
      LOKA_VERIFY(p.list.size() == p.expectedSize);
      if (p.expectedSize)
        LOKA_VERIFY(p.list.at(0).value == p.expectedFirstValue);
      for (unsigned short i = 0; i < p.list.size(); ++i)
        LOKA_VERIFY(!p.list.at(i).id.isNone());
      if (p.reenter)
      {
        LOKA_VERIFY(p.list.insert(0, 90) == EDIT_REENTRANT);
        LOKA_VERIFY(p.list.remove(ItemId()) == EDIT_REENTRANT);
        LOKA_VERIFY(p.list.update(ItemId(), 90) == EDIT_REENTRANT);
        LOKA_VERIFY(p.list.move(ItemId(), 0) == EDIT_REENTRANT);
        LOKA_VERIFY(p.list.reset() == EDIT_REENTRANT);
        LOKA_VERIFY(p.list.detach() == EDIT_REENTRANT);
        ArrayListOpCursor<int> empty(0, 0);
        LOKA_VERIFY(p.list.apply(empty) == EDIT_REENTRANT);
      }
    }
  };
  void invalidateCount(void *data)
  {
    ++*static_cast<int *>(data);
  }
} // namespace

void testObservableListAttachRefusalAndLifetime()
{
  GateScope gate;
  PushStateTracker tracker;
  ObservableList<int> list;
  RevisionProbe probe(list);
  LOKA_VERIFY(list.attach(0, 4) == ATTACH_NULL_TRACKER);
  for (int refusal = 1; refusal <= 2; ++refusal)
  {
    refuseCall = allocCalls + refusal;
    LOKA_VERIFY(list.attach(&tracker, 4) == ATTACH_ALLOCATION_REFUSED);
    LOKA_VERIFY(liveItems == 0);
    LOKA_VERIFY(list.insert(0, 1) == EDIT_NOT_ATTACHED);
    LOKA_VERIFY(list.remove(ItemId()) == EDIT_NOT_ATTACHED);
    LOKA_VERIFY(list.update(ItemId(), 1) == EDIT_NOT_ATTACHED);
    LOKA_VERIFY(list.move(ItemId(), 0) == EDIT_NOT_ATTACHED);
    LOKA_VERIFY(list.reset() == EDIT_NOT_ATTACHED);
    ArrayListOpCursor<int> empty(0, 0);
    LOKA_VERIFY(list.apply(empty) == EDIT_NOT_ATTACHED);
    LOKA_VERIFY(list.capacity() == 0 && list.size() == 0 && probe.calls == 0);
  }
  refuseCall = 0;
  const int before = allocCalls;
  LOKA_VERIFY(list.attach(&tracker, 4) == ATTACH_OK);
  LOKA_VERIFY(allocCalls == before + 2 && liveItems == 2);
  LOKA_VERIFY(list.attach(&tracker, 4) == ATTACH_ALREADY_ATTACHED);
  LOKA_VERIFY(list.detach() == EDIT_OK);
  LOKA_VERIFY(list.detach() == EDIT_OK);
  LOKA_VERIFY(liveItems == 0 && probe.calls == 0);
  LOKA_VERIFY(list.attach(&tracker, 0) == ATTACH_OK);
  LOKA_VERIFY(list.insert(0, 1) == EDIT_CAPACITY_EXCEEDED);
}

void testObservableListPublishesCompletedRevisions()
{
  GateScope gate;
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 4) == ATTACH_OK);
  RevisionProbe probe(list);
  ItemId a, b;
  probe.expect(1, 0, LIST_INSERT, 0, 1, 1, 10);
  LOKA_VERIFY(list.insert(0, 10, &a) == EDIT_OK);
  probe.expect(2, 0, LIST_INSERT, 1, 1, 2, 10);
  LOKA_VERIFY(list.insert(1, 20, &b) == EDIT_OK);
  probe.expect(3, 0, LIST_MOVE, 1, 1, 2, 20);
  LOKA_VERIFY(list.move(a, 1) == EDIT_OK);
  LOKA_VERIFY(list.at(0).id == b && list.at(1).value == 10);
  probe.expect(3, 1, LIST_UPDATE, 0, 1, 2, 30);
  LOKA_VERIFY(list.update(b, 30) == EDIT_OK);
  LOKA_VERIFY(list.at(0).value == 30);
  probe.expect(4, 1, LIST_REMOVE, 1, 1, 1, 30);
  LOKA_VERIFY(list.remove(a) == EDIT_OK);
  LOKA_VERIFY(probe.calls == 5 && allocCalls == 2);
  LOKA_VERIFY(tracker.committedSet().identities().size() == 1);
  LOKA_VERIFY(tracker.committedSet().identities()[0] == &list.revision());
}

void testObservableListCapacityAndAtomicBatch()
{
  GateScope gate;
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 2) == ATTACH_OK);
  ItemId a, b;
  LOKA_VERIFY(list.insert(0, 10, &a) == EDIT_OK);
  LOKA_VERIFY(list.insert(1, 20, &b) == EDIT_OK);
  const ListRevision full = list.revision().get();
  ItemId output(7, 7);
  LOKA_VERIFY(list.insert(2, 30, &output) == EDIT_CAPACITY_EXCEEDED);
  LOKA_VERIFY(output == ItemId(7, 7) && list.size() == 2 && !(list.revision().get() != full));
  LOKA_VERIFY(list.remove(b) == EDIT_OK);
  ListOp<int> ops[] = {
      ListOp<int>(INSERT, ItemId(), 1, 20), ListOp<int>(INSERT, ItemId(), 2, 30), ListOp<int>(REMOVE, a)};
  ArrayListOpCursor<int> cursor(ops, 3);
  const ListRevision before = list.revision().get();
  unsigned char bytes[sizeof(ObservableList<int>::Entry)];
  std::memcpy(bytes, &list.at(0), sizeof(bytes));
  LOKA_VERIFY(list.apply(cursor) == EDIT_CAPACITY_EXCEEDED);
  LOKA_VERIFY(list.size() == 1 && !(list.revision().get() != before));
  assert(std::memcmp(bytes, &list.at(0), sizeof(bytes)) == 0);
  ops[0] = ListOp<int>(UPDATE, a, 0, 99);
  ops[1] = ListOp<int>(REMOVE, b);
  ArrayListOpCursor<int> invalid(ops, 2);
  LOKA_VERIFY(list.apply(invalid) == EDIT_ID_NOT_FOUND);
  assert(std::memcmp(bytes, &list.at(0), sizeof(bytes)) == 0);
  LOKA_VERIFY(!(list.revision().get() != before));
  LOKA_VERIFY(list.insert(2, 2) == EDIT_INDEX_OUT_OF_RANGE);
  LOKA_VERIFY(list.move(a, 1) == EDIT_INDEX_OUT_OF_RANGE);
  LOKA_VERIFY(list.update(b, 2) == EDIT_ID_NOT_FOUND);
  LOKA_VERIFY(list.remove(b) == EDIT_ID_NOT_FOUND);
  LOKA_VERIFY(allocCalls == 2);
}

void testObservableListIdentitiesAndExhaustion()
{
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(sizeof(ItemId) == 4 && sizeof(ListRevision) <= 32);
  LOKA_VERIFY(ItemId::none().isNone());
  LOKA_VERIFY(list.attach(&tracker, 3) == ATTACH_OK);
  ItemId a, b, c;
  LOKA_VERIFY(list.insert(0, 1, &a) == EDIT_OK);
  LOKA_VERIFY(list.remove(a) == EDIT_OK);
  LOKA_VERIFY(list.insert(0, 1, &b) == EDIT_OK);
  LOKA_VERIFY(b.seq == a.seq + 1 && b.generation == a.generation);
  LOKA_VERIFY(list.reset() == EDIT_OK);
  LOKA_VERIFY(list.insert(0, 1, &c) == EDIT_OK);
  LOKA_VERIFY(c.generation == b.generation + 1 && c.seq == 1);
  LOKA_VERIFY(list.remove(b) == EDIT_ID_NOT_FOUND);
  testing::ObservableListAccessForTesting::exhaustSequenceForTesting(list);
  LOKA_VERIFY(list.insert(1, 2, &b) == EDIT_OK);
  LOKA_VERIFY(b.seq == 65535);
  const ListRevision before = list.revision().get();
  LOKA_VERIFY(list.insert(2, 3) == EDIT_SEQ_EXHAUSTED);
  ListOp<int> op(INSERT, ItemId(), 2, 3);
  ArrayListOpCursor<int> cursor(&op, 1);
  LOKA_VERIFY(list.apply(cursor) == EDIT_SEQ_EXHAUSTED);
  LOKA_VERIFY(!(list.revision().get() != before));
  LOKA_VERIFY(list.detach() == EDIT_OK);
  LOKA_VERIFY(list.attach(&tracker, 3) == ATTACH_OK);
  LOKA_VERIFY(list.insert(0, 1, &b) == EDIT_OK);
  LOKA_VERIFY(b.generation == c.generation + 1 && b.seq == 1);
  testing::ObservableListAccessForTesting::exhaustGenerationForTesting(list);
  LOKA_VERIFY(list.reset() == EDIT_GENERATION_EXHAUSTED);
  LOKA_VERIFY(list.detach() == EDIT_OK);
  LOKA_VERIFY(list.attach(&tracker, 3) == ATTACH_GENERATION_EXHAUSTED);
}

void testObservableListApplySummariesAndReplay()
{
  GateScope gate;
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 3) == ATTACH_OK);
  ItemId a, b;
  LOKA_VERIFY(list.insert(0, 1, &a) == EDIT_OK);
  LOKA_VERIFY(list.insert(1, 2, &b) == EDIT_OK);
  RevisionProbe probe(list);
  ListOp<int> ops[] = {ListOp<int>(UPDATE, a, 0, 10),
                       ListOp<int>(MOVE, a, 1),
                       ListOp<int>(REMOVE, b),
                       ListOp<int>(INSERT, ItemId(), 1, 30)};
  ArrayListOpCursor<int> cursor(ops, 4);
  probe.expect(3, 1, LIST_BATCH, 0, 0, 2, 10);
  LOKA_VERIFY(list.apply(cursor) == EDIT_OK);
  LOKA_VERIFY(list.at(0).id == a && list.at(0).value == 10 && list.at(1).value == 30);
  LOKA_VERIFY(list.at(1).id.seq == 3);
  ops[0] = ListOp<int>(UPDATE, a, 0, 11);
  ArrayListOpCursor<int> single(ops, 1);
  probe.expect(3, 2, LIST_UPDATE, 0, 1, 2, 11);
  LOKA_VERIFY(list.apply(single) == EDIT_OK);
  ArrayListOpCursor<int> empty(0, 0);
  LOKA_VERIFY(list.apply(empty) == EDIT_OK);
  LOKA_VERIFY(probe.calls == 2 && allocCalls == 2);
  // Net-zero structural work still applies two operations and advances once.
  ops[0] = ListOp<int>(INSERT, ItemId(), 2, 40);
  ops[1] = ListOp<int>(REMOVE, ItemId(a.generation, 4));
  ArrayListOpCursor<int> pair(ops, 2);
  probe.expect(4, 2, LIST_BATCH, 0, 0, 2, 11);
  LOKA_VERIFY(list.apply(pair) == EDIT_OK);
  LOKA_VERIFY(list.size() == 2 && probe.calls == 3);
}

void testObservableListRejectsReentrantDoors()
{
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 2) == ATTACH_OK);
  RevisionProbe probe(list);
  probe.reenter = true;
  probe.expect(1, 0, LIST_INSERT, 0, 1, 1, 1);
  LOKA_VERIFY(list.insert(0, 1) == EDIT_OK);
  LOKA_VERIFY(probe.calls == 1 && list.at(0).value == 1);
}

void testMirroredListProvisionalIdExhaustionIsMirrorLocal()
{
  GateScope gate;
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 8) == ATTACH_OK);
  MirroredList<int> mirror(list);
  LOKA_VERIFY(mirror.status().kind == MIRROR_OK);
  testing::ObservableListAccessForTesting::exhaustProvisionalSequenceForTesting(mirror);
  ItemId last;
  LOKA_VERIFY(mirror.insert(0, 1, &last).kind == MIRROR_OK);
  LOKA_VERIFY(last.generation == 65535 && last.seq == 65535);
  const ListRevision before = list.revision().get();
  const MirrorResult exhausted = mirror.insert(1, 2);
  LOKA_VERIFY(exhausted.kind == MIRROR_SEQ_EXHAUSTED && exhausted.edit == EDIT_OK);
  LOKA_VERIFY(mirror.opCount() == 1 && mirror.size() == 1);
  LOKA_VERIFY(!(list.revision().get() != before) && list.size() == 0);
  LOKA_VERIFY(mirror.commit().kind == MIRROR_COMMITTED && list.size() == 1);
}

void testMirroredListPagesRefusalUndoAndCancel()
{
  GateScope gate;
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 20) == ATTACH_OK);
  MirroredList<int> mirror(list);
  LOKA_VERIFY(mirror.status().kind == MIRROR_OK);
  ItemId first;
  LOKA_VERIFY(mirror.insert(0, 0, &first).kind == MIRROR_OK);
  for (unsigned short i = 1; i < 8; ++i)
    LOKA_VERIFY(mirror.insert(i, i).kind == MIRROR_OK);
  refusePages = true;
  ItemId output(9, 9);
  LOKA_VERIFY(mirror.insert(8, 8, &output).kind == MIRROR_PAGE_REFUSED);
  LOKA_VERIFY(output == ItemId(9, 9) && mirror.opCount() == 8 && mirror.size() == 8 && livePages == 0);
  refusePages = false;
  for (unsigned short i = 8; i < 18; ++i)
    LOKA_VERIFY(mirror.insert(i, i).kind == MIRROR_OK);
  LOKA_VERIFY(livePages == 2 && mirror.opCount() == 18);
  LOKA_VERIFY(mirror.update(first, 99).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.undo().kind == MIRROR_OK);
  LOKA_VERIFY(mirror.at(0).value == 0 && mirror.size() == 18);
  LOKA_VERIFY(mirror.undo().kind == MIRROR_OK);
  LOKA_VERIFY(mirror.undo().kind == MIRROR_OK);
  LOKA_VERIFY(mirror.opCount() == 16 && livePages == 1);
  for (unsigned short i = 0; i < mirror.size(); ++i)
    LOKA_VERIFY(mirror.at(i).value == i);
  LOKA_VERIFY(mirror.cancel().kind == MIRROR_OK);
  LOKA_VERIFY(livePages == 0 && mirror.size() == 0 && mirror.opCount() == 0);
  LOKA_VERIFY(mirror.undo().kind == MIRROR_NOTHING_TO_UNDO);
}

void testMirroredListCommitMapsProvisionalIds()
{
  GateScope gate;
  PushStateTracker tracker;
  int commits = 0;
  tracker.setInvalidateCallback(&invalidateCount, &commits);
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 20) == ATTACH_OK);
  ItemId base;
  LOKA_VERIFY(list.insert(0, 1, &base) == EDIT_OK);
  MirroredList<int> mirror(list);
  ItemId provisional, removed;
  LOKA_VERIFY(mirror.insert(1, 2, &provisional).kind == MIRROR_OK);
  LOKA_VERIFY(provisional.generation == 65535);
  LOKA_VERIFY(mirror.insert(2, 3, &removed).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.remove(removed).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.update(provisional, 22).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.move(provisional, 0).kind == MIRROR_OK);
  for (int i = 0; i < 12; ++i)
    LOKA_VERIFY(mirror.update(base, i).kind == MIRROR_OK);
  LOKA_VERIFY(livePages == 2 && list.size() == 1);
  RevisionProbe probe(list);
  probe.expect(2, 1, LIST_BATCH, 0, 0, 2, 22);
  const int allocations = allocCalls;
  commits = 0;
  LOKA_VERIFY(mirror.commit().kind == MIRROR_COMMITTED);
  LOKA_VERIFY(commits == 1 && probe.calls == 1 && allocCalls == allocations);
  LOKA_VERIFY(list.at(0).value == 22 && list.at(0).id == ItemId(base.generation, 2));
  LOKA_VERIFY(list.at(1).id == base && list.at(1).value == 11);
  LOKA_VERIFY(mirror.at(0).id == list.at(0).id && mirror.opCount() == 0 && livePages == 0 && !mirror.isStale());
  LOKA_VERIFY(mirror.undo().kind == MIRROR_NOTHING_TO_UNDO);
}

void testMirroredListStaleReplayAndRefusal()
{
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 4) == ATTACH_OK);
  ItemId a, b;
  LOKA_VERIFY(list.insert(0, 1, &a) == EDIT_OK);
  LOKA_VERIFY(list.insert(1, 2, &b) == EDIT_OK);
  MirroredList<int> mirror(list);
  LOKA_VERIFY(mirror.update(a, 10).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.move(a, 1).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.remove(a).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.update(b, 20).kind == MIRROR_OK);
  LOKA_VERIFY(list.remove(a) == EDIT_OK);
  LOKA_VERIFY(mirror.isStale());
  LOKA_VERIFY(mirror.undo().kind == MIRROR_STALE);
  LOKA_VERIFY(mirror.opCount() == 4);
  LOKA_VERIFY(mirror.commit().kind == MIRROR_COMMITTED);
  LOKA_VERIFY(list.size() == 1 && list.at(0).id == b && list.at(0).value == 20);
  LOKA_VERIFY(list.revision().get().change.kind == LIST_UPDATE);
  LOKA_VERIFY(mirror.insert(1, 30).kind == MIRROR_OK);
  LOKA_VERIFY(list.remove(b) == EDIT_OK);
  const ListRevision before = list.revision().get();
  MirrorResult failed = mirror.commit();
  LOKA_VERIFY(failed.kind == MIRROR_MODEL_REFUSED && failed.edit == EDIT_INDEX_OUT_OF_RANGE);
  LOKA_VERIFY(mirror.opCount() == 1 && mirror.size() == 2 && !(list.revision().get() != before));
  LOKA_VERIFY(mirror.cancel().kind == MIRROR_OK);
  LOKA_VERIFY(mirror.insert(0, 40, &a).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.update(a, 41).kind == MIRROR_OK);
  LOKA_VERIFY(list.reset() == EDIT_OK);
  LOKA_VERIFY(mirror.commit().kind == MIRROR_COMMITTED);
  LOKA_VERIFY(list.at(0).value == 41 && list.at(0).id.generation != 65535);
  LOKA_VERIFY(mirror.update(list.at(0).id, 42).kind == MIRROR_OK);
  LOKA_VERIFY(list.update(list.at(0).id, 99) == EDIT_OK);
  LOKA_VERIFY(mirror.undo().kind == MIRROR_STALE);
  LOKA_VERIFY(mirror.commit().kind == MIRROR_COMMITTED);
  LOKA_VERIFY(list.at(0).value == 42);
}

void testMirroredListConstructionAndAttachmentChanges()
{
  GateScope gate;
  PushStateTracker tracker;
  ObservableList<int> list;
  MirroredList<int> unattached(list);
  LOKA_VERIFY(unattached.status().kind == MIRROR_MODEL_REFUSED);
  LOKA_VERIFY(unattached.insert(0, 1).kind == MIRROR_NOT_USABLE);
  LOKA_VERIFY(list.attach(&tracker, 10) == ATTACH_OK);
  refuseCall = allocCalls + 1;
  MirroredList<int> refused(list);
  LOKA_VERIFY(refused.status().kind == MIRROR_ALLOCATION_REFUSED);
  LOKA_VERIFY(refused.commit().kind == MIRROR_NOT_USABLE);
  refuseCall = 0;
  {
    MirroredList<int> mirror(list);
    for (unsigned short i = 0; i < 10; ++i)
      LOKA_VERIFY(mirror.insert(i, i).kind == MIRROR_OK);
    LOKA_VERIFY(livePages == 1);
    LOKA_VERIFY(list.detach() == EDIT_OK);
    LOKA_VERIFY(mirror.commit().edit == EDIT_NOT_ATTACHED);
    LOKA_VERIFY(mirror.opCount() == 10);
    LOKA_VERIFY(mirror.cancel().edit == EDIT_NOT_ATTACHED);
    LOKA_VERIFY(livePages == 0);
    LOKA_VERIFY(list.attach(&tracker, 11) == ATTACH_OK);
    LOKA_VERIFY(mirror.commit().kind == MIRROR_STALE);
  }
  {
    MirroredList<int> mirror(list);
    for (unsigned short i = 0; i < 10; ++i)
      LOKA_VERIFY(mirror.insert(i, i).kind == MIRROR_OK);
    LOKA_VERIFY(livePages == 1);
  }
  LOKA_VERIFY(livePages == 0);
}

namespace
{
  void reenterMirror(void *data)
  {
    MirroredList<int> &mirror = *static_cast<MirroredList<int> *>(data);
    LOKA_VERIFY(mirror.cancel().kind == MIRROR_REENTRANT);
    LOKA_VERIFY(mirror.undo().kind == MIRROR_REENTRANT);
    LOKA_VERIFY(mirror.commit().kind == MIRROR_REENTRANT);
    LOKA_VERIFY(mirror.insert(0, 99).kind == MIRROR_REENTRANT);
  }
} // namespace
void testMirroredListRejectsReentrantLogChanges()
{
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 2) == ATTACH_OK);
  MirroredList<int> mirror(list);
  LOKA_VERIFY(mirror.insert(0, 1).kind == MIRROR_OK);
  State<ListRevision> &revision = const_cast<State<ListRevision> &>(list.revision());
  revision.bind(&reenterMirror, &mirror, false);
  LOKA_VERIFY(mirror.commit().kind == MIRROR_COMMITTED);
  revision.unbind(&reenterMirror, &mirror);
  LOKA_VERIFY(list.size() == 1 && mirror.opCount() == 0);
}

namespace
{
  struct SettlementProbe
  {
    ObservableList<int> *list;
    int calls;
    static void invalidate(void *data)
    {
      SettlementProbe &probe = *static_cast<SettlementProbe *>(data);
      ++probe.calls;
      LOKA_VERIFY(probe.list->detach() == EDIT_REENTRANT);
      LOKA_VERIFY(probe.list->insert(0, 99) == EDIT_REENTRANT);
    }
  };
} // namespace
void testObservableListProtectsTrackerSettlement()
{
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 2) == ATTACH_OK);
  SettlementProbe probe = {&list, 0};
  tracker.setInvalidateCallback(&SettlementProbe::invalidate, &probe);
  MirroredList<int> mirror(list);
  LOKA_VERIFY(mirror.insert(0, 7).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.commit().kind == MIRROR_COMMITTED);
  LOKA_VERIFY(probe.calls == 1 && mirror.size() == 1 && mirror.at(0).value == 7);
}

void testObservableListNestedTransactionsAndFailedSequence()
{
  PushStateTracker tracker;
  int commits = 0;
  tracker.setInvalidateCallback(&invalidateCount, &commits);
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 3) == ATTACH_OK);
  ItemId id;
  {
    StateTrackerGuard outer(&tracker);
    LOKA_VERIFY(list.insert(0, 1, &id) == EDIT_OK);
    LOKA_VERIFY(list.update(id, 2) == EDIT_OK);
    LOKA_VERIFY(commits == 0);
  }
  LOKA_VERIFY(commits == 1);
  ListOp<int> ops[] = {ListOp<int>(INSERT, ItemId(), 1, 3), ListOp<int>(REMOVE, ItemId(99, 99))};
  ArrayListOpCursor<int> invalid(ops, 2);
  LOKA_VERIFY(list.apply(invalid) == EDIT_ID_NOT_FOUND);
  ItemId next;
  LOKA_VERIFY(list.insert(1, 3, &next) == EDIT_OK);
  LOKA_VERIFY(next.seq == id.seq + 1);
  ops[0] = ListOp<int>(UPDATE, id, 0, 5);
  ops[1] = ListOp<int>(UPDATE, next, 0, 6);
  ArrayListOpCursor<int> updates(ops, 2);
  const ListRevision before = list.revision().get();
  LOKA_VERIFY(list.apply(updates) == EDIT_OK);
  LOKA_VERIFY(list.revision().get().structure == before.structure);
  LOKA_VERIFY(list.revision().get().content == before.content + 1);
  LOKA_VERIFY(list.revision().get().change.kind == LIST_BATCH);
}

void testMirroredListAllSkippedAndCapacityRefusal()
{
  PushStateTracker tracker;
  ObservableList<int> list;
  LOKA_VERIFY(list.attach(&tracker, 2) == ATTACH_OK);
  ItemId id;
  LOKA_VERIFY(list.insert(0, 1, &id) == EDIT_OK);
  MirroredList<int> mirror(list);
  LOKA_VERIFY(mirror.remove(id).kind == MIRROR_OK);
  LOKA_VERIFY(list.remove(id) == EDIT_OK);
  const ListRevision before = list.revision().get();
  LOKA_VERIFY(mirror.commit().kind == MIRROR_COMMITTED);
  LOKA_VERIFY(mirror.opCount() == 0 && mirror.size() == 0 && !(list.revision().get() != before));
  LOKA_VERIFY(mirror.insert(0, 2).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.insert(1, 3).kind == MIRROR_OK);
  LOKA_VERIFY(mirror.insert(2, 4).edit == EDIT_CAPACITY_EXCEEDED);
  LOKA_VERIFY(mirror.move(ItemId(99, 99), 0).edit == EDIT_ID_NOT_FOUND);
  LOKA_VERIFY(mirror.move(mirror.at(0).id, 2).edit == EDIT_INDEX_OUT_OF_RANGE);
  LOKA_VERIFY(mirror.update(ItemId(99, 99), 4).edit == EDIT_ID_NOT_FOUND);
  LOKA_VERIFY(mirror.opCount() == 2 && mirror.size() == 2);
  LOKA_VERIFY(list.insert(0, 4) == EDIT_OK);
  MirrorResult failed = mirror.commit();
  LOKA_VERIFY(failed.kind == MIRROR_MODEL_REFUSED && failed.edit == EDIT_CAPACITY_EXCEEDED);
  LOKA_VERIFY(mirror.opCount() == 2 && mirror.size() == 2 && list.size() == 1);
}
