#ifndef LOKA_WIN32_TEXT_STORE_HPP
#define LOKA_WIN32_TEXT_STORE_HPP
#include <windows.h>
#include "app/text/TextChangeNote.hpp"
#include "app/scene/projection/PlatformController.hpp"
class Win32TextParking;
namespace loka
{
  namespace win32
  {
    namespace testing
    {
#ifdef TEST_BUILD
      void failTextParkingCreations(unsigned);
      void failTextLocalAllocations(unsigned);
      void failTextEditCreations(unsigned);
      void failTextDpiQualifications(unsigned);
      bool refuseTextParkingCreation();
      bool refuseTextLocalAllocation();
      bool refuseTextEditCreation();
      bool refuseTextDpiQualification();
#endif
      struct TextStoreAccess;
    } // namespace testing
  } // namespace win32
} // namespace loka
/** Owns the EDIT and its change note; the parking ledger owns this resident. */
class Win32TextStore : public loka::app::scene::TextStoreRow
{
public:
  static loka::app::scene::TextStoreBirth create(Win32TextParking &,
                                                 loka::app::scene::IPlatformController &,
                                                 loka::app::TextStoreSource &,
                                                 loka::app::scene::TextStoreRow *&,
                                                 loka::app::scene::TextStoreReading &);
  virtual ~Win32TextStore();
  /** Parking notifications only mark; no kernel entry or allocation. */
  void noteEdit()
  {
    this->changes_.noteEdit(0);
  }
  void stopMarks();
  bool read(loka::app::scene::TextStoreReading &);
  bool pending();
  bool apply(unsigned long);
  loka::app::scene::TextChunkResult chunk(unsigned long &, unsigned long, char *, unsigned long, unsigned long &);

private:
  Win32TextStore(loka::app::scene::TextStoreLedger &ledger, loka::app::scene::IPlatformController &controller)
      : TextStoreRow(ledger, controller),
        edit_(0)
  {
  }
  bool matches(HWND edit) const;
  HWND edit_;
  loka::app::scene::TextChangeNote changes_;
  friend struct loka::win32::testing::TextStoreAccess;
};
#endif
