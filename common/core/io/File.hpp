#ifndef LOKA_CORE_IO_FILE_HPP
#define LOKA_CORE_IO_FILE_HPP

#include <cassert>

#include "core/String.hpp"
#include "core/io/FileLocator.hpp"

namespace loka
{
  namespace file
  {
    struct File
    {
      enum Kind
      {
        KIND_UNKNOWN = 0,
        KIND_FILE,
        KIND_FOLDER
      };

      enum BaseKind
      {
        BASE_NONE = 0,
#if !defined(LOKA_RETRO68)
        BASE_PATH,
#endif
        BASE_APPLICATION,
        BASE_DESKTOP,
        BASE_DOCUMENTS,
        BASE_ROOT,
        /** Sticky composition refusal, with no text or locator. */
        BASE_REFUSED
      };

      File()
          : base_(BASE_NONE),
            kind_(KIND_UNKNOWN),
            basePath_(),
            relative_()
      {
      }

      explicit File(const char *name)
          : base_(BASE_NONE),
            kind_(KIND_UNKNOWN),
            basePath_(),
            relative_(loka::core::String::Literal(name ? name : ""))
      {
      }

      explicit File(const loka::core::String &name)
          : base_(BASE_NONE),
            kind_(KIND_UNKNOWN),
            basePath_(),
            relative_(name)
      {
      }

      static File Desktop()
      {
        File item;
        item.base_ = BASE_DESKTOP;
        return item;
      }

      /** The place where files shipped with the application land, per platform
          convention — not literally the executable's directory everywhere:
          Win32 resolves the directory containing the .exe, Classic the folder
          containing the application file, but macOS resolves the bundle's
          resource directory (Contents/Resources), because that is where a
          bundle carries its shipped files. The null test platform answers
          from an injected directory. Resolution happens only in
          PlatformContext::openFile; see #199. */
      static File Application()
      {
        File item;
        item.base_ = BASE_APPLICATION;
        return item;
      }

      static File Documents()
      {
        File item;
        item.base_ = BASE_DOCUMENTS;
        return item;
      }

      static File Root()
      {
        File item;
        item.base_ = BASE_ROOT;
        return item;
      }

#if !defined(LOKA_RETRO68)
      static File FromPath(const loka::core::String &value)
      {
        File item;
        item.base_ = BASE_PATH;
        item.basePath_ = value;
        return item;
      }

      static File FromPath(const char *value)
      {
        return FromPath(loka::core::String::Literal(value ? value : ""));
      }
#endif

      /** Refusal wins first, then a located parent refuses composition, then
          a located child wins. Unlocated values retain ordinary path rules. */
      File operator<<(const File &child) const
      {
        if (this->base_ == BASE_REFUSED || child.base_ == BASE_REFUSED || !this->locator_.empty())
        {
          File refused;
          refused.base_ = BASE_REFUSED;
          return refused;
        }
        if (!child.locator_.empty())
          return child;
        if (child.base_ != BASE_NONE)
        {
          return child;
        }
        File combined(*this);
        combined.appendRelative(child.relative_);
        if (child.kind_ != KIND_UNKNOWN)
        {
          combined.kind_ = child.kind_;
        }
        return combined;
      }

      /** Opaque immutable address snapshot; display text is not its identity. */
      const FileLocator &locator() const { return this->locator_; }

      Kind kind() const
      {
        return this->kind_;
      }

      BaseKind base() const
      {
        return this->base_;
      }

      const loka::core::String &relativePath() const
      {
        return this->relative_;
      }

      void setKind(Kind kind)
      {
        if (this->base_ != BASE_REFUSED)
          this->kind_ = kind;
      }

