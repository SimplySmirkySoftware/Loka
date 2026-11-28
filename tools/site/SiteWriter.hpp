#ifndef DOCUMENT_SITE_WRITER_HPP
#define DOCUMENT_SITE_WRITER_HPP

#include "app/doc/Render.hpp"
#include <cstdio>
#include <cstdlib>
#include <list>
#include <new>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#if defined(TEST_BUILD) || defined(LOKA_DIAG)
#error DocumentSite requires the common shipping configuration
#endif

namespace document_site
{
  /** Cost: once per page, scan its title and mount, settle, walk and tear down its own document once. */
  template <class Page> bool renderPage(const loka::core::String &title, const Page &page, std::string &out)
  {
    namespace doc = loka::app::doc;
    std::string bytes;
    if (!doc::detail::CollectDocumentText(title, bytes))
      return false;
    // EdgeSpace handles document spaces after line folding. Include the remaining Unicode whitespace here.
    size_t pos = 0;
    unsigned scalar;
    do
    {
      if (pos == bytes.size())
        return false;
      loka::core::detail::DecodeUtf8Codepoint(bytes.data(), bytes.size(), pos, scalar);
    } while (doc::detail::EdgeSpace(scalar) || (scalar >= 11 && scalar <= 13) || scalar == 0x85 || scalar == 0x2028
             || scalar == 0x2029);

    std::string staged = "<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
                         "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n<title>";
    for (size_t i = 0; i < bytes.size(); ++i)
      doc::detail::AppendHtmlByte(staged, bytes[i], false);
    staged += "</title>\n<link rel=\"stylesheet\" href=\"site.css\">\n</head>\n<body>\n<main>";
    if (!doc::RenderHtml(page, staged))
      return false;
    staged += "</main>\n</body>\n</html>\n";
    out += staged;
    return true;
  }

  /** Cost: addImage and writeSite each read one source into an empty buffer. A zero limit admits any size. */
  inline bool readFile(const char *path, size_t limit, std::string &bytes)
  {
    FILE *file = path ? std::fopen(path, "rb") : 0;
    if (!file)
      return false;
    char chunk[8192];
    bool read;
    for (;;)
    {
      const size_t remaining = limit ? limit - bytes.size() : 0;
      const size_t request = limit && remaining < sizeof(chunk) ? remaining + 1 : sizeof(chunk);
      const size_t count = std::fread(chunk, 1, request, file);
      if (limit && count > remaining)
      {
        read = false;
        break;
      }
      bytes.append(chunk, count);
      if (count < request)
      {
        read = !std::ferror(file);
        break;
      }
    }
    return std::fclose(file) == 0 && read;
  }

  /** Owns one unique sibling temporary until publication or scope exit. */
  class SiteFile
  {
    const std::string destination_;
    std::string temporary_;

    SiteFile(const SiteFile &);
    SiteFile &operator=(const SiteFile &);

  public:
    /** Cost: writeSite joins one final path per output. */
    SiteFile(const std::string &directory, const char *name)
        : destination_(directory + "/" + name)
    {
    }

    /** Cost: once per output on return, unlink its unpublished temporary if present. */
    ~SiteFile()
    {
      if (!this->temporary_.empty())
        std::remove(this->temporary_.c_str());
    }

    /** Cost: once per output, write its in-memory bytes and close its unique temporary. */
    bool stage(const std::string &bytes)
    {
      const std::string pattern = this->destination_ + ".XXXXXX";
      std::vector<char> path(pattern.begin(), pattern.end());
      path.push_back('\0');
      const int fd = mkstemp(&path[0]);
      if (fd < 0)
        return false;
      this->temporary_ = &path[0];
      // mkstemp creates the file for its owner only; a published page is world-readable.
      FILE *file = fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) == 0 ? fdopen(fd, "wb") : 0;
      if (!file)
      {
        close(fd);
        return false;
      }
      const size_t count = std::fwrite(bytes.data(), 1, bytes.size(), file);
      return std::fclose(file) == 0 && count == bytes.size();
    }

