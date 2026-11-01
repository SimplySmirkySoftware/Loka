#include "platform/ToolboxUtf8Bytes.hpp"
#include "core/StringAccess.hpp"
#include "platform/StringUTF8.hpp"

ToolboxUtf8Bytes::ToolboxUtf8Bytes(const loka::core::String &source)
    : view_(),
      scratch_()
{
  const loka::core::Managed<loka::platform::String> &handle = loka::core::StringAccess::handle(source);
  if (handle.isValid() && !handle->queryUtf8(this->view_))
  {
    if (!loka::platform::CollectUtf8(source, this->scratch_))
      return;
    this->view_.bytes = this->scratch_.data();
    this->view_.length = this->scratch_.size();
  }
  if (!this->view_.bytes)
    this->view_.bytes = this->scratch_.data();
}