      /** Last component of this locator's text, split only on this rail's name
          separators (isNameSeparator), trailing ones ignored. Relative text
          takes precedence over the path base. No component, an unlocated
          BASE_NONE, or refusal answers empty; application-relative items need
          no resolution. A captured Classic name keeps a '/' inside it. */
      loka::core::String name() const
      {
        if (this->base_ == BASE_REFUSED || (this->base_ == BASE_NONE && this->locator_.empty()))
          return loka::core::String();
        const loka::core::String &text = this->relative_.empty() ? this->basePath_ : this->relative_;
        const loka::core::StringBuffer buffer = text.bufferWithEncoding(loka::core::StringEncodingUtf8);
        const char *bytes = static_cast<const char *>(buffer.data());
        std::size_t end = buffer.length();
        while (end && isNameSeparator(bytes[end - 1]))
          --end;
        if (!end)
          return loka::core::String();
        std::size_t start = end;
        while (start && !isNameSeparator(bytes[start - 1]))
          --start;
        return loka::core::String::Utf8(bytes + start, end - start);
      }

      loka::core::String toString() const
      {
        if (this->base_ == BASE_APPLICATION)
        {
          assert(false && "Application-relative File must be resolved through PlatformContext::openFile");
          return loka::core::String();
        }
        if (this->base_ == BASE_ROOT)
        {
          if (this->relative_.empty())
          {
            return this->separatorString();
          }
          return this->separatorString() + this->relative_;
        }
        const loka::core::String base = this->baseString();
        if (base.empty())
        {
          return this->relative_;
        }
        if (this->relative_.empty())
        {
          return base;
        }
        return base + this->separatorString() + this->relative_;
      }

    private:
      friend class loka::platform::file::FileLocatorAccess;
      friend bool operator!=(const File &lhs, const File &rhs);

      BaseKind base_;
      Kind kind_;
      loka::core::String basePath_;
      loka::core::String relative_;
      FileLocator locator_;

      static const char *separatorLiteral()
      {
#if defined(LOKA_RETRO68)
        return ":";
#else
        return "/";
#endif
      }

      /** Which bytes end a name component in this rail's sealed text: ':' on
          Classic; '/' elsewhere, plus '\\' on Win32 because its dialogs and
          FileFromWidePath seal native backslash paths unchanged while
          separatorLiteral stays '/'. */
      static bool isNameSeparator(char c)
      {
#if defined(LOKA_RETRO68)
        return c == ':';
#elif defined(_WIN32)
        return c == '/' || c == '\\';
#else
        return c == '/';
#endif
      }

      loka::core::String separatorString() const
      {
        return loka::core::String::Literal(separatorLiteral());
      }

      loka::core::String baseString() const
      {
#if !defined(LOKA_RETRO68)
        if (this->base_ == BASE_PATH)
        {
          return this->basePath_;
        }
#endif
#if defined(LOKA_RETRO68)
        if (this->base_ == BASE_DESKTOP)
        {
          return loka::core::String::Literal("Desktop");
        }
        if (this->base_ == BASE_DOCUMENTS)
        {
          return loka::core::String::Literal("Documents");
        }
#else
        if (this->base_ == BASE_DESKTOP)
        {
          return loka::core::String::Literal("~/Desktop");
        }
        if (this->base_ == BASE_DOCUMENTS)
        {
          return loka::core::String::Literal("~/Documents");
        }
#endif
        return loka::core::String();
      }

      void appendRelative(const loka::core::String &segment)
      {
        if (segment.empty())
        {
          return;
        }
        if (this->relative_.empty())
        {
          this->relative_ = segment;
          return;
        }
        this->relative_ = this->relative_ + this->separatorString() + segment;
      }
    };

    inline bool operator!=(const File &lhs, const File &rhs)
    {
      if (lhs.locator_ != rhs.locator_)
        return true;
      if (lhs.kind_ != rhs.kind_)
      {
        return true;
      }
      if (lhs.base_ != rhs.base_)
      {
        return true;
      }
      if (lhs.basePath_.equals(rhs.basePath_) == false)
      {
        return true;
      }
      if (lhs.relative_.equals(rhs.relative_) == false)
      {
        return true;
      }
      return false;
    }

  } // namespace file
} // namespace loka

#endif // LOKA_CORE_IO_FILE_HPP
