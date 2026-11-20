#ifndef DOCUMENT_SITE_WRITER_HPP
#define DOCUMENT_SITE_WRITER_HPP

#include "app/doc/Render.hpp"
#include <cstdio>
#include <cstdlib>
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

  /** Cost: main calls once, holding two rendered pages and the supplied stylesheet until publication. */
  template <class First, class Second>
  bool writeSite(const std::string &directory,
                 const char *stylesheet,
                 const char *firstTitle,
                 const First &first,
                 const char *secondTitle,
                 const Second &second)
  {
    std::string index, trail, css;
    if (directory.empty() || !renderPage(loka::core::String(firstTitle), first, index))
      return false;
    if (!renderPage(loka::core::String(secondTitle), second, trail) || !readStylesheet(stylesheet, css))
      return false;
    SiteFile home(directory, "index.html"), notes(directory, "trail.html"), style(directory, "site.css");
    return home.stage(index) && notes.stage(trail) && style.stage(css) && home.publish() && notes.publish()
           && style.publish();
  }
} // namespace document_site
#endif
