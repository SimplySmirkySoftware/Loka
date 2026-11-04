#ifndef LOKA_APP_FOCUS_PUBLISHER_HPP
#define LOKA_APP_FOCUS_PUBLISHER_HPP

#include "app/FocusParticipant.hpp"
class Window;
namespace loka { namespace app { namespace scene { class IPlatformController; } } }

namespace loka
{
  namespace app
  {
    namespace detail
    {
      /** Policy twin of TextDocumentPublisher (independent document facts).
          Stateless take and publication policy, entered through Window completion.
          Completion audits same-Scene placement for inspected bound rows.
          Publication and published binding exchange additionally audit attached
          duplicates and facts simultaneously published by another live Scene. */
      class FocusPublisher
      {
        static void complete(scene::SceneFocus &current, scene::IPlatformController &platform, scene::Node &root);
        static scene::NodeContext *resolve(scene::SceneFocus &current, const FocusBinding &identity);
        static scene::NodeContext *publishedContext(scene::SceneFocus &current);
        static void reconcile(scene::SceneFocus &current, bool answered, scene::NodeContext *target);
        static void leave(FocusParticipant &row);
        static void rebind(FocusParticipant &row, const FocusBinding &previous);
        static void exchange(scene::SceneFocus &current, FocusParticipant *target, const FocusBinding &previous);
        static void audit(scene::SceneFocus &current, const FocusParticipant &target);
        static bool attached(const FocusParticipant &row);
        friend class ::Window;
        friend class ::loka::app::FocusParticipant;
      };
    } // namespace detail
  } // namespace app
} // namespace loka
#endif
