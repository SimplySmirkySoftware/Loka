#ifndef LOKA_APP_STYLE_TEXT_TOGGLE_VALUE_HPP
#define LOKA_APP_STYLE_TEXT_TOGGLE_VALUE_HPP

namespace loka
{
  namespace app
  {
    /** Declaration of an optional on/off text axis. */
    enum TextToggle
    {
      TEXT_TOGGLE_UNSET,
      TEXT_TOGGLE_OFF,
      TEXT_TOGGLE_ON,
      TEXT_TOGGLE_COUNT
    };

    /** Optional on/off text axis in one byte. Invalid input is refused as UNSET
        before narrowing, including in release builds. UNSET is transparent in merges. */
    class TextToggleValue
    {
    public:
      explicit TextToggleValue(int value = TEXT_TOGGLE_UNSET)
          : value_(static_cast<unsigned char>(
                value >= TEXT_TOGGLE_UNSET && value < TEXT_TOGGLE_COUNT ? value : TEXT_TOGGLE_UNSET))
      {
      }

      bool isSet() const
      {
        return this->value_ != TEXT_TOGGLE_UNSET;
      }

      /** Returns presence and writes on only when declared. */
      bool query(bool &on) const
      {
        if (!this->isSet())
          return false;
        on = this->isOn();
        return true;
      }

      /** Ordering view: unset reads as off. Use query for projection. */
      bool isOn() const
      {
        return this->value_ == TEXT_TOGGLE_ON;
      }

      TextToggleValue operator+(const TextToggleValue &right) const
      {
        return right.isSet() ? right : *this;
      }

      bool operator==(const TextToggleValue &other) const
      {
        return this->value_ == other.value_;
      }

      bool operator<(const TextToggleValue &other) const
      {
        return this->value_ < other.value_;
      }

    private:
      unsigned char value_;
    };
  } // namespace app
} // namespace loka

#endif // LOKA_APP_STYLE_TEXT_TOGGLE_VALUE_HPP
