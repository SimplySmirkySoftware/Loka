#ifndef LOKA_APP_STYLE_TEXT_WEIGHT_VALUE_HPP
#define LOKA_APP_STYLE_TEXT_WEIGHT_VALUE_HPP

#include "app/style/StyleVocab.hpp"

namespace loka
{
  namespace app
  {
    /** Optional font weight in one byte. Unset is transparent in merges.
        Only TextWeight enumerators are valid input. An integer outside the enum's
        range is already invalid where the caller converts it (UBSan reports the
        load), so no refusal is promised here, unlike the int-taking toggle and
        family values. */
    class TextWeightValue
    {
    public:
      TextWeightValue()
          : value_(STORED_UNSET)
      {
      }

      explicit TextWeightValue(TextWeight value)
          : value_(STORED_UNSET)
      {
        switch (value)
        {
        case TEXT_WEIGHT_NORMAL:
          this->value_ = STORED_NORMAL;
          break;
        case TEXT_WEIGHT_BOLD:
          this->value_ = STORED_BOLD;
          break;
        }
      }

      bool isSet() const
      {
        return this->value_ != STORED_UNSET;
      }

      /** Returns presence and writes out only when declared. */
      bool query(TextWeight &out) const
      {
        if (!this->isSet())
          return false;
        out = this->orderWeight();
        return true;
      }

      /** Ordering view. Use query for projection. Unset reads as normal. */
      TextWeight orderWeight() const
      {
        return this->value_ == STORED_BOLD ? TEXT_WEIGHT_BOLD : TEXT_WEIGHT_NORMAL;
      }

      TextWeightValue operator+(const TextWeightValue &right) const
      {
        return right.isSet() ? right : *this;
      }

      bool operator==(const TextWeightValue &other) const
      {
        return this->value_ == other.value_;
      }

    private:
      enum StoredWeight
      {
        STORED_UNSET,
        STORED_NORMAL,
        STORED_BOLD
      };
      unsigned char value_;
    };
  } // namespace app
} // namespace loka

#endif // LOKA_APP_STYLE_TEXT_WEIGHT_VALUE_HPP
