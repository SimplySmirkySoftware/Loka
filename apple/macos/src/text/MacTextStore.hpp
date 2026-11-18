#ifndef LOKA_MAC_TEXT_STORE_HPP
#define LOKA_MAC_TEXT_STORE_HPP
#ifdef TEST_BUILD
class MacTextStore;
namespace loka
{
  namespace macos
  {
    namespace testing
    {
      enum StoreAllocation
      {
        BUFFER,
        STRING,
        ROW,
        STORAGE,
        LAYOUT,
        CONTAINER,
        VIEW,
        UNDO,
        DELEGATE,
        STORE_ALLOCATION_COUNT
      };
      void failStoreAllocation(StoreAllocation stage, unsigned count);
      bool refuseStoreAllocation(StoreAllocation stage);
      void observeStoreCalls(const MacTextStore *row);
      unsigned observedStoreCalls();
      void recordStoreCall(const MacTextStore *row);
      struct TextStoreAccess;
    } // namespace testing
  } // namespace macos
} // namespace loka
#endif

#ifdef __OBJC__
#include "app/text/TextChangeNote.hpp"
#include "app/scene/projection/PlatformController.hpp"
@class NSTextStorage, NSLayoutManager, NSTextContainer, NSTextView, NSUndoManager, LokaTextStoreDelegate;
class MacScenePlatformController;

/** Controller-ledger resident. The row owns each part of the parked text network. */
class MacTextStore : public loka::app::scene::TextStoreRow
{
public:
  static loka::app::scene::TextStoreBirth create(loka::app::scene::TextStoreLedger &,
                                                 MacScenePlatformController &,
                                                 loka::app::TextStoreSource &,
                                                 loka::app::scene::TextStoreRow *&,
                                                 loka::app::scene::TextStoreReading &);
  virtual ~MacTextStore();
  void stopMarks();
  bool read(loka::app::scene::TextStoreReading &);
  bool pending() const;
  bool apply(unsigned long);
  void commitMarkedText();
  loka::app::scene::TextChunkResult chunk(unsigned long &, unsigned long, char *, unsigned long, unsigned long &);
  void noteEdit(const loka::app::TextChangedRange *range)
  {
#ifdef TEST_BUILD
    loka::macos::testing::recordStoreCall(this);
#endif
    this->changes_.noteEdit(range);
  }
  NSUndoManager *undoManager() const
  {
#ifdef TEST_BUILD
    loka::macos::testing::recordStoreCall(this);
#endif
    return this->undo_;
  }

private:
  MacTextStore(loka::app::scene::TextStoreLedger &, MacScenePlatformController &);
  bool buildNetwork();
  bool matches(NSTextView *) const;
  NSTextStorage *storage_;
  NSLayoutManager *layout_;
  NSTextContainer *container_;
  NSTextView *view_;
  NSUndoManager *undo_;
  LokaTextStoreDelegate *delegate_;
  loka::app::scene::TextChangeNote changes_;
#ifdef TEST_BUILD
  friend struct loka::macos::testing::TextStoreAccess;
#endif
};
#endif
#endif
