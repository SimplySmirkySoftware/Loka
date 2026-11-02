#ifndef LOKA_TOOLBOX_PICT_ADMISSION_HPP
#define LOKA_TOOLBOX_PICT_ADMISSION_HPP

#include <cstddef>

namespace loka
{
  namespace toolbox
  {
    /** Completed classification of the translation-safe mono raster subset. */
    class ToolboxPictAdmission
    {
    public:
      explicit ToolboxPictAdmission(bool admitted = false)
          : admitted_(admitted)
      {
      }
      bool admitsTranslation() const
      {
        return this->admitted_;
      }

    private:
      bool admitted_;
    };

    /** Scans one installed picture range (including picSize/frame), once per
        payload construction: O(bytes), no allocation. picSize is not an extent.
        Trailing bytes after logical OpEndPic are tolerated, as by PictParser. */
    ToolboxPictAdmission ScanToolboxPictAdmission(const unsigned char *bytes, std::size_t size);
  } // namespace toolbox
} // namespace loka

#endif
