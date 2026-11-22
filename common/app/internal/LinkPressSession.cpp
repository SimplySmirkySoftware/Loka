#include "app/internal/LinkPressSession.hpp"

namespace loka
{
  namespace app
  {
    namespace internal
    {
      LinkPressOccupancy::LinkPressOccupancy()
          : active_(0)
      {
      }

      bool LinkPressOccupancy::enter(LinkPressRegistration &registration)
      {
        if (this->active_)
          return false;
        this->active_ = &registration;
        return true;
      }

      void LinkPressOccupancy::leave(LinkPressRegistration &registration)
      {
        assert(this->active_ == &registration);
        (void)registration;
        this->active_ = 0;
      }

      void LinkPressOccupancy::markActive()
      {
        if (this->active_)
          this->active_->invalidated_ = true;
      }

      LinkPressRegistration::LinkPressRegistration(LinkPressOccupancy &occupancy)
          : occupancy_(occupancy.enter(*this) ? &occupancy : 0),
            invalidated_(false)
      {
      }

      LinkPressRegistration::~LinkPressRegistration()
      {
        this->close();
      }

      void LinkPressRegistration::close()
      {
        if (this->occupancy_)
        {
          this->occupancy_->leave(*this);
          this->occupancy_ = 0;
        }
      }
    } // namespace internal
  } // namespace app
} // namespace loka
