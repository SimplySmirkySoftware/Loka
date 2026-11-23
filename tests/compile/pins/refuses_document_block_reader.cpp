#include "app/doc/BlockReader.hpp"
using namespace loka::app::doc;
struct Reader : BlockReader
{
  using BlockReader::read;
  bool read(const ImageProps &)
  {
    return true;
  }
  bool read(const ItemProps &)
  {
    return true;
  }
  bool read(const NumberedItemProps &)
  {
    return true;
  }
  bool enter(const ListProps &)
  {
    return true;
  }
  bool enter(const NumberedListProps &)
  {
    return true;
  }
  bool leave(const ListProps &)
  {
    return true;
  }
  bool leave(const NumberedListProps &)
  {
    return true;
  }
  bool enter(const QuoteProps &)
  {
    return true;
  }
  bool leave(const QuoteProps &)
  {
    return true;
  }
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
