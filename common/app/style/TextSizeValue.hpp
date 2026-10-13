#ifndef LOKA_APP_STYLE_TEXT_SIZE_VALUE_HPP
#define LOKA_APP_STYLE_TEXT_SIZE_VALUE_HPP

#include <cassert>
#include "app/style/StyleVocab.hpp"

namespace loka
{
  namespace app
  {
    /** Optional points or semantic font role. Absence is transparent during style merge.
        Points are stored unchanged; each rail keeps its existing snapping policy.
        Four bytes, the size of the int and flag it replaced: TextStyle sits in
        every Text definition, and the definitions are pinned to their pool class. */
    class TextSizeValue
    {
    public:
      TextSizeValue()
          : payload_(0),
            kind_(static_cast<unsigned char>(NONE))
      {
      }

      /** Points must fit in a short; font sizes are far inside that range. */
      static TextSizeValue points(int value)
      {
        assert(value >= -32768 && value <= 32767 && "text size points must fit in a short");
        return TextSizeValue(static_cast<short>(value), POINTS);
      }

      /** Returns false without changing out when no points were declared. */
      bool queryPoints(int &out) const
      {
        switch (static_cast<Kind>(this->kind_))
        {
        case ROLE:
        case NONE:
          return false;
        case POINTS:
          out = this->payload_;
          return true;
        }
        return false;
      }

      /** A semantic size, resolved by the receiving rail. */
      static TextSizeValue role(TextSizeRole value)
      {
        assert(value >= 0 && value < TEXT_SIZE_ROLE_COUNT);
        return TextSizeValue(static_cast<short>(value), ROLE);
      }

      /** Returns false without changing out when no role was declared. */
      bool queryRole(TextSizeRole &out) const
      {
        if (this->kind_ != ROLE)
          return false;
        out = static_cast<TextSizeRole>(this->payload_);
        return true;
      }

      bool isSet() const
      {
        return this->kind_ != NONE;
      }

      bool operator==(const TextSizeValue &other) const
      {
        return this->kind_ == other.kind_ && this->payload_ == other.payload_;
      }

      bool operator<(const TextSizeValue &other) const
      {
        if (this->kind_ != other.kind_)
          return this->kind_ < other.kind_;
        return this->payload_ < other.payload_;
      }

    private:
      enum Kind
      {
        NONE,
        POINTS,
        ROLE
      };

      TextSizeValue(short value, Kind kind)
          : payload_(value),
            kind_(static_cast<unsigned char>(kind))
      {
      }

      short payload_;
      unsigned char kind_;
    };
  } // namespace app
} // namespace loka

#endif // LOKA_APP_STYLE_TEXT_SIZE_VALUE_HPP
