#ifndef LOKA_APP_DOC_MARKDOWN_HPP
#define LOKA_APP_DOC_MARKDOWN_HPP

#include "app/doc/detail/Projection.hpp"
#include <cstdio>
#include <cstring>

namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        /** Additional whitespace stripped by some CommonMark readers at unprotected block edges.
            Cost: O(1) per edge check or padding candidate. */
        inline bool ExtraStripSpace(unsigned c)
        {
          return c == '\v' || c == '\f' || (c >= 0x1c && c <= 0x1f) || c == 0x85 || c == 0x2028 || c == 0x2029;
        }

        /** Cost: one bounded scan of the byte interval being fenced. */
        inline std::string Backticks(const std::string &body, size_t first, size_t end, size_t minimum)
        {
          size_t length = minimum, current = 0;
          for (size_t i = first; i < end; ++i)
          {
            current = body[i] == '`' ? current + 1 : 0;
            if (current >= length)
              length = current + 1;
          }
          return std::string(length, '`');
        }

        /** Cost: once per code run, scans its validated bytes, then appends the fenced slice. */
        inline bool AppendInlineCode(const std::string &body, size_t first, size_t end, std::string &out)
        {
          bool pad = body[first] == '`' || body[end - 1] == '`';
          if (body[first] == ' ' && body[end - 1] == ' ')
          {
            bool onlySpaces = true;
            for (size_t i = first; i < end;)
            {
              unsigned scalar = 0;
              core::detail::DecodeUtf8Codepoint(body.data(), end, i, scalar);
              if (scalar != ' ')
                onlySpaces = false;
              if (!EdgeSpace(scalar) && !ExtraStripSpace(scalar))
                break;
              // Readers disagree about padding around mixed whitespace alone.
              if (i == end && !onlySpaces)
                return false;
            }
            pad = !onlySpaces;
          }
          const std::string fence = Backticks(body, first, end, 1);
          out += fence;
          if (pad)
            out += ' ';
          out.append(body, first, end - first);
          if (pad)
            out += ' ';
          out += fence;
          return true;
        }

        /** Forward cursor shared across styles, including code spans the text speller skips.
            Cost: at most one search and ink scan per physical line, constant storage. */
        class MarkdownLineEdges
        {
          size_t nextLine_;
          DocumentInk ink_;

        public:
          explicit MarkdownLineEdges(size_t first)
              : nextLine_(first),
                ink_()
          {
          }
          /** Cost: the speller queries in increasing byte order, amortized linear in all line bytes. */
          bool contains(const std::string &bytes, size_t offset, size_t end)
          {
            while (this->nextLine_ <= offset)
            {
              size_t lineEnd = bytes.find('\n', this->nextLine_);
              if (lineEnd > end)
                lineEnd = end;
              if (offset <= lineEnd)
                this->ink_ = FindDocumentInk(bytes, this->nextLine_, lineEnd);
              this->nextLine_ = lineEnd + 1;
            }
            return offset < this->ink_.first || offset >= this->ink_.end;
          }
        };

        /** A block-local projection whose byte ranges are rewritten to their Markdown spellings. */
        struct MarkdownInline : InlineProjection
        {
          /** Cost: one spelling pass into a byte buffer. Ranges then address emitted payloads
              so flanking checks use the actual adjacent bytes. Line lookahead is linear in total. */
          bool spell()
          {
            if (this->runs.empty())
              return true;
            const size_t first = this->runs.front().first, end = this->runs.back().end;
            MarkdownLineEdges edges(first);
            std::string spelled;
            spelled.reserve(end - first);
            for (size_t r = 0; r < this->runs.size(); ++r)
            {
              InlineRun &run = this->runs[r];
              const size_t start = spelled.size();
              if (run.style.code)
              {
                if (!AppendInlineCode(this->bytes, run.first, run.end, spelled))
                  return false;
              }
              else
              {
                bool lineStart = !r && !run.style.link && !run.style.marks;
                bool digits = false;
                for (size_t i = run.first; i < run.end;)
                {
                  size_t pos = i;
                  unsigned v = 0;
                  core::detail::DecodeUtf8Codepoint(this->bytes.data(), run.end, i, v);
                  if ((pos == first || i == end) && !run.style.link && !run.style.marks && ExtraStripSpace(v))
                    return false;
                  if (v == '\n')
                    spelled += "\\\n";
                  else if (v == 0xfeff && pos == first)
                    spelled += "&#xFEFF;"; // Bare, a reader takes the fragment's first bytes as a signature.
                  else if (EdgeSpace(v) && edges.contains(this->bytes, pos, end))
                  {
                    char ref[16];
                    ::snprintf(ref, sizeof(ref), v < 128 ? "&#%u;" : "&#x%X;", v);
                    spelled += ref;
                  }
                  else
                  {
                    bool beforeLink = i == run.end && r + 1 < this->runs.size() && this->runs[r + 1].style.link
                                      && !run.style.link && !run.style.marks;
                    if (v < 128
                        && (std::strchr("\\`*_[]<&#~", v) || (lineStart && std::strchr(">+-=", v))
                            || (digits && (v == '.' || v == ')')) || (v == '!' && beforeLink)))
                      spelled += '\\';
                    spelled.append(this->bytes, pos, i - pos);
                  }
                  digits = (lineStart || digits) && v >= '0' && v <= '9';
                  lineStart = v == '\n';
                }
              }
              run.first = start;
              run.end = spelled.size();
            }
            this->bytes.swap(spelled);
            return true;
          }
        };

        /** Cost: one locale-independent byte classification per flanking check. */
        inline bool AsciiAlphanumeric(char c)
        {
          return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        }

        /** Match only edges whose emitted ASCII bytes prove one-way flanking. Cost: O(1) per edge. */
        inline bool StarEdge(char outside, char inside)
        {
          if (!outside || outside == ' ' || outside == '\t' || outside == '\n')
            return inside >= '!' && inside <= '~';
          return AsciiAlphanumeric(inside) && outside >= '!' && outside <= '~' && outside != '*' && outside != '_'
                 && !AsciiAlphanumeric(outside);
        }

        /** Cost: once per maximal link, escaping only its own destination bytes. */
        inline void CloseMarkdownLink(std::string &out, const char *target)
        {
          out += "](<";
          for (size_t i = 0; target[i]; ++i)
          {
            if (std::strchr("<>\\&", target[i]))
              out += '\\';
            out += target[i];
          }
          out += ">)";
        }

        /** Cost: once per inline block, scans its collected projection and writes its own output bytes. */
        inline bool WriteInline(const AttributedString &content, bool heading, std::string &out)
        {
          MarkdownInline input;
          if (!input.collect(content, heading))
            return false;
          input.normalizeEdges();
          if (!input.spell())
            return false;
          const std::vector<InlineRun> &runs = input.runs;
          for (size_t i = 0; i < runs.size(); ++i)
          {
            const InlineRun &run = runs[i];
            const InlineRun *prev = i ? &runs[i - 1] : 0;
            const InlineRun *next = i + 1 < runs.size() ? &runs[i + 1] : 0;
            bool open = run.style.link && (!prev || prev->style.link != run.style.link);
            bool close = run.style.link && (!next || next->style.link != run.style.link);
            if (open)
              out += '[';
            const unsigned marks = run.style.marks;
            if (!marks)
              out.append(input.bytes, run.first, run.end - run.first);
            else
            {
              char left = open || out.empty() ? 0 : out[out.size() - 1];
              char right = 0;
              if (!close && next)
              {
                if (next->style.link != run.style.link)
                  right = '[';
                else if (next->style.marks)
                  right = '<'; // The touching successor will use HTML.
                else
                  right = input.bytes[next->first];
              }
              bool touching = !open && prev && prev->style.link == run.style.link && prev->style.marks;
              bool literal = next && !close && !next->style.marks && !next->style.code
                             && next->style.link == run.style.link
                             && (input.bytes.compare(next->first, 2, "\\*") == 0
                                 || input.bytes.compare(next->first, 2, "\\_") == 0);
              bool stars = !touching && !literal && StarEdge(left, input.bytes[run.first])
                           && StarEdge(right, input.bytes[run.end - 1]);
              if (stars)
              {
                const std::string marker(marks, '*');
                out += marker;
                out.append(input.bytes, run.first, run.end - run.first);
                out += marker;
              }
              else
              {
                if (marks & 2)
                  out += "<strong>";
                if (marks & 1)
                  out += "<em>";
                out.append(input.bytes, run.first, run.end - run.first);
                if (marks & 1)
                  out += "</em>";
                if (marks & 2)
                  out += "</strong>";
              }
            }
            if (close)
              CloseMarkdownLink(out, input.targets.c_str() + run.style.link);
          }
          return true;
        }

        /** Owns the fragment until the complete borrow succeeds. Cost: one instance per explicit exit. */
        class MarkdownReader : public BlockReader
        {
        public:
          std::string fragment;
          /** Cost: each accepted nonempty block appends its spelling and separator once. */
          void block(const std::string &value)
          {
            if (!this->fragment.empty())
              this->fragment += '\n';
            this->fragment += value;
            this->fragment += '\n';
          }
          /** Cost: one inline projection per visited heading, independent of its visual defaults. */
          virtual bool read(const HeadingProps &p)
          {
            std::string body;
            if (!WriteInline(p.content, true, body))
              return false;
            this->block(std::string(p.level(), '#') + (body.empty() ? "" : " ") + body);
            return true;
          }
          /** Cost: one inline projection per visited paragraph. */
          virtual bool read(const ParagraphProps &p)
          {
            std::string body;
            if (!WriteInline(p.content, false, body))
              return false;
            if (!body.empty())
              this->block(body);
            return true;
          }
          /** Cost: one collection per visited code block, then linear newline and fence scans. */
          virtual bool read(const CodeBlockProps &p)
          {
            std::string body;
            if (!CollectCodeBlock(p.text, body))
              return false;
            std::string fence = Backticks(body, 0, body.size(), 3);
            this->block(fence + "\n" + body + fence);
            return true;
          }
        };

      } // namespace detail

      /** Append an independent fragment for the subtree including root. The caller supplies any prefix separator.
          On refusal out retains its original bytes. Output allocation follows the process OOM path.
          The owner lends a stable mounted tree synchronously on the Main Thread. The walk never mutates it.
          Detached roots contribute nothing. The initial attached lifecycle fact cannot authenticate a mount.
          Cost: once per explicit export, proportional to visited nodes, segments, collection and output bytes.
          Recursion follows tree depth. Scratch is O(style runs) records + collected bytes + staged output, alongside
         out and its growth. */
      inline bool WriteMarkdown(scene::Node *root, std::string &out)
      {
        detail::MarkdownReader reader;
        if (!root || !detail::WalkDocumentBlocks(root, reader))
          return false;
        out += reader.fragment;
        return true;
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
