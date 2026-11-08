#ifndef LOKA_WEB_TEXT_DESCRIPTOR_HPP
#define LOKA_WEB_TEXT_DESCRIPTOR_HPP
#include "WebStyleWire.hpp"
#include "app/style/Style.hpp"
#if __cplusplus >= 201103L
static_assert(WEB_TEXT_FIELD_COUNT == 9, "Web text wire v2 has nine words");
#endif
/** Immutable transport value borrowed only during the boundary call.
 * Cost: Text render once per visit, reads its own props and writes nine stack words. */
class WebTextDescriptor
{
public:
  explicit WebTextDescriptor(const loka::app::TextStyle &style)
  {
    using namespace loka::app;
    int points = 0;
    TextSizeRole role = TEXT_SIZE_ROLE_SMALL_LABEL;
    const bool hasPoints = style.size_.queryPoints(points);
    const bool hasRole = style.size_.queryRole(role);
    TextWeight weight = TEXT_WEIGHT_NORMAL;
    bool italic = false, underline = false;
    this->words_[WEB_TEXT_FIELD_VERSION] = kWebTextDescriptorVersion;
    this->words_[WEB_TEXT_FIELD_WORD_COUNT] = WEB_TEXT_FIELD_COUNT;
    this->words_[WEB_TEXT_FIELD_SIZE_KIND] = hasPoints ? 1 : hasRole ? 2 : 0;
    this->words_[WEB_TEXT_FIELD_SIZE_PAYLOAD] = hasPoints ? points : hasRole ? static_cast<int>(role) : 0;
    this->words_[WEB_TEXT_FIELD_WEIGHT] = style.queryWeight(weight) ? static_cast<int>(weight) : -1;
    this->words_[WEB_TEXT_FIELD_ITALIC] = style.queryItalic(italic) ? static_cast<int>(italic) : -1;
    this->words_[WEB_TEXT_FIELD_FAMILY] = static_cast<int>(style.family());
    this->words_[WEB_TEXT_FIELD_UNDERLINE] = style.queryUnderline(underline) ? static_cast<int>(underline) : -1;
    this->words_[WEB_TEXT_FIELD_COLOR] = static_cast<int>(style.color_);
  }
  /** Cost: boundary caller once per render; borrows this value's nine words. */
  const int *data() const
  {
    return this->words_;
  }
  /** Cost: boundary caller once per render; constant, no rows. */
  static unsigned count()
  {
    return WEB_TEXT_FIELD_COUNT;
  }

private:
  int words_[WEB_TEXT_FIELD_COUNT];
  WebTextDescriptor &operator=(const WebTextDescriptor &);
};
#endif
