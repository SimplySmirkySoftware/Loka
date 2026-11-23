#ifndef LOKA_APP_DOC_DETAIL_CONTAINER_WRITER_HPP
#define LOKA_APP_DOC_DETAIL_CONTAINER_WRITER_HPP
#include "app/doc/Image.hpp"
#include "app/doc/detail/Projection.hpp"
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
            NUMBERED_LIST,
            QUOTE
          };
          struct Frame
          {
            Kind kind;

            void reset(Kind parent)
            {
              this->kind = parent;
              this->previous = 0;
            }
            bool emitted() const
            {
              return this->previous != 0;
            }
            bool isList() const
            {
              return this->kind == LIST || this->kind == NUMBERED_LIST;
            }
            bool acceptsOrdinal(unsigned ordinal) const
            {
              return this->kind == NUMBERED_LIST && ordinal <= NumberedItemProps::MaximumOrdinal
                     && ordinal == static_cast<unsigned>(this->previous) + 1;
            }
            char siblingMarker() const
            {
              assert(!this->isList());
              return this->previous;
            }
            void recordChild(Kind child)
            {
              if (this->kind == NUMBERED_LIST)
                ++this->previous;
              else if (child == LIST)
                this->previous = this->previous == '-' ? '*' : '-';
              else if (child == NUMBERED_LIST)
                this->previous = this->previous == '.' ? ')' : '.';
              else
                this->previous = 'p';
            }

          private:
            char previous;
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
            this->frames[0].reset(ROOT);
          }
          /** Cost: each semantic event checks its immediate parent once, including empty inputs. */
          bool accepts(bool item) const
          {
            const Frame &parent = this->frames[this->depth];
            return item ? parent.kind == LIST : !parent.isList();
          }
          /** Cost: once per emitted leaf, visit at most sixteen enclosing frames, opening each once. */
          void beginLeaf()
          {
            Writer &writer = static_cast<Writer &>(*this);
            for (unsigned i = 0; i <= this->depth; ++i)
            {
              const bool leaf = i == this->depth;
              if (!leaf && this->frames[i + 1].emitted())
                continue;
              Frame &parent = this->frames[i];
              const Kind child = leaf ? ROOT : this->frames[i + 1].kind;
              if (parent.emitted() && !parent.isList())
                writer.separator(i);
              parent.recordChild(child);
              if (!leaf)
                writer.open(child);
            }
          }

          using BlockReader::read;
          /** Cost: once per image, check its parent and collect its two fields before emitting it. */
          virtual bool read(const ImageProps &p)
          {
            std::string source, alt;
            if (!this->accepts(false) || !CollectDocumentLine(p.source, source) || source.empty()
                || !CollectDocumentLine(p.alt, alt))
              return false;
            return static_cast<Writer &>(*this).image(source, alt, p.size.get());
          }

        private:
          /** Cost: the walk checks one parent and initializes one frame per container entry. */
          bool push(Kind kind)
          {
            if (!this->accepts(false) || this->depth == MaxDepth)
              return false;
            Frame &frame = this->frames[++this->depth];
            frame.reset(kind);
            return true;
          }
          /** Cost: one frame and at most one closing tag per completed container. */
          bool pop()
          {
            Frame &frame = this->frames[this->depth--];
            if (frame.emitted())
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
          virtual bool enter(const NumberedListProps &)
          {
            return this->push(NUMBERED_LIST);
          }
          virtual bool leave(const NumberedListProps &)
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
