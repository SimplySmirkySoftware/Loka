#ifndef LOKA_APP_DOC_HTML_HPP
#define LOKA_APP_DOC_HTML_HPP

#include "app/doc/detail/Projection.hpp"
#include "app/doc/detail/ContainerWriter.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        /** Cost: the inline and block writers escape one original byte per call. */
        inline void AppendHtmlByte(std::string &out, char c, bool attribute)
        {
          switch (c)
          {
          case '&':
            out += "&amp;";
            break;
          case '<':
            out += "&lt;";
            break;
          case '>':
            out += "&gt;";
            break;
          case '"':
            if (attribute)
              out += "&quot;";
            else
              out += c;
            break;
          default:
            out += c;
          }
        }

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
          /** Cost: once per inline block, walks its projected runs and appends their escaped bytes. */
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
            const HtmlTags empty = {{0, 0, 0, 0}};
            HtmlTags current = empty;
            for (size_t i = 0; i < input.runs.size(); ++i)
            {
              const InlineRun &run = input.runs[i];
              const HtmlTags next = {{run.style.link, run.style.marks & 2U, run.style.marks & 1U, run.style.code}};
              current.transition(next, input.targets, this->fragment);
              for (size_t j = run.first; j < run.end; ++j)
              {
                if (input.bytes[j] == '\n')
                  this->fragment += "<br>";
                else
                  AppendHtmlByte(this->fragment, input.bytes[j], false);
              }
              current = next;
            }
            current.transition(empty, input.targets, this->fragment);
            this->fragment += "</";
            this->fragment += tag;
            this->fragment += ">\n";
            return true;
          }

        public:
          using ContainerWriter<HtmlReader>::read;
          std::string fragment;
          /** Cost: HTML needs no separator between complete elements. */
          void separator(unsigned) {}
          /** Cost: one opening tag per emitted container. */
          void open(Kind kind)
          {
            this->fragment += kind == LIST ? "<ul>\n" : "<blockquote>\n";
          }
          /** Cost: one closing tag per emitted container. */
          void close(Kind kind)
          {
            this->fragment += kind == LIST ? "</ul>\n" : "</blockquote>\n";
          }
          /** Cost: once per image, emit one block and escape both of its attribute values. */
          void image(const std::string &source, const std::string &alt)
          {
            this->beginLeaf();
            this->fragment += "<p><img src=\"";
            for (size_t i = 0; i < source.size(); ++i)
              AppendHtmlByte(this->fragment, source[i], true);
            this->fragment += "\" alt=\"";
            for (size_t i = 0; i < alt.size(); ++i)
              AppendHtmlByte(this->fragment, alt[i], true);
            this->fragment += "\"></p>\n";
          }
          /** Cost: each visited item checks one parent and projects its own runs once. */
          virtual bool read(const ItemProps &p)
          {
            return this->accepts(true) && this->inlineBlock(p.content, false, "li");
          }

          /** Cost: the walk projects the authored content once per attached heading. */
          virtual bool read(const HeadingProps &p)
          {
            if (!this->accepts(false))
              return false;
            char tag[] = "h1";
            tag[1] = static_cast<char>('0' + p.level());
            return this->inlineBlock(p.content, true, tag);
          }
          /** Cost: the walk projects the authored content once per attached paragraph. */
          virtual bool read(const ParagraphProps &p)
          {
            if (!this->accepts(false))
              return false;
            return this->inlineBlock(p.content, false, "p");
          }
          /** Cost: the walk collects and escapes the body once per attached code block. */
          virtual bool read(const CodeBlockProps &p)
          {
            if (!this->accepts(false))
              return false;
            std::string body;
            if (!CollectCodeBlock(p.text, body))
              return false;
            this->beginLeaf();
            this->fragment += "<pre><code>";
            for (size_t i = 0; i < body.size(); ++i)
              AppendHtmlByte(this->fragment, body[i], false);
            this->fragment += "</code></pre>\n";
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
