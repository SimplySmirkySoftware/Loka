#ifndef LOKA_APP_WINDOW_SEAT_HPP
#define LOKA_APP_WINDOW_SEAT_HPP

#include "app/core/DocumentRoster.hpp"
#include "app/core/DocumentFacts.hpp"
#include "app/core/WindowDefinition.hpp"
#include "app/scene/KeySnapshot.hpp"

namespace loka
{
  namespace app
  {
    /** App-owned keyed consumer. The config owns the borrowed roster and
        outlives the App. Only App admission advances the committed snapshot. */
    class WindowSeat
    {
    public:
      explicit WindowSeat(DocumentRosterBase &roster)
          : key_(&roster.revision(), loka::core::ListRevision()), roster_(roster) {}
      virtual ~WindowSeat() {}
      const scene::KeySnapshot<loka::core::ListRevision> &key() const { return this->key_; }
      DocumentRosterBase &roster() const { return this->roster_; }
      virtual Window *createWindow(loka::core::ItemId id, PlatformContext *context) const = 0;
      /** Copies by value: any edit can shift the entry in EntryBuffer storage.
          Returns false when the id is no longer in the roster. */
      virtual bool copyFacts(loka::core::ItemId id, DocumentFacts &out) const = 0;
      virtual bool anyDocumentEdited() const = 0;
      virtual unsigned short snapshotDesired() = 0;
      virtual loka::core::ItemId desiredAt(unsigned short index) const = 0;

    private:
      friend class ::App;
      scene::KeySnapshot<loka::core::ListRevision> key_;
      DocumentRosterBase &roster_;
      WindowSeat(const WindowSeat &);
      WindowSeat &operator=(const WindowSeat &);
    };

    /** Typed factory and one walk's identity copy. The callback target (the
        config) outlives the App, as in RunApp; userData is a borrowed target,
        never an owner. No retry, decrease, or refusal ledger is kept here.
        The id is the document's own identity in the roster, the same key
        find resolves; a window that keeps it can close its own document. */
    template <class T, unsigned short N> class DocumentWindowSeat : public WindowSeat
    {
      static char factsConversion(const DocumentFacts *);
      static long factsConversion(...);
      typedef char document_requires_facts[sizeof(factsConversion(static_cast<const T *>(0))) == sizeof(char) ? 1 : -1];

    public:
      typedef WindowProps (*Factory)(loka::core::ItemId, const T &, void *);
      DocumentWindowSeat(DocumentRoster<T, N> &roster, Factory factory, void *userData)
          : WindowSeat(roster), factory_(factory), userData_(userData)
      {
        assert(factory);
      }
      virtual Window *createWindow(loka::core::ItemId id, PlatformContext *context) const
      {
        const T *entry = static_cast<DocumentRoster<T, N> &>(this->roster()).find(id);
        if (!entry)
          return 0;
        WindowProps props = this->factory_(id, *entry, this->userData_);
        assert(!props.titleStatePtr && !props.displayTitleStatePtr && "a keyed window's title is roster-owned (#1245)");
        // The factory may edit the roster and move EntryBuffer storage.
        entry = static_cast<DocumentRoster<T, N> &>(this->roster()).find(id);
        if (!entry)
          return 0;
        props.title(entry->title);
        return WindowDef(props).create(context);
      }
      virtual bool copyFacts(loka::core::ItemId id, DocumentFacts &out) const
      {
        const T *entry = static_cast<DocumentRoster<T, N> &>(this->roster()).find(id);
        if (!entry)
          return false;
        out = *entry;
        return true;
      }
      virtual bool anyDocumentEdited() const
      {
        return static_cast<const DocumentRoster<T, N> &>(this->roster()).anyOf(&DocumentWindowSeat::isEdited);
      }
      virtual unsigned short snapshotDesired()
      {
        const unsigned short count = this->roster().count();
        for (unsigned short i = 0; i < count; ++i)
          this->desired_[i] = this->roster().idAt(i);
        return count;
      }
      virtual loka::core::ItemId desiredAt(unsigned short index) const { return this->desired_[index]; }

    private:
      static bool isEdited(const T &entry)
      {
        return static_cast<const DocumentFacts &>(entry).edited;
      }
      Factory const factory_;
      void *const userData_;
      loka::core::ItemId desired_[N];
    };

    /** A composition declaration creates one independently owned runtime seat. */
    class DocumentWindowSeatDefinitionBase
    {
    public:
      virtual ~DocumentWindowSeatDefinitionBase() {}
      virtual WindowSeat *createSeat() const = 0;
    };

    /** Copyable declaration; the roster and callback target are config-owned
        and must outlive the App. Creating the seat copies the factory values. */
    template <class T, unsigned short N>
    class DocumentWindowSeatDefinition : public DocumentWindowSeatDefinitionBase
    {
    public:
      typedef typename DocumentWindowSeat<T, N>::Factory Factory;
      DocumentWindowSeatDefinition(DocumentRoster<T, N> &roster, Factory factory, void *userData)
          : roster_(roster), factory_(factory), userData_(userData) {}
      virtual WindowSeat *createSeat() const
      {
        return new (std::nothrow) DocumentWindowSeat<T, N>(this->roster_, this->factory_, this->userData_);
      }

    private:
      DocumentRoster<T, N> &roster_;
      Factory const factory_;
      void *const userData_;
    };

    template <class T, unsigned short N>
    DocumentWindowSeatDefinition<T, N> DocumentWindows(
        DocumentRoster<T, N> &roster, WindowProps (*factory)(loka::core::ItemId, const T &, void *), void *userData)
    {
      return DocumentWindowSeatDefinition<T, N>(roster, factory, userData);
    }
  } // namespace app
} // namespace loka

#endif
