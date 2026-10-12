#ifndef LOKA_APP_STYLE_TEXT_SIZE_VALUE_HPP
#define LOKA_APP_STYLE_TEXT_SIZE_VALUE_HPP

#include <cassert>

namespace loka
{
  namespace app
  {
    /** Optional text size. Absence is transparent during style merge.
        Points are stored unchanged; each rail keeps its existing snapping policy.
        Four bytes, the size of the int and flag it replaced: TextStyle sits in
        every Text definition, and the definitions are pinned to their pool class. */
    class TextSizeValue
    {
    public:
      TextSizeValue()
          : points_(0),
            kind_(static_cast<unsigned char>(NONE))
      {
      }

      /** Points must fit in a short; font sizes are far inside that range. */
      static TextSizeValue points(int value)
      {
        assert(value >= -32768 && value <= 32767 && "text size points must fit in a short");
        return TextSizeValue(static_cast<short>(value));
      }

      /** Returns false without changing out when no points were declared. */
      bool queryPoints(int &out) const
      {
        switch (static_cast<Kind>(this->kind_))
        {
        case NONE:
          return false;
        case POINTS:
          out = this->points_;
          return true;
        }
        return false;
      }

      bool isSet() const
      {
        return this->kind_ != NONE;
      }

      bool operator==(const TextSizeValue &other) const
      {
        return this->kind_ == other.kind_ && this->points_ == other.points_;
      }

      bool operator<(const TextSizeValue &other) const
      {
        if (this->kind_ != other.kind_)
          return this->kind_ < other.kind_;
        return this->points_ < other.points_;
      }

    private:
      enum Kind
      {
        NONE,
        POINTS
      };

      explicit TextSizeValue(short value)
          : points_(value),
            kind_(static_cast<unsigned char>(POINTS))
      {
      }

      short points_;
      unsigned char kind_;
    };
  } // namespace app
} // namespace loka

#endif // LOKA_APP_STYLE_TEXT_SIZE_VALUE_HPP
