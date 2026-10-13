#ifndef LOKA_TEST_DIALOG_RESULT_ACCESS_HPP
#define LOKA_TEST_DIALOG_RESULT_ACCESS_HPP
#include "app/core/DialogResultTransport.hpp"

namespace loka
{
  namespace app
  {
    namespace testing
    {
      class DialogResultTestAccess
      {
      public:
        static size_t census(const DialogResultTransport &transport)
        {
          const DialogResultTransport::Chain *chains[] = {
              &transport.reserved_, &transport.pending_, &transport.active_, &transport.retired_};
          size_t count = 0;
          for (size_t i = 0; i != 4; ++i)
            for (DialogResultTransport::DialogRequest *entry = chains[i]->head; entry; entry = entry->next)
              ++count;
          return count;
        }
      };
    } // namespace testing
  } // namespace app
} // namespace loka

#endif
