#ifndef LOKA_APP_DOC_DETAIL_PROJECTION_HPP
#define LOKA_APP_DOC_DETAIL_PROJECTION_HPP

#include "app/doc/Heading.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/doc/CodeBlock.hpp"
#include "core/Utf8Decode.hpp"
#include "platform/StringUTF8.hpp"
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
        inline bool CollectDocumentText(const core::String &source, std::string &bytes)
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

        /** One normalized style. Equal consecutive targets share a block-local index. */
        struct InlineStyle
        {
          size_t link;
          unsigned marks;
          bool code;
          /** Cost: one constant-time comparison per interval contribution, without re-reading targets. */
          bool operator==(const InlineStyle &rhs) const
          {
            return this->link == rhs.link && this->marks == rhs.marks && this->code == rhs.code;
          }
        };
        /** Half-open byte interval in the current buffer. No per-character storage. */
        struct InlineRun
        {
          InlineStyle style;
          size_t first;
          size_t end;
        };

        /** Cost: O(1) amortized per interval contributed during collection, folding or edge splitting. */
        inline void AppendRun(std::vector<InlineRun> &runs, InlineStyle style, size_t first, size_t end)
        {
          if (first == end)
            return;
          if (runs.empty() || !(runs.back().style == style))
          {
            InlineRun run = {style, first, end};
            runs.push_back(run);
          }
          else
            runs.back().end = end;
        }

        /** Half-open interval containing the ink in a validated run or physical line. */
        struct DocumentInk
        {
          size_t first, end;
        };

        /** Cost: one scalar scan of the specified range. All-whitespace input returns an empty suffix. */
        inline DocumentInk FindDocumentInk(const std::string &bytes, size_t first, size_t end)
        {
          DocumentInk ink = {end, end};
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

        /** Block-local scratch = O(runs) records + collected bytes + staged output.
            Each InlineRun costs sizeof(InlineStyle) + 2*sizeof(size_t), including style padding.
            For R maximal nonempty authored style/target runs, folding yields at most R ranges.
            Removing emphasis from their edges yields at most 3R. Only two range lists coexist
            during replacement. Destination offsets refer into one NUL-delimited byte buffer. */
        struct InlineProjection
        {
          std::string bytes;
          std::string targets;
          std::vector<InlineRun> runs;

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
              if (!CollectDocumentText(seg.text, this->bytes) || !CollectDocumentText(seg.link.text(), target)
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
              InlineStyle style = {this->linkFor(target), marks, seg.style.family() == TEXT_FAMILY_MONOSPACE};
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
            std::vector<InlineRun> projected;
            const size_t first = this->bytes.find_first_not_of('\n');
            if (first != std::string::npos)
            {
              const size_t end = this->bytes.find_last_not_of('\n') + 1;
              for (size_t r = 0; r < this->runs.size(); ++r)
              {
                InlineRun run = this->runs[r];
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
                const DocumentInk ink = FindDocumentInk(this->bytes, run.first, run.end);
                InlineStyle plain = run.style;
                plain.marks = 0;
                AppendRun(projected, plain, run.first, ink.first);
                AppendRun(projected, run.style, ink.first, ink.end);
                AppendRun(projected, plain, ink.end, run.end);
              }
            }
            this->runs.swap(projected);
          }
        };

        /** Cost: one collection and one newline scan of the visited code block's bytes. */
        inline bool CollectCodeBlock(const core::String &text, std::string &body)
        {
          std::string raw;
          if (!CollectDocumentText(text, raw))
            return false;
          for (size_t i = 0; i < raw.size(); ++i)
          {
            if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n')
              continue;
            body += raw[i] == '\r' ? '\n' : raw[i];
          }
          if (!body.empty() && body[body.size() - 1] != '\n')
            body += '\n';
          return true;
        }

        /** Cost: one dispatch per attached composition resident, pruning the block's projection. */
        inline bool WalkDocumentBlocks(scene::Node *node, BlockReader &reader)
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
            if (!WalkDocumentBlocks(child, reader))
              return false;
          return true;
        }
      } // namespace detail
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
