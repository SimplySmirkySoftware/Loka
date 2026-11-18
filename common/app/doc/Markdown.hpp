#ifndef LOKA_APP_DOC_MARKDOWN_HPP
#define LOKA_APP_DOC_MARKDOWN_HPP

#include "app/doc/Heading.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/doc/CodeBlock.hpp"
#include "core/Utf8Decode.hpp"
#include "platform/StringUTF8.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        using std::size_t;

        /** Cost: one collection and strict scan of the current body or target per export. */
        inline bool CollectMarkdownText(const core::String &source, std::string &bytes)
        {
          const size_t first = bytes.size();
          if (!platform::CollectUtf8(source, bytes))
            return false;
          unsigned scalar;
          for (size_t i = first; i < bytes.size();)
            if (!core::detail::DecodeUtf8Codepoint(bytes.data(), bytes.size(), i, scalar) || !scalar)
              return false;
          return true;
        }

        /** Cost: normalization tests one scalar at a time against the fixed document whitespace set. */
        inline bool EdgeSpace(unsigned c)
        {
          return c == ' ' || c == '\t' || c == '\n' || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a)
                 || c == 0x202f || c == 0x205f || c == 0x3000;
        }

        /** Additional whitespace stripped by some CommonMark readers at unprotected block edges.
            Cost: O(1) per edge check or padding candidate. */
        inline bool ExtraStripSpace(unsigned c)
        {
          return c == '\v' || c == '\f' || (c >= 0x1c && c <= 0x1f) || c == 0x85 || c == 0x2028 || c == 0x2029;
        }

        /** One normalized style. Equal consecutive targets share a block-local index. */
        struct MarkdownStyle
        {
          size_t link;
          unsigned marks;
          bool code;
          /** Cost: one constant-time comparison per interval contribution, without re-reading targets. */
          bool operator==(const MarkdownStyle &rhs) const
          {
            return this->link == rhs.link && this->marks == rhs.marks && this->code == rhs.code;
          }
        };
        /** Half-open byte interval in the current buffer. No per-character storage. */
        struct MarkdownRun
        {
          MarkdownStyle style;
          size_t first;
          size_t end;
        };

        /** Cost: O(1) amortized per interval contributed during collection, folding or edge splitting. */
        inline void AppendRun(std::vector<MarkdownRun> &runs, MarkdownStyle style, size_t first, size_t end)
        {
          if (first == end)
            return;
          if (runs.empty() || !(runs.back().style == style))
          {
            MarkdownRun run = {style, first, end};
            runs.push_back(run);
          }
          else
            runs.back().end = end;
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

        /** Half-open interval containing the ink in a validated run or physical line. */
        struct MarkdownInk
        {
          size_t first, end;
        };

        /** Cost: one scalar scan of the specified range. All-whitespace input returns an empty suffix. */
        inline MarkdownInk FindMarkdownInk(const std::string &bytes, size_t first, size_t end)
        {
          MarkdownInk ink = {end, end};
          for (size_t i = first; i < end;)
          {
            const size_t start = i;
            unsigned scalar = 0;
            core::detail::DecodeUtf8Codepoint(bytes.data(), end, i, scalar);
            if (!EdgeSpace(scalar))
            {
              if (ink.first == end)
                ink.first = start;
              ink.end = i;
            }
          }
          return ink;
        }

        /** Forward cursor shared across styles, including code spans the text speller skips.
            Cost: at most one search and ink scan per physical line, constant storage. */
        class MarkdownLineEdges
        {
          size_t nextLine_;
          MarkdownInk ink_;

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
                this->ink_ = FindMarkdownInk(bytes, this->nextLine_, lineEnd);
              this->nextLine_ = lineEnd + 1;
            }
            return offset < this->ink_.first || offset >= this->ink_.end;
          }
        };

        /** Block-local scratch = O(runs) records + collected bytes + staged output.
            Each MarkdownRun costs sizeof(MarkdownStyle) + 2*sizeof(size_t), including style padding.
            For R maximal nonempty authored style/target runs, folding yields at most R ranges.
            Removing emphasis from their edges yields at most 3R. Only two range lists coexist
            during replacement. Destination offsets refer into one NUL-delimited byte buffer. */
        struct MarkdownInline
        {
          std::string bytes;
          std::string targets;
          std::vector<MarkdownRun> runs;

          /** Cost: one comparison with the previous target per nonempty linked segment. */
          size_t linkFor(const std::string &target)
          {
            if (target.empty())
              return 0;
            size_t link = this->runs.empty() ? 0 : this->runs.back().style.link;
            if (target != this->targets.c_str() + link)
            {
              link = this->targets.size();
              this->targets += target;
              this->targets += '\0';
            }
            return link;
          }

          /** Cost: collect and strictly validate each segment body and target once. */
          bool collect(const AttributedString &content, bool heading)
          {
            if (!content.valid())
              return false;
            this->targets += '\0';
            bool trailingCR = false;
            for (size_t s = 0; s < content.segmentCount(); ++s)
            {
              const AttributedString::Segment &seg = content.segment(s);
              const size_t first = this->bytes.size();
              std::string target;
              if (!CollectMarkdownText(seg.text, this->bytes) || !CollectMarkdownText(seg.link.text(), target)
                  || target.find_first_of("\r\n") != std::string::npos)
                return false;
              if (this->bytes.size() == first)
                continue;
              // The following LF owns a cross-segment CRLF. Withdraw the preceding
              // byte (and possibly its entire run) before deciding whether this link continues.
              size_t begin = first;
              if (trailingCR && this->bytes[first] == '\n')
              {
                --begin;
                this->runs.back().end = begin;
                if (this->runs.back().first == begin)
                  this->runs.pop_back();
              }
              trailingCR = this->bytes[this->bytes.size() - 1] == '\r';
              bool italic;
              TextWeight weight;
              const unsigned marks = (seg.style.queryWeight(weight) && weight == TEXT_WEIGHT_BOLD ? 2U : 0U)
                                     + (seg.style.queryItalic(italic) && italic ? 1U : 0U);
              MarkdownStyle style = {this->linkFor(target), marks, seg.style.family() == TEXT_FAMILY_MONOSPACE};
              // Within this segment both bytes have the same style. Compact toward begin.
              size_t write = begin;
              for (size_t i = first; i < this->bytes.size(); ++i)
              {
                char c = this->bytes[i];
                if (c == '\r' && i + 1 < this->bytes.size() && this->bytes[i + 1] == '\n')
                  continue;
                if (c == '\r' || c == '\n')
                  c = heading || style.code ? ' ' : '\n';
                this->bytes[write++] = c;
              }
              this->bytes.resize(write);
              AppendRun(this->runs, style, begin, write);
            }
            return true;
          }

          /** Cost: trim the paragraph's byte bounds, then scan each marked run once for its ink. */
          void normalizeEdges()
          {
            std::vector<MarkdownRun> projected;
            const size_t first = this->bytes.find_first_not_of('\n');
            if (first != std::string::npos)
            {
              const size_t end = this->bytes.find_last_not_of('\n') + 1;
              for (size_t r = 0; r < this->runs.size(); ++r)
              {
                MarkdownRun run = this->runs[r];
                if (run.first < first)
                  run.first = first;
                if (run.end > end)
                  run.end = end;
                if (run.first >= run.end)
                  continue;
                if (!run.style.marks)
                {
                  AppendRun(projected, run.style, run.first, run.end);
                  continue;
                }
                const MarkdownInk ink = FindMarkdownInk(this->bytes, run.first, run.end);
                MarkdownStyle plain = run.style;
                plain.marks = 0;
                AppendRun(projected, plain, run.first, ink.first);
                AppendRun(projected, run.style, ink.first, ink.end);
                AppendRun(projected, plain, ink.end, run.end);
              }
            }
            this->runs.swap(projected);
          }

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
              MarkdownRun &run = this->runs[r];
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
          const std::vector<MarkdownRun> &runs = input.runs;
          for (size_t i = 0; i < runs.size(); ++i)
          {
            const MarkdownRun &run = runs[i];
            const MarkdownRun *prev = i ? &runs[i - 1] : 0;
            const MarkdownRun *next = i + 1 < runs.size() ? &runs[i + 1] : 0;
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
            std::string raw, body;
            if (!CollectMarkdownText(p.text, raw))
              return false;
            for (size_t i = 0; i < raw.size(); ++i)
            {
              if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
                continue;
              body += raw[i] == '\r' ? '\n' : raw[i];
            }
            std::string fence = Backticks(body, 0, body.size(), 3);
            if (!body.empty() && body[body.size() - 1] != '\n')
              body += '\n';
            this->block(fence + "\n" + body + fence);
            return true;
          }
        };

        /** Cost: one dispatch per attached composition resident, pruning the block's projection. */
        inline bool WalkMarkdown(scene::Node *node, BlockReader &reader)
        {
          if (node->lifecycleFact() != scene::NODE_FACT_ATTACHED)
            return true;
          switch (node->readDocumentBlock(reader))
          {
          case scene::DOCUMENT_BLOCK_READ:
            return true;
          case scene::DOCUMENT_BLOCK_REFUSED:
            return false;
          case scene::DOCUMENT_BLOCK_NONE:
            break;
          }
          scene::INestable *nest = node->asNestable();
          for (scene::Node *child = nest ? nest->childrenHead() : 0; child; child = child->nextInComposition)
            if (!WalkMarkdown(child, reader))
              return false;
          return true;
        }
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
        if (!root || !detail::WalkMarkdown(root, reader))
          return false;
        out += reader.fragment;
        return true;
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