    /** Cost: once per staged output after all writes succeed, rename one directory entry. */
    bool publish()
    {
      if (std::rename(this->temporary_.c_str(), this->destination_.c_str()) != 0)
        return false;
      this->temporary_.clear();
      return true;
    }
  };

  /** Owns every pending file through staging and publication. */
  class StagedFiles
  {
    std::vector<SiteFile *> files_;
    StagedFiles(const StagedFiles &);
    StagedFiles &operator=(const StagedFiles &);

  public:
    /** Cost: writeSite reserves one pointer per planned file once per site. */
    explicit StagedFiles(size_t count)
    {
      this->files_.reserve(count);
    }

    /** Cost: writeSite scope exit destroys each owned file once. */
    ~StagedFiles()
    {
      for (size_t i = 0; i < this->files_.size(); ++i)
        delete this->files_[i];
    }

    /** Cost: writeSite calls once per output, acquiring one file and staging its bytes. */
    bool stage(const std::string &directory, const char *name, const std::string &bytes)
    {
      SiteFile *file = new (std::nothrow) SiteFile(directory, name);
      if (!file)
        return false;
      this->files_.push_back(file);
      return file->stage(bytes);
    }

    /** Cost: writeSite calls once after staging, visiting this holder's files in order until a rename fails. */
    bool publish()
    {
      for (size_t i = 0; i < this->files_.size(); ++i)
        if (!this->files_[i]->publish())
          return false;
      return true;
    }
  };

  /** Rendered pages and captured images awaiting publication. */
  class SiteOutputs
  {
    typedef std::list<std::pair<std::string, std::string> > Outputs;
    Outputs pages_;
    Outputs images_;
    /** Cost: writeSite calls once per collection, staging its files in insertion order. */
    static bool stage(const Outputs &outputs, StagedFiles &files, const std::string &directory)
    {
      for (Outputs::const_iterator entry = outputs.begin(); entry != outputs.end(); ++entry)
        if (!files.stage(directory, entry->first.c_str(), entry->second))
          return false;
      return true;
    }

    friend bool writeSite(const std::string &, const char *, const SiteOutputs &);

    /**
     * A name is a stem of lowercase ASCII letters, digits and '-', starting with a letter or digit, followed by
     * the given suffix. No two such names can denote one file: there is no path separator, no "." or "..", no
     * case to fold, no trailing dot to drop, and the stylesheet's name is not of this shape. Page and image
     * names cannot collide with each other because their suffixes differ.
     * Cost: each admission checks a fixed number of suffixes and walks the matching stem once.
     */
    template <size_t N> static bool hasStemAndSuffix(const std::string &name, const char (&ending)[N])
    {
      const size_t suffix = N - 1;
      if (name.size() <= suffix || name.compare(name.size() - suffix, suffix, ending) != 0)
        return false;
      for (size_t i = 0; i < name.size() - suffix; ++i)
      {
        const char c = name[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!alnum && (i == 0 || c != '-'))
          return false;
      }
      return true;
    }

    /** Cost: one lookup per admissible name, scanning only the collection that can contain it. */
    static bool contains(const Outputs &outputs, const std::string &name)
    {
      for (Outputs::const_iterator entry = outputs.begin(); entry != outputs.end(); ++entry)
        if (entry->first == name)
          return true;
      return false;
    }

  public:
    /** Cost: addPage calls once per rendered page, scanning this site's page names and copying the admitted bytes. */
    bool add(const std::string &name, const std::string &bytes)
    {
      if (!hasStemAndSuffix(name, ".html") || contains(this->pages_, name))
        return false;
      this->pages_.push_back(std::make_pair(name, bytes));
      return true;
    }

    /** Cost: once per offered image, check its name and capture up to 8 MiB of trusted source bytes. */
    bool addImage(const std::string &name, const char *sourcePath)
    {
      if (!(hasStemAndSuffix(name, ".png") || hasStemAndSuffix(name, ".jpg") || hasStemAndSuffix(name, ".gif")))
        return false;
      if (contains(this->images_, name))
        return false;
      std::string bytes;
      if (!readFile(sourcePath, 8 * 1024 * 1024, bytes))
        return false;
      this->images_.push_back(std::make_pair(name, std::string()));
      this->images_.back().second.swap(bytes);
      return true;
    }
  };

  /** Cost: main calls once per page, rendering its own boundary and offering the completed bytes once. */
  template <class Page> bool addPage(SiteOutputs &outputs, const char *fileName, const char *title, const Page &page)
  {
    std::string bytes;
    return renderPage(loka::core::String(title), page, bytes) && outputs.add(fileName, bytes);
  }

  /** Cost: main calls once per site, staging pages, images and stylesheet in order, then publishing all staged files.
   */
  inline bool writeSite(const std::string &directory, const char *stylesheetPath, const SiteOutputs &outputs)
  {
    std::string css;
    if (directory.empty() || outputs.pages_.empty() || !readFile(stylesheetPath, 0, css))
      return false;
    StagedFiles files(outputs.pages_.size() + outputs.images_.size() + 1);
    return outputs.stage(outputs.pages_, files, directory) && outputs.stage(outputs.images_, files, directory)
           && files.stage(directory, "site.css", css) && files.publish();
  }
} // namespace document_site
#endif
