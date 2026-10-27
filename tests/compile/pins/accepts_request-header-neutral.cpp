#include "app/scene/state/Request.hpp"
struct LocalRequest
{
};
namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <> struct RequestTraits<LocalRequest>
      {
        enum
        {
          coalescable = 1
        };
      };
    } // namespace scene
  } // namespace app
} // namespace loka
void pin()
{
  loka::app::scene::Request<LocalRequest> request;
  (void)request;
}
