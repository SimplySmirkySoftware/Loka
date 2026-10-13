#include "app/core/DialogResultTransport.hpp"

void dialogResultCrossPayloadPin(loka::app::DialogResultTransport::ConfirmRegistration *registration)
{
  loka::app::DialogResultTransport::ConfirmReturnPort port(registration);
  (void)port.seal(loka::app::FileChooserResult::Canceled());
}
