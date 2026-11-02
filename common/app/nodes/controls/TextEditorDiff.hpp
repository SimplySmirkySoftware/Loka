#ifndef LOKA_APP_TEXT_EDITOR_DIFF_HPP
#define LOKA_APP_TEXT_EDITOR_DIFF_HPP
#include <string>
#include <cstring>

namespace loka
{
  namespace app
  {
    /** Completed differing range in two CR-separated logical documents.
        Empty text is one empty line. No native window participates. */
    class TextEditorLineDiff
    {
    public:
      TextEditorLineDiff(int first, int before, int after)
          : first_(first),
            before_(before),
            after_(after)
      {
      }
      int first() const
      {
        return this->first_;
      }
      int before() const
      {
        return this->before_;
      }
      int after() const
      {
        return this->after_;
      }

    private:
      int first_, before_, after_;
    };

    /** Slice a logical line from the same snapshot used for range detection. */
    inline std::string TextEditorLogicalLine(const std::string &text, int index)
    {
      std::size_t start = 0;
      for (int i = 0; i < index; ++i)
      {
        const std::size_t end = text.find('\r', start);
        if (end == std::string::npos)
          return std::string();
        start = end + 1;
      }
      const std::size_t end = text.find('\r', start);
      return text.substr(start, end == std::string::npos ? end : end - start);
    }

    namespace detail
    {
      inline std::size_t TextEditorLineEnd(const char *bytes, std::size_t length, std::size_t start)
      {
        if (start >= length)
          return length;
        const char *found = static_cast<const char *>(std::memchr(bytes + start, '\r', length - start));
        return found ? static_cast<std::size_t>(found - bytes) : length;
      }

      inline std::size_t TextEditorLineStart(const char *bytes, std::size_t end)
      {
        while (end && bytes[end - 1] != '\r')
          --end;
        return end;
      }

      inline bool TextEditorBytesEqual(const char *before,
                                       std::size_t beforeStart,
                                       std::size_t beforeLength,
                                       const char *after,
                                       std::size_t afterStart,
                                       std::size_t afterLength)
      {
        return beforeLength == afterLength
               && (!beforeLength || std::memcmp(before + beforeStart, after + afterStart, beforeLength) == 0);
      }
    } // namespace detail

    /** The optional pre-edit caret (logical line and byte column) breaks ties
        only for an edge split/join that reproduces the complete after snapshot.
        An absent or unrelated hint leaves text-only detection unchanged.
        Buffers need not be terminated; a null buffer is allowed only at zero length. */
    inline TextEditorLineDiff DiffTextEditorLines(const char *before,
                                                  std::size_t beforeLength,
                                                  const char *after,
                                                  std::size_t afterLength,
                                                  int caretLine = -1,
                                                  int caretColumn = -1)
    {
      int oldCount = 1, newCount = 1;
      for (std::size_t i = 0; i < beforeLength; ++i)
        if (before[i] == '\r')
          ++oldCount;
      for (std::size_t i = 0; i < afterLength; ++i)
        if (after[i] == '\r')
          ++newCount;
      int prefix = 0;
      std::size_t oldStart = 0, newStart = 0;
      while (prefix < oldCount && prefix < newCount)
      {
        std::size_t oldEnd = detail::TextEditorLineEnd(before, beforeLength, oldStart),
                    newEnd = detail::TextEditorLineEnd(after, afterLength, newStart);
        if (!detail::TextEditorBytesEqual(before, oldStart, oldEnd - oldStart, after, newStart, newEnd - newStart))
          break;
        ++prefix;
        oldStart = oldEnd + 1;
        newStart = newEnd + 1;
      }
      int suffix = 0;
      std::size_t oldEnd = beforeLength, newEnd = afterLength;
      while (suffix < oldCount - prefix && suffix < newCount - prefix)
      {
        oldStart = detail::TextEditorLineStart(before, oldEnd);
        newStart = detail::TextEditorLineStart(after, newEnd);
        if (!detail::TextEditorBytesEqual(before, oldStart, oldEnd - oldStart, after, newStart, newEnd - newStart))
          break;
        ++suffix;
        oldEnd = oldStart ? oldStart - 1 : 0;
        newEnd = newStart ? newStart - 1 : 0;
      }
      if (oldCount == prefix && newCount == prefix)
        return TextEditorLineDiff(prefix, 0, 0);
      // A split/join at an empty edge still needs the retained line identity.
      if (oldCount == prefix + suffix || newCount == prefix + suffix)
      {
        if (caretLine >= 0 && caretLine < oldCount && caretColumn >= 0)
        {
          std::size_t start = 0;
          for (int line = 0; line < caretLine; ++line)
            start = detail::TextEditorLineEnd(before, beforeLength, start) + 1;
          const std::size_t end = detail::TextEditorLineEnd(before, beforeLength, start);
          const std::size_t length = end - start;
          if (static_cast<std::size_t>(caretColumn) <= length)
          {
            const std::size_t position = start + static_cast<std::size_t>(caretColumn);
            if (newCount == oldCount + 1 && afterLength == beforeLength + 1 && after[position] == '\r'
                && detail::TextEditorBytesEqual(before, 0, position, after, 0, position)
                && detail::TextEditorBytesEqual(
                    before, position, beforeLength - position, after, position + 1, afterLength - position - 1))
              return TextEditorLineDiff(caretLine, 1, 2);
            // Backspace joins the caret line into its predecessor; forward
            // delete at the other edge joins the following line into this one.
            const std::size_t seam = caretColumn == 0 && caretLine > 0 ? start - 1 : position;
            if (oldCount == newCount + 1 && beforeLength == afterLength + 1 && seam < beforeLength
                && before[seam] == '\r' && detail::TextEditorBytesEqual(before, 0, seam, after, 0, seam)
                && detail::TextEditorBytesEqual(
                    before, seam + 1, beforeLength - seam - 1, after, seam, afterLength - seam))
              return TextEditorLineDiff(seam < start ? caretLine - 1 : caretLine, 2, 1);
          }
        }
        if (prefix)
          --prefix;
        else if (suffix)
          --suffix;
      }
      return TextEditorLineDiff(prefix, oldCount - prefix - suffix, newCount - prefix - suffix);
    }
    /** String convenience overload of the borrowed-buffer diff. */
    inline TextEditorLineDiff
    DiffTextEditorLines(const std::string &before, const std::string &after, int caretLine = -1, int caretColumn = -1)
    {
      return DiffTextEditorLines(before.data(), before.size(), after.data(), after.size(), caretLine, caretColumn);
    }
  } // namespace app
} // namespace loka
#endif
