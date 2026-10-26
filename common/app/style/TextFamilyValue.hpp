#ifndef LOKA_APP_STYLE_TEXT_FAMILY_VALUE_HPP
#define LOKA_APP_STYLE_TEXT_FAMILY_VALUE_HPP

#include "app/style/StyleVocab.hpp"

namespace loka
{
  namespace app
  {
    /** Optional font family in one byte. Invalid input is refused as UNSET before
        narrowing, including in release builds. UNSET is transparent in merges. */
    class TextFamilyValue
    {
    public:
      explicit TextFamilyValue(int value = TEXT_FAMILY_UNSET)
          : value_(static_cast<unsigned char>(
                value >= TEXT_FAMILY_UNSET && value < TEXT_FAMILY_COUNT ? value : TEXT_FAMILY_UNSET))
      {
      }

      TextFamily family() const
      {
        return static_cast<TextFamily>(this->value_);
      }

      bool isSet() const
      {
        return this->value_ != TEXT_FAMILY_UNSET;
      }

      TextFamilyValue operator+(const TextFamilyValue &right) const
      {
        return right.isSet() ? right : *this;
      }

      bool operator==(const TextFamilyValue &other) const
      {
        return this->value_ == other.value_;
      }

      bool operator<(const TextFamilyValue &other) const
      {
        return this->value_ < other.value_;
      }

    private:
      unsigned char value_;
    };
  } // namespace app
} // namespace loka

#endif // LOKA_APP_STYLE_TEXT_FAMILY_VALUE_HPP
