#ifndef LOKA_APP_TEXT_VIEW_VALUES_HPP
#define LOKA_APP_TEXT_VIEW_VALUES_HPP
#include "app/style/Style.hpp"
namespace loka
{
  namespace app
  {
    /** Attachment snapshot published only at document completion, never a transition event. */
    class TextViewAttachment
    {
    public:
      enum Value
      {
        VIEW_DETACHED,
        VIEW_ATTACHED,
        VIEW_REFUSED_OCCUPIED,
        VIEW_REFUSED_UNAVAILABLE,
        VIEW_REFUSED_UNSUPPORTED,
        VIEW_REFUSED_FOREIGN
      };
      explicit TextViewAttachment(Value value = VIEW_DETACHED)
          : value_(value)
      {
      }
      Value value() const
      {
        return this->value_;
      }
      bool operator==(const TextViewAttachment &other) const
      {
        return this->value_ == other.value_;
      }
      bool operator!=(const TextViewAttachment &other) const
      {
        return !(*this == other);
      }

    private:
      Value value_;
    };
    /** Completed presentation. Default style uses the system font and native colors.
        Resource handles belong to the rail; read-only governs user input, not document requests. */
    class TextViewPresentation
    {
    public:
      explicit TextViewPresentation(const TextStyle &style = TextStyle(),
                                    short margin = 0,
                                    bool readOnly = false,
                                    bool wrap = true)
          : style_(style),
            margin_(margin),
            readOnly_(readOnly),
            wrap_(wrap)
      {
      }
      const TextStyle &style() const
      {
        return this->style_;
      }
      short margin() const
      {
        return this->margin_;
      }
      bool readOnly() const
      {
        return this->readOnly_;
      }
      bool wrap() const
      {
        return this->wrap_;
      }
      bool operator==(const TextViewPresentation &other) const
      {
        return this->style_ == other.style_ && this->margin_ == other.margin_ && this->readOnly_ == other.readOnly_
               && this->wrap_ == other.wrap_;
      }
      bool operator!=(const TextViewPresentation &other) const
      {
        return !(*this == other);
      }
      bool operator<(const TextViewPresentation &other) const
      {
        if (this->style_ != other.style_)
          return this->style_ < other.style_;
        if (this->margin_ != other.margin_)
          return this->margin_ < other.margin_;
        if (this->readOnly_ != other.readOnly_)
          return this->readOnly_ < other.readOnly_;
        return this->wrap_ < other.wrap_;
      }

    private:
      TextStyle style_;
      short margin_;
      bool readOnly_, wrap_;
    };
  } // namespace app
} // namespace loka
#endif
