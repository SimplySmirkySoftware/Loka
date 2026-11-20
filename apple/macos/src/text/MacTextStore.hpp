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
      void failTextViewPreparation(unsigned count);
      bool refuseTextViewPreparation();
      enum StoreCall
      {
        NOTE_EDIT,
        GET_UNDO_MANAGER
      };
      typedef void (*StoreCallObserver)(const MacTextStore *, StoreCall);
      void observeStoreCalls(const MacTextStore *row, StoreCallObserver observer = 0);
      unsigned observedStoreCalls();
      void recordStoreCall(const MacTextStore *row, StoreCall call);
      struct TextStoreAccess;
    } // namespace testing
  } // namespace macos
} // namespace loka
#endif

#ifdef __OBJC__
#include "app/text/TextChangeNote.hpp"
#include "app/scene/projection/PlatformController.hpp"
@class NSResponder;
class MacTextDocumentViewContext;
@class NSTextStorage, NSLayoutManager, NSTextContainer, NSTextView, NSUndoManager, LokaTextStoreDelegate;
class MacScenePlatformController;

/** Controller-ledger resident. The row owns each part of the parked text network. */
class MacTextStore : public loka::app::scene::TextStoreRow
{
public:
  static loka::app::scene::TextStoreBirth create(loka::app::scene::TextStoreLedger &,
                                                 MacScenePlatformController &,
                                                 loka::app::TextStoreSource &,
                                                 MacTextStore *,
                                                 loka::app::scene::TextStoreRow *&,
                                                 loka::app::scene::TextStoreReading &);
  virtual ~MacTextStore();
  void stopMarks();
  bool read(loka::app::scene::TextStoreReading &);
  bool pending() const;
  bool apply(unsigned long);
  void commitMarkedText();
  void settleComposition();
  bool attach(MacTextDocumentViewContext &, const loka::app::TextViewPresentation &);
  void detach();
  void present(const loka::app::TextViewPresentation &);
  NSTextView *view() const
  {
    return this->view_;
  }
  /** Resolves native identity through the live delegate and the row's claim. */
  static MacTextStore *attachedRow(NSResponder *);
  static MacTextDocumentViewContext *fromNativeFocus(NSResponder *);
  loka::app::scene::TextChunkResult chunk(unsigned long &, unsigned long, char *, unsigned long, unsigned long &);
  void noteEdit(const loka::app::TextChangedRange *range)
  {
#ifdef TEST_BUILD
    loka::macos::testing::recordStoreCall(this, loka::macos::testing::NOTE_EDIT);
#endif
    this->changes_.noteEdit(range);
  }
  NSUndoManager *undoManager() const
  {
#ifdef TEST_BUILD
    loka::macos::testing::recordStoreCall(this, loka::macos::testing::GET_UNDO_MANAGER);
#endif
    return this->undo_;
  }

private:
  MacTextStore(loka::app::scene::TextStoreLedger &, MacScenePlatformController &);
  bool buildNetwork();
  bool prepareReplacement(const MacTextStore &);
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
