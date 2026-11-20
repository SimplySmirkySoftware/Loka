#ifndef LOKA_APP_DOC_DETAIL_CONTAINER_WRITER_HPP
#define LOKA_APP_DOC_DETAIL_CONTAINER_WRITER_HPP
#include "app/doc/List.hpp"
#include "app/doc/Quote.hpp"

namespace loka
{
  namespace app
  {
    namespace doc
    {
      namespace detail
      {
        /** One export's semantic parents and their last emitted children.
            Cost: the synchronous exit owns a root and at most sixteen nested frames. */
        template <class Writer> class ContainerWriter : public BlockReader
        {
        protected:
          enum Kind
          {
            ROOT,
            LIST,
            QUOTE
          };
          struct Frame
          {
            Kind kind;
            char previous; // zero if empty, list marker for a list, 'p' for any other block
          };
          enum
          {
            MaxDepth = 16
          };
          Frame frames[MaxDepth + 1];
          unsigned depth;
          /** Cost: each explicit exit starts with a single empty root. */
          ContainerWriter()
              : depth(0)
          {
            this->frames[0].kind = ROOT;
            this->frames[0].previous = 0;
          }
          /** Cost: each semantic event checks its immediate parent once, including empty inputs. */
          bool accepts(bool item) const
          {
            return (this->frames[this->depth].kind == LIST) == item;
          }
          /** Cost: once per emitted leaf, visit at most sixteen enclosing frames, opening each once. */
          void beginLeaf()
          {
            Writer &writer = static_cast<Writer &>(*this);
            for (unsigned i = 0; i <= this->depth; ++i)
            {
              const bool leaf = i == this->depth;
              if (!leaf && this->frames[i + 1].previous)
                continue;
              Frame &parent = this->frames[i];
              const Kind child = leaf ? ROOT : this->frames[i + 1].kind;
              if (parent.previous && parent.kind != LIST)
                writer.separator(i);
              parent.previous = child == LIST ? (parent.previous == '-' ? '*' : '-') : 'p';
              if (!leaf)
                writer.open(child);
            }
          }

        private:
          /** Cost: the walk checks one parent and initializes one frame per container entry. */
          bool push(Kind kind)
          {
            if (!this->accepts(false) || this->depth == MaxDepth)
              return false;
            Frame &frame = this->frames[++this->depth];
            frame.kind = kind;
            frame.previous = 0;
            return true;
          }
          /** Cost: one frame and at most one closing tag per completed container. */
          bool pop()
          {
            Frame &frame = this->frames[this->depth--];
            if (frame.previous)
              static_cast<Writer &>(*this).close(frame.kind);
            return true;
          }
          /** Cost: once per visited List enter, one local frame. */
          virtual bool enter(const ListProps &)
          {
            return this->push(LIST);
          }
          /** Cost: once per visited List leave, one local frame. */
          virtual bool leave(const ListProps &)
          {
            return this->pop();
          }
          /** Cost: once per visited Quote enter, one local frame. */
          virtual bool enter(const QuoteProps &)
          {
            return this->push(QUOTE);
          }
          /** Cost: once per visited Quote leave, one local frame. */
          virtual bool leave(const QuoteProps &)
          {
            return this->pop();
          }
        };
      } // namespace detail
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
