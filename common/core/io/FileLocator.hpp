#ifndef LOKA_CORE_IO_FILELOCATOR_HPP
#define LOKA_CORE_IO_FILELOCATOR_HPP

#include <cstring>
#include "core/Managed.hpp"

namespace loka
{
  namespace platform { namespace file { class FileLocatorAccess; } }
  namespace file
  {
    /** Immutable platform address snapshot. Copies share sealed counted bytes;
        equality compares contents, not allocation identity. None allocates nothing. */
    class FileLocator
    {
    public:
      FileLocator() : payload_() {}

      /** Equal representation implies equal value, not the reverse. Never allocates. */
      bool sharesRepresentation(const FileLocator &other) const
      {
        return this->payload_ == other.payload_;
      }

      bool empty() const { return !this->payload_.isValid(); }

      bool operator==(const FileLocator &other) const
      {
        if (this->payload_ == other.payload_)
          return true;
        if (this->empty() || other.empty())
          return false;
        return this->payload_->size == other.payload_->size &&
               std::memcmp(this->payload_->bytes(), other.payload_->bytes(), this->payload_->size) == 0;
      }

      bool operator!=(const FileLocator &other) const { return !(*this == other); }

    private:
      friend class loka::platform::file::FileLocatorAccess;

      struct Payload
      {
        explicit Payload(std::size_t count) : size(count) {}
        const unsigned char *bytes() const
        {
          return reinterpret_cast<const unsigned char *>(this + 1);
        }
        const std::size_t size;
      };

      loka::core::Managed<Payload> payload_;
    };
  }
}

#endif
