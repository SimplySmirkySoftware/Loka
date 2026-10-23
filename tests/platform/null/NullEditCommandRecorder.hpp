#ifndef LOKA_TESTING_NULL_EDIT_COMMAND_RECORDER_HPP
#define LOKA_TESTING_NULL_EDIT_COMMAND_RECORDER_HPP
#include "app/EditVerb.hpp"
#include <vector>
namespace loka
{
  namespace testing
  {
    /** Test rail payload, owned and reclaimed with its editor context. */
    class NullEditCommandRecorder
    {
    public:
      bool record(app::EditVerb verb)
      {
        this->verbs_.push_back(verb);
        return true;
      }
      const std::vector<app::EditVerb> &verbs() const
      {
        return this->verbs_;
      }

    private:
      std::vector<app::EditVerb> verbs_;
    };
  } // namespace testing
} // namespace loka
#endif
