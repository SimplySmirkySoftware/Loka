#ifndef LOKA_APP_STYLE_STYLE_HPP
#define LOKA_APP_STYLE_STYLE_HPP

#include "app/style/TextSizeValue.hpp"
#include "app/style/TextFamilyValue.hpp"
#include "app/style/TextToggleValue.hpp"
#include "app/style/StyleVocab.hpp"

namespace loka
{
  namespace app
  {
    /** Character-level text style. Unset fields are transparent during merge. */
    struct TextStyle
    {
      explicit TextStyle(TextSizeValue size = TextSizeValue())
          : size_(size),
            weight_(TEXT_WEIGHT_NORMAL),
            color_(TEXT_COLOR_UNSET),
            italic_(),
            hasWeight_(false)
      {
      }

      TextStyle &weight(TextWeight value)
      {
        this->weight_ = value;
        this->hasWeight_ = true;
        return *this;
      }

      /** UNSET restores the component's native default and is transparent in merges. */
      TextStyle &color(TextColor value)
      {
        this->color_ = value;
        return *this;
      }

      TextStyle &family(TextFamily value)
      {
        this->family_ = TextFamilyValue(value);
        return *this;
      }

      TextFamily family() const
      {
        return this->family_.family();
      }

      /** Whether any font axis is declared, independently of color and block style. */
      bool declaresFont() const
      {
        return this->size_.isSet() || this->hasWeight_ || this->italic_.isSet() || this->family_.isSet();
      }

      TextStyle &italic(bool value = true)
      {
        this->italic_ = TextToggleValue(value ? TEXT_TOGGLE_ON : TEXT_TOGGLE_OFF);
        return *this;
      }

      /** Returns presence and writes on only when italic is declared. */
      bool queryItalic(bool &on) const
      {
        return this->italic_.query(on);
      }

      TextStyle operator+(const TextStyle &right) const
      {
        TextStyle result(*this);
        if (right.size_.isSet())
          result.size_ = right.size_;
        if (right.hasWeight_)
        {
          result.weight_ = right.weight_;
          result.hasWeight_ = true;
        }
        result.italic_ = this->italic_ + right.italic_;
        if (right.color_ != TEXT_COLOR_UNSET)
          result.color_ = right.color_;
        result.family_ = this->family_ + right.family_;
        return result;
      }

      bool operator==(const TextStyle &other) const
      {
        return this->size_ == other.size_ && this->weight_ == other.weight_ && this->italic_ == other.italic_
               && this->hasWeight_ == other.hasWeight_ && this->color_ == other.color_
               && this->family_ == other.family_;
      }

      bool operator!=(const TextStyle &other) const
      {
        return !(*this == other);
      }

      bool operator<(const TextStyle &other) const
      {
        // Preserve the numeric-first style order, including raw zero/negative points.
        int points = 0;
        int otherPoints = 0;
        this->size_.queryPoints(points);
        other.size_.queryPoints(otherPoints);
        if (points != otherPoints)
          return points < otherPoints;
        if (this->weight_ != other.weight_)
          return this->weight_ < other.weight_;
        if (this->italic_.isOn() != other.italic_.isOn())
          return this->italic_.isOn() < other.italic_.isOn();
        if (!(this->size_ == other.size_))
          return this->size_ < other.size_;
        if (this->hasWeight_ != other.hasWeight_)
          return this->hasWeight_ < other.hasWeight_;
        if (this->italic_.isSet() != other.italic_.isSet())
          return this->italic_.isSet() < other.italic_.isSet();
        if (this->color_ != other.color_)
          return this->color_ < other.color_;
        return this->family_ < other.family_;
      }

      TextSizeValue size_;
      TextWeight weight_;
      /** Color carries its own absence in UNSET. Weight keeps a hasWeight_ flag
          because its value domain has no distinct unset member. */
      TextColor color_;
      TextToggleValue italic_;
      bool hasWeight_;
      TextFamilyValue family_;
    };

    /** Paragraph-level text style. Unset fields are transparent during merge. */
    struct BlockStyle
    {
      BlockStyle()
          : wrap_(TEXT_WRAP_NONE),
            truncation_(TEXT_TRUNCATION_NONE),
            align_(TEXT_ALIGN_LEFT),
            hasWrap_(false),
            hasTruncation_(false),
            hasAlign_(false)
      {
      }

