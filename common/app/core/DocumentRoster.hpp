#ifndef LOKA_APP_DOCUMENT_ROSTER_HPP
#define LOKA_APP_DOCUMENT_ROSTER_HPP

#include "core/ObservableList.hpp"
#include "core/StateTracker.hpp"

class App;

namespace loka
{
  namespace app
  {
    /** Read-only document identities; only the App consumes window removal facts. */
    class DocumentRosterBase
    {
    public:
      virtual ~DocumentRosterBase() {}
      virtual const loka::core::State<loka::core::ListRevision> &revision() const = 0;
      virtual unsigned short count() const = 0;
      virtual loka::core::ItemId idAt(unsigned short index) const = 0;

    private:
      friend class ::App;
      virtual void windowGone(loka::core::ItemId id) = 0;
    };

    /** Config-owned documents and their tracker. The config outlives the App.
        List edits supply their own transaction guard, joining the active turn
        or using the standalone fallback before launch. Attachment refusal
        leaves an empty detached roster whose open door returns none and whose
        close door returns the list's refusal. */
    template <class T, unsigned short N> class DocumentRoster : public DocumentRosterBase
    {
    public:
      DocumentRoster() { this->documents_.attach(&this->tracker_, N); }

      loka::core::ItemId open(const T &entry)
      {
        loka::core::ItemId id;
        if (this->documents_.insert(this->count(), entry, &id) != loka::core::EDIT_OK)
          return loka::core::ItemId::none();
        return id;
      }
      loka::core::ListEditResult close(loka::core::ItemId id)
      {
        return this->documents_.remove(id);
      }
      const T *find(loka::core::ItemId id) const
      {
        const int index = this->documents_.find(id);
        return index < 0 ? 0 : &this->documents_.at(static_cast<unsigned short>(index)).value;
      }
      loka::core::ListEditResult update(loka::core::ItemId id, const T &entry)
      {
        return this->documents_.update(id, entry);
      }
      virtual const loka::core::State<loka::core::ListRevision> &revision() const
      {
        return this->documents_.revision();
      }
      virtual unsigned short count() const { return this->documents_.size(); }
      virtual loka::core::ItemId idAt(unsigned short index) const { return this->documents_.at(index).id; }

    private:
      loka::core::PushStateTracker tracker_;
      loka::core::ObservableList<T> documents_;

      virtual void windowGone(loka::core::ItemId id)
      {
        const loka::core::ListEditResult result = this->documents_.remove(id);
        assert(result == loka::core::EDIT_OK || result == loka::core::EDIT_ID_NOT_FOUND);
        (void)result;
      }
      DocumentRoster(const DocumentRoster &);
      DocumentRoster &operator=(const DocumentRoster &);
    };
  } // namespace app
} // namespace loka

#endif
