#ifndef LOKA_APP_DOCUMENT_FACTS_HPP
#define LOKA_APP_DOCUMENT_FACTS_HPP

#include "core/String.hpp"

namespace loka
{
  namespace app
  {
    /** The chrome facts of one document, owned by the roster entry and written
        by the document's node through DocumentRoster::update. App admission
        copies these facts into the Window (t1). */
    struct DocumentFacts
    {
      loka::core::String title;
      bool edited;

      DocumentFacts()
          : title(),
            edited(false)
      {
      }
      DocumentFacts(const loka::core::String &titleValue, bool editedValue)
          : title(titleValue),
            edited(editedValue)
      {
      }
    };
  } // namespace app
} // namespace loka

#endif
