#ifndef LOKA_APP_DOC_HTML_HPP
#define LOKA_APP_DOC_HTML_HPP

#include "app/doc/detail/Projection.hpp"
#include "app/doc/detail/ContainerWriter.hpp"
#include "app/doc/detail/HtmlImage.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        /** Four ordered tag slots. Zero means absent. The anchor slot holds the destination offset. */
        struct HtmlTags
        {
          size_t slots[4];

          /** Cost: once per run transition and block end, visits at most four slots and each newly opened href. */
          void transition(const HtmlTags &next, const std::string &targets, std::string &out) const
          {
            size_t keep = 0;
            while (keep < 4 && this->slots[keep] == next.slots[keep])
              ++keep;
            const char *const close[] = {"</a>", "</strong>", "</em>", "</code>"};
            const char *const open[] = {"<a href=\"", "<strong>", "<em>", "<code>"};
            for (size_t i = 4; i > keep; --i)
              if (this->slots[i - 1])
                out += close[i - 1];
            for (size_t i = keep; i < 4; ++i)
            {
              if (!next.slots[i])
                continue;
              out += open[i];
              if (i == 0)
              {
                for (size_t j = next.slots[0]; targets[j]; ++j)
                  AppendHtmlByte(out, targets[j], true);
                out += "\">";
              }
            }
          }
        };

        /** Owns one staged fragment for the synchronous tree borrow. */
        class HtmlReader : public ContainerWriter<HtmlReader>
        {
          /** Each run writes its escaped text and the tags changed at its boundaries. */
          static void appendRuns(const InlineProjection &input, std::string &out)
          {
            const HtmlTags empty = {{0, 0, 0, 0}};
            HtmlTags current = empty;
            for (size_t i = 0; i < input.runs.size(); ++i)
            {
              const InlineRun &run = input.runs[i];
              const HtmlTags next = {{run.style.link, run.style.marks & 2U, run.style.marks & 1U, run.style.code}};
              current.transition(next, input.targets, out);
              for (size_t j = run.first; j < run.end; ++j)
              {
                if (input.bytes[j] == '\n')
                  out += "<br>";
                else
                  AppendHtmlByte(out, input.bytes[j], false);
              }
              current = next;
            }
            current.transition(empty, input.targets, out);
          }
          bool parentNeedsNewline() const
          {
            return this->frames[this->depth].kind != NESTED_ITEM;
          }
          bool inlineBlock(const AttributedString &content, bool heading, const char *tag)
          {
            InlineProjection input;
            if (!input.collect(content, heading))
              return false;
            input.normalizeEdges();
            if (!heading && input.runs.empty())
              return true;
            this->beginLeaf();
            this->fragment += '<';
            this->fragment += tag;
            this->fragment += '>';
            appendRuns(input, this->fragment);
            this->fragment += "</";
            this->fragment += tag;
            this->fragment += ">\n";
            return true;
          }

        public:
          using ContainerWriter<HtmlReader>::read;
          std::string fragment;
          bool spellItem(const AttributedString &content, std::string &body)
          {
            InlineProjection input;
            if (!input.collect(content, false))
              return false;
            input.normalizeEdges();
            appendRuns(input, body);
            return true;
          }
          void item(unsigned, const std::string &body, bool withChildren)
          {
            this->fragment += "<li>";
            this->fragment += body;
            if (!withChildren)
              this->fragment += "</li>\n";
          }
          void separator(unsigned) {}
          void open(unsigned level, bool)
          {
            const Frame &frame = this->frames[level];
            if (frame.kind == NESTED_ITEM)
              this->item(level - 1, frame.head, true);
            else
              this->fragment += frame.kind == LIST            ? "<ul>\n"
                                : frame.kind == NUMBERED_LIST ? "<ol>\n"
                                                              : "<blockquote>\n";
          }
          void close(Kind kind)
          {
            switch (kind)
            {
            case LIST:
              this->fragment += "</ul>";
              break;
            case NUMBERED_LIST:
              this->fragment += "</ol>";
              break;
            case NESTED_ITEM:
              this->fragment += "</li>";
              break;
            case QUOTE:
              this->fragment += "</blockquote>";
              break;
            case ROOT:
              return;
            }
            if (this->parentNeedsNewline())
              this->fragment += '\n';
          }
          void rule()
          {
            this->beginLeaf();
            this->fragment += "<hr>\n";
          }
          /** Cost: once per image, emit one block and escape both of its attribute values. */
          bool image(const std::string &source, const std::string &alt, const ImageSize *size)
          {
            this->beginLeaf();
            this->fragment += "<p>";
            if (!AppendHtmlImage(this->fragment, source, alt, size))
              return false;
            this->fragment += "</p>\n";
            return true;
          }
          /** Cost: the walk projects the authored content once per attached heading. */
          virtual bool read(const HeadingProps &p)
          {
            if (!this->acceptsBlock())
              return false;
            char tag[] = "h1";
            tag[1] = static_cast<char>('0' + p.level());
            return this->inlineBlock(p.content, true, tag);
          }
          /** Cost: the walk projects the authored content once per attached paragraph. */
          virtual bool read(const ParagraphProps &p)
          {
            if (!this->acceptsBlock())
              return false;
            return this->inlineBlock(p.content, false, "p");
          }
          /** Cost: the walk collects and escapes the body once per attached code block. */
          virtual bool read(const CodeBlockProps &p)
          {
            if (!this->acceptsListOrCodeBlock())
              return false;
            std::string body;
            if (!CollectCodeBlock(p.text, body))
              return false;
            this->beginLeaf();
            this->fragment += "<pre><code>";
            for (size_t i = 0; i < body.size(); ++i)
              AppendHtmlByte(this->fragment, body[i], false);
            this->fragment += "</code></pre>";
            if (this->parentNeedsNewline())
              this->fragment += '\n';
            return true;
          }
        };
      } // namespace detail

      /** Append a UTF-8 HTML fragment for a flow context. The caller lends a stable tree on the Main Thread
          and supplies any prefix separator. Refusal preserves out. Link destinations are trusted input.
          Cost: once per explicit export, walk the attached subtree and stage its projected blocks. */
      inline bool WriteHtml(scene::Node *root, std::string &out)
      {
        detail::HtmlReader reader;
        if (!root || !detail::WalkDocumentBlocks(root, reader))
          return false;
        out += reader.fragment;
        return true;
      }
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