      BlockStyle &wrap(TextWrap value)
      {
        this->wrap_ = value;
        this->hasWrap_ = true;
        return *this;
      }

      BlockStyle &truncation(TextTruncation value)
      {
        this->truncation_ = value;
        this->hasTruncation_ = true;
        return *this;
      }

      BlockStyle &align(TextAlign value)
      {
        this->align_ = value;
        this->hasAlign_ = true;
        return *this;
      }

      BlockStyle operator+(const BlockStyle &right) const
      {
        BlockStyle result(*this);
        if (right.hasWrap_)
        {
          result.wrap_ = right.wrap_;
          result.hasWrap_ = true;
        }
        if (right.hasTruncation_)
        {
          result.truncation_ = right.truncation_;
          result.hasTruncation_ = true;
        }
        if (right.hasAlign_)
          result.align(right.align_);
        return result;
      }

      bool operator==(const BlockStyle &other) const
      {
        return this->wrap_ == other.wrap_
               && this->truncation_ == other.truncation_
               && this->align_ == other.align_
               && this->hasWrap_ == other.hasWrap_
               && this->hasTruncation_ == other.hasTruncation_
               && this->hasAlign_ == other.hasAlign_;
      }

      bool operator!=(const BlockStyle &other) const
      {
        return !(*this == other);
      }

      bool operator<(const BlockStyle &other) const
      {
        if (this->wrap_ != other.wrap_)
          return this->wrap_ < other.wrap_;
        if (this->truncation_ != other.truncation_)
          return this->truncation_ < other.truncation_;
        if (this->align_ != other.align_)
          return this->align_ < other.align_;
        if (this->hasWrap_ != other.hasWrap_)
          return this->hasWrap_ < other.hasWrap_;
        if (this->hasTruncation_ != other.hasTruncation_)
          return this->hasTruncation_ < other.hasTruncation_;
        return this->hasAlign_ < other.hasAlign_;
      }

      TextWrap wrap_;
      TextTruncation truncation_;
      TextAlign align_;
      bool hasWrap_;
      bool hasTruncation_;
      bool hasAlign_;
    };
  } // namespace app
} // namespace loka

#define LOKA_STYLE_VOCAB_DEFINE_SIZES
#include "app/style/StyleVocab.hpp"
#undef LOKA_STYLE_VOCAB_DEFINE_SIZES

namespace loka
{
  namespace app
  {
    namespace detail
    {
      /** Nearest vocabulary point size, ties down, shared by numeric rail projections. */
      int SnapTextSizePoints(int logicalUnits);
    } // namespace detail

    TextStyle SizeOf(int logicalUnits);

    /* Named styles are defined per translation unit (internal linkage) so that a
       namespace-scope style in application code, such as
       `const TextStyle Warning = Bold + Italic;`, is initialised after them:
       within one translation unit dynamic initialisation runs in declaration
       order, and this header precedes any consumer declaration. An extern
       object defined in Style.cpp would give no such guarantee across
       translation units. */
    /** Named block alignments. They live in their own namespace so the call
        site says what is aligned (`Text("x") + Title + block::Centered`; Row
        and Column own a different, child-box alignment) without a global
        `Centered`. LEFT is the default and has no name. Defined per
        translation unit like Bold/Italic, so a namespace-scope style in
        application code may be initialized from them safely. */
    namespace block
    {
      static const BlockStyle Centered = BlockStyle().align(TEXT_ALIGN_CENTER);
      static const BlockStyle RightAligned = BlockStyle().align(TEXT_ALIGN_RIGHT);
    } // namespace block

    static const TextStyle Bold = TextStyle().weight(TEXT_WEIGHT_BOLD);
    static const TextStyle Italic = TextStyle().italic();
    static const TextStyle Monospace = TextStyle().family(TEXT_FAMILY_MONOSPACE);
    static const TextStyle Body = FontSize<12>();
    static const TextStyle Caption = FontSize<9>();
    static const TextStyle Heading = FontSize<12>() + Bold;
    static const TextStyle Title = FontSize<18>() + Bold;
  } // namespace app
} // namespace loka

#endif // LOKA_APP_STYLE_STYLE_HPP
