#ifndef LOKA_APP_EDIT_VERB_HPP
#define LOKA_APP_EDIT_VERB_HPP

namespace loka
{
  namespace app
  {
    /** Standard native editor actions. A refusal by a target is still handled. */
    enum EditVerb
    {
      EDIT_UNDO,
      EDIT_CUT,
      EDIT_COPY,
      EDIT_PASTE,
      EDIT_SELECT_ALL
    };
  } // namespace app
} // namespace loka
#endif
