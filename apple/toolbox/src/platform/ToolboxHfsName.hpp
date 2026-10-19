#ifndef LOKA_TOOLBOX_PLATFORM_TOOLBOX_HFS_NAME_HPP
#define LOKA_TOOLBOX_PLATFORM_TOOLBOX_HFS_NAME_HPP

#include <Files.h>

#include "core/String.hpp"

namespace loka
{
  namespace toolbox
  {
    /**
     * Copies a logical string into a classic HFS Str63 file name.
     *
     * Refuses empty names, names longer than 31 MacRoman bytes, malformed
     * UTF-8, and characters that MacRoman cannot represent.
     */
    bool CopyStringToHfsName(const loka::core::String &value, Str63 out);

    /** Builds a colon-prefixed application partial pathname. Each component
        uses CopyStringToHfsName; the complete Pascal payload must fit 63 bytes.
        The caller must first pass ApplicationRelativeIsOpenable. */
    bool CopyApplicationRelativeToHfsPath(const loka::core::String &value, Str63 out);

  } // namespace toolbox
} // namespace loka

#endif // LOKA_TOOLBOX_PLATFORM_TOOLBOX_HFS_NAME_HPP
