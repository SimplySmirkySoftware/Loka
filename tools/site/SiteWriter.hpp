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

  /** Cost: writeSite reads the supplied stylesheet once, before opening a destination. */
  inline bool readStylesheet(const char *path, std::string &bytes)
  {
    FILE *file = std::fopen(path, "rb");
    if (!file)
      return false;
    char chunk[8192];
    size_t count;
    while ((count = std::fread(chunk, 1, sizeof(chunk), file)) != 0)
      bytes.append(chunk, count);
    const bool read = !std::ferror(file);
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

  /** Ordered rendered pages awaiting publication. */
  class SiteOutputs
  {
    typedef std::list<std::pair<std::string, std::string> > Pages;
    Pages pages_;
    /** Cost: writeSite calls once per site, staging this collection's pages in insertion order. */
    bool stage(StagedFiles &files, const std::string &directory) const
    {
      for (Pages::const_iterator page = this->pages_.begin(); page != this->pages_.end(); ++page)
        if (!files.stage(directory, page->first.c_str(), page->second))
          return false;
      return true;
    }

    friend bool writeSite(const std::string &, const char *, const SiteOutputs &);

  public:
    /** Cost: addPage calls once per rendered page, scanning this site's names and copying the admitted bytes. */
    bool add(const std::string &name, const std::string &bytes)
    {
      if (name.empty() || name.find('/') != std::string::npos || name.find('\0') != std::string::npos
          || name == "site.css")
        return false;
      for (Pages::const_iterator page = this->pages_.begin(); page != this->pages_.end(); ++page)
        if (page->first == name)
          return false;
      this->pages_.push_back(std::make_pair(name, bytes));
      return true;
    }
  };

  /** Cost: main calls once per page, rendering its own boundary and offering the completed bytes once. */
  template <class Page> bool addPage(SiteOutputs &outputs, const char *fileName, const char *title, const Page &page)
  {
    std::string bytes;
    return renderPage(loka::core::String(title), page, bytes) && outputs.add(fileName, bytes);
  }

  /** Cost: main calls once per site, walking its admitted pages once, then publishing them and the stylesheet. */
  inline bool writeSite(const std::string &directory, const char *stylesheetPath, const SiteOutputs &outputs)
  {
    std::string css;
    if (directory.empty() || outputs.pages_.empty() || !readStylesheet(stylesheetPath, css))
      return false;
    StagedFiles files(outputs.pages_.size() + 1);
    return outputs.stage(files, directory) && files.stage(directory, "site.css", css) && files.publish();
  }
} // namespace document_site
#endif
