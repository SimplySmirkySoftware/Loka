#include "app/doc/BlockReader.hpp"
using namespace loka::app::doc;
struct Reader : BlockReader
{
  bool read(const HeadingProps &)
  {
    return true;
  }
  bool read(const ParagraphProps &)
  {
    return true;
  }
};
void instantiateReader()
{
  Reader reader;
  (void)reader;
}
