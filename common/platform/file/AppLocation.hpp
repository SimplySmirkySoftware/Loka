#ifndef LOKA_PLATFORM_FILE_APP_LOCATION_HPP
#define LOKA_PLATFORM_FILE_APP_LOCATION_HPP

#include <string>

#include "core/io/File.hpp"
#include "platform/StringUTF8.hpp"
#include "platform/file/FileHandle.hpp"

namespace loka
{
  namespace platform
  {
    namespace file
    {
      /**
       * Reports whether an application-relative item names a file at most one folder deep.
       *
       * Every platform shares this predicate so an application-relative File
       * has one portable meaning: one or two plain names, joined by the
       * rail separator. Foreign separators, traversal and empty names refuse.
       */
      inline bool ApplicationRelativeIsOpenable(const loka::file::File &item)
      {
        if (item.base() != loka::file::File::BASE_APPLICATION)
        {
          return false;
        }

        std::string relative;
        if (!loka::platform::CollectUtf8(item.relativePath(), relative))
        {
          return false;
        }
        // Keep the composition separator parallel with File::separatorLiteral.
#if defined(LOKA_RETRO68)
        const char separator = ':';
#else
        const char separator = '/';
#endif
        std::size_t start = 0;
        unsigned int components = 0;
        for (std::size_t i = 0; i <= relative.size(); ++i)
        {
          if (i == relative.size() || relative[i] == separator)
          {
            const std::string name = relative.substr(start, i - start);
            if (++components > 2 || name.empty() || name == "." || name == "..")
              return false;
            start = i + 1;
          }
          else if (relative[i] == '/' || relative[i] == '\\' || relative[i] == ':' || relative[i] == '\0')
            return false;
        }
        return true;
      }

      /**
       * Resolves an application-relative item using the platform convention.
       *
       * @return False when the item is not application-relative, its relative
       *         name is refused, or the platform cannot locate the application.
       */
      bool ResolveApplicationItem(const loka::file::File &item, FileHandle &out);

      /**
       * Resolves a peer intended for writable sidecar data rather than
       * packaged application content.
       *
       * The query declines when the platform cannot identify a currently
       * writable peer location. OpenWriteTruncate remains the authoritative
       * write boundary because availability can change after resolution.
       */
      bool ResolveApplicationSidecar(const loka::file::File &item, FileHandle &out);
    } // namespace file
  } // namespace platform
} // namespace loka

#endif // LOKA_PLATFORM_FILE_APP_LOCATION_HPP
