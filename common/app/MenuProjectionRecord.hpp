#ifndef LOKA_APP_MENU_PROJECTION_RECORD_HPP
#define LOKA_APP_MENU_PROJECTION_RECORD_HPP

#include "app/MenuSources.hpp"
#include <cassert>

namespace loka
{
  namespace app
  {
    /** Rail-owned applied input identity. Commit only after native success;
        refusal leaves the record intact for the next admission. Revoke the
        source before freeing its overlay; clear before the base owner dies.
        Each operation is constant work and walks no rows. */
    class MenuProjectionRecord
    {
    public:
      MenuProjectionRecord() : applied_() {}

      /** Absent offers always require the rail's native-presence path. */
      bool unchanged(const MenuSources &offer) const
      {
        return !offer.absent() && offer == this->applied_;
      }
      void commit(const MenuSources &offer)
      {
        assert((offer.overlay == 0) == (offer.source == 0));
        this->applied_ = offer;
      }
      void release(const scene::Scene *scene)
      {
        if (scene && this->applied_.source == scene)
          this->clear();
      }
      void clear()
      {
        const MenuSources empty = {0, 0, 0};
        this->applied_ = empty;
      }

    private:
      MenuSources applied_;
    };
  }
}

#endif // LOKA_APP_MENU_PROJECTION_RECORD_HPP
