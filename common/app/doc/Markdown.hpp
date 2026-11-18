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
          if (!platform::CollectUtf8(source, bytes))
            return false;
          unsigned scalar;
          for (size_t i = 0; i < bytes.size();)
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
          /** Cost: grouping compares neighboring styles once per scalar, without re-reading targets. */
          bool operator==(const MarkdownStyle &rhs) const
          {
            return this->link == rhs.link && this->marks == rhs.marks && this->code == rhs.code;
          }
        };
        /** A scalar references its original bytes even when its projected value changes. */
        struct MarkdownChar
        {
          MarkdownStyle style;
          size_t offset;
          unsigned scalar;
          unsigned length;
          bool edge;
        };
        /** Nonempty span of equal projected style with its escaped or fenced body. */
        struct MarkdownRun
        {
          MarkdownStyle style;
          std::string body;
        };

        /** Cost: one pass over each byte in the run being fenced. */
        inline std::string Backticks(const std::string &body, size_t minimum)
        {
          size_t length = minimum, current = 0;
          for (size_t i = 0; i < body.size(); ++i)
          {
            current = body[i] == '`' ? current + 1 : 0;
            if (current >= length)
              length = current + 1;
          }
          return std::string(length, '`');
        }
        /** Cost: the speller fences one code run, scanning its bytes for delimiter width and padding. */
        inline bool InlineCode(std::string &body)
        {
          const std::string fence = Backticks(body, 1);
          bool pad =
              body[0] == '`' || body[body.size() - 1] == '`'
              || (body[0] == ' ' && body[body.size() - 1] == ' ' && body.find_first_not_of(' ') != std::string::npos);
          if (pad && body[0] == ' ')
          {
            // CommonMark tests for ASCII spaces only. Some readers trim using Unicode whitespace.
            size_t i = 0;
            while (i < body.size())
            {
              unsigned scalar = 0;
              core::detail::DecodeUtf8Codepoint(body.data(), body.size(), i, scalar);
              if (!EdgeSpace(scalar) && !ExtraStripSpace(scalar))
                break;
              if (i == body.size())
                return false;
            }
          }
          body = fence + (pad ? " " : "") + body + (pad ? " " : "") + fence;
          return true;
        }

        /** Temporary normalized projection of one paragraph or heading. Cost: linear passes over its segments and
         * scalars. */
        struct MarkdownInline
        {
          std::string bytes;
          std::vector<std::string> targets;
          std::vector<MarkdownChar> chars;

          /** After CRLF folding, continue the preceding link if its destination bytes match.
              Cost: one comparison per nonempty linked segment, against its immediate predecessor only. */
          size_t linkFor(const std::string &target)
          {
            if (target.empty())
              return 0;
            size_t link = this->chars.empty() ? 0 : this->chars.back().style.link;
            if (this->targets[link] != target)
            {
              this->targets.push_back(target);
              link = this->targets.size() - 1;
            }
            return link;
          }

          /** Cost: one collection per authored segment body and destination, one projection per scalar. */
          bool collect(const AttributedString &content)
          {
            if (!content.valid())
              return false;
            this->targets.reserve(content.segmentCount() + 1);
            this->targets.push_back("");
            for (size_t s = 0; s < content.segmentCount(); ++s)
            {
              const AttributedString::Segment &seg = content.segment(s);
              std::string body, target;
              if (!platform::CollectUtf8(seg.text, body) || !CollectMarkdownText(seg.link.text(), target)
                  || target.find_first_of("\r\n") != std::string::npos)
                return false;
              if (body.empty())
                continue;
              bool italic;
              TextWeight weight;
              const unsigned marks = (seg.style.queryWeight(weight) && weight == TEXT_WEIGHT_BOLD ? 2U : 0U)
                                     + (seg.style.queryItalic(italic) && italic ? 1U : 0U);
              MarkdownStyle style = {0, marks, seg.style.family() == TEXT_FAMILY_MONOSPACE};
              size_t base = this->bytes.size();
              this->bytes += body;
              for (size_t i = 0; i < body.size();)
              {
                size_t start = i;
                unsigned scalar = 0;
                if (!core::detail::DecodeUtf8Codepoint(body.data(), body.size(), i, scalar) || !scalar)
                  return false;
                if (scalar == '\n' && !this->chars.empty() && this->chars.back().scalar == '\r')
                  this->chars.pop_back();
                if (!start)
                  style.link = this->linkFor(target);
                MarkdownChar c = {style, base + start, scalar, static_cast<unsigned>(i - start), false};
                this->chars.push_back(c);
              }
            }
            return true;
          }

          /** Cost: the inline writer visits the block's scalars in a bounded number of linear passes. */
          void normalize(bool heading)
          {
            for (size_t i = 0; i < this->chars.size(); ++i)
            {
              MarkdownChar &c = this->chars[i];
              if (c.scalar == '\r' || c.scalar == '\n')
                c.scalar = heading || c.style.code ? ' ' : '\n';
            }
            size_t first = 0;
            while (first < this->chars.size() && this->chars[first].scalar == '\n')
              ++first;
            this->chars.erase(this->chars.begin(), this->chars.begin() + first);
            while (!this->chars.empty() && this->chars.back().scalar == '\n')
              this->chars.pop_back();
            for (size_t i = 0; i < this->chars.size();)
            {
              size_t end = i + 1;
              while (end < this->chars.size() && this->chars[end].style == this->chars[i].style)
                ++end;
              size_t left = i, right = end;
              while (left < end && EdgeSpace(this->chars[left].scalar))
                this->chars[left++].style.marks = 0;
              while (right > left && EdgeSpace(this->chars[right - 1].scalar))
                this->chars[--right].style.marks = 0;
              i = end;
            }
            bool leading = true, trailing = true;
            for (size_t i = 0; i < this->chars.size(); ++i)
            {
              MarkdownChar &a = this->chars[i], &b = this->chars[this->chars.size() - 1 - i];
              leading = a.scalar == '\n' || (leading && EdgeSpace(a.scalar));
              trailing = b.scalar == '\n' || (trailing && EdgeSpace(b.scalar));
              a.edge |= leading;
              b.edge |= trailing;
            }
          }

          /** Cost: once per block, writes its projected scalars into maximal runs. */
          bool spell(std::vector<MarkdownRun> &runs) const
          {
            size_t count = 0;
            for (size_t i = 0; i < this->chars.size(); ++i)
              if (!i || !(this->chars[i - 1].style == this->chars[i].style))
                ++count;
            runs.reserve(count);
            bool lineStart = true, digits = false;
            for (size_t i = 0; i < this->chars.size(); ++i)
            {
              const MarkdownChar &c = this->chars[i];
              if ((i == 0 || i + 1 == this->chars.size()) && !c.style.link && !c.style.marks && !c.style.code
                  && ExtraStripSpace(c.scalar))
                return false;
              if (runs.empty() || !(runs.back().style == c.style))
              {
                // A generated label, code fence or emphasis marker interrupts a physical list prefix.
                if (!runs.empty() || c.style.link || c.style.marks || c.style.code)
                  lineStart = digits = false;
                MarkdownRun run = {c.style, ""};
                runs.push_back(run);
              }
              std::string &body = runs.back().body;
              unsigned v = c.scalar;
              if (!c.style.code && v == '\n')
                body += "\\\n";
              else if (!c.style.code && c.edge && EdgeSpace(v))
              {
                char ref[16];
                ::snprintf(ref, sizeof(ref), v < 128 ? "&#%u;" : "&#x%X;", v);
                body += ref;
              }
              else
              {
                bool beforeLink = i + 1 < this->chars.size() && this->chars[i + 1].style.link
                                  && this->chars[i + 1].style.link != c.style.link && !c.style.link && !c.style.marks;
                if (!c.style.code && v < 128
                    && (std::strchr("\\`*_[]<&#~", v) || (lineStart && std::strchr(">+-=", v))
                        || (digits && (v == '.' || v == ')')) || (v == '!' && beforeLink)))
                  body += '\\';
                if (v < 128)
                  body += static_cast<char>(v);
                else
                  body.append(this->bytes, c.offset, c.length);
              }
              digits = (lineStart || digits) && v >= '0' && v <= '9';
              lineStart = v == '\n';
            }
            for (size_t i = 0; i < runs.size(); ++i)
              if (runs[i].style.code && !InlineCode(runs[i].body))
                return false;
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
        inline void CloseMarkdownLink(std::string &out, const std::string &target)
        {
          out += "](<";
          for (size_t i = 0; i < target.size(); ++i)
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
          if (!input.collect(content))
            return false;
          input.normalize(heading);
          std::vector<MarkdownRun> runs;
          if (!input.spell(runs))
            return false;
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
              out += run.body;
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
                  right = next->body[0];
              }
              bool touching = !open && prev && prev->style.link == run.style.link && prev->style.marks;
              bool literal = next && !close && !next->style.marks && !next->style.code
                             && next->style.link == run.style.link
                             && (next->body.compare(0, 2, "\\*") == 0 || next->body.compare(0, 2, "\\_") == 0);
              bool stars = !touching && !literal && StarEdge(left, run.body[0])
                           && StarEdge(right, run.body[run.body.size() - 1]);
              if (stars)
              {
                const std::string marker(marks, '*');
                out += marker + run.body + marker;
              }
              else
              {
                if (marks & 2)
                  out += "<strong>";
                if (marks & 1)
                  out += "<em>";
                out += run.body;
                if (marks & 1)
                  out += "</em>";
                if (marks & 2)
                  out += "</strong>";
              }
            }
            if (close)
              CloseMarkdownLink(out, input.targets[run.style.link]);
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
            std::string fence = Backticks(body, 3);
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
          Recursion follows tree depth. Peak storage includes out, staged fragment, scratch and destination growth. */
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
