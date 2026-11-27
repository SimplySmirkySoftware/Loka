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
            NESTED_ITEM,
            QUOTE
          };
          struct Frame
          {
            Kind kind;
            std::string head;

            // Before any output, a shell can hold a head and an ordered list can have accepted ordinal 1.
            // Subsequent emitted children reuse the ordinary ordinal or sibling-marker representation.
            enum
            {
              Pending = -1
            };
            bool needsHead() const
            {
              return this->kind == NESTED_ITEM && this->previous == 0;
            }
            void captureHead(std::string &body)
            {
              this->head.swap(body);
              this->previous = Pending;
            }
            unsigned ordinal() const
            {
              return this->previous == Pending ? 1U : static_cast<unsigned>(this->previous);
            }
            void advanceOrdinal()
            {
              this->previous = static_cast<signed char>(this->previous ? this->previous + 1 : Pending);
            }

            void reset(Kind parent)
            {
              this->kind = parent;
              this->head.clear();
              this->previous = 0;
            }
            bool emitted() const
            {
              return this->previous > 0;
            }
            bool isList() const
            {
              return this->kind == LIST || this->kind == NUMBERED_LIST;
            }
            bool acceptsOrdinal(unsigned ordinal) const
            {
              return this->kind == NUMBERED_LIST && ordinal <= NumberedItemProps::MaximumOrdinal
                     && ordinal == this->ordinal() + 1;
            }
            char siblingMarker() const
            {
              assert(!this->isList());
              return this->previous;
            }
            void recordChild(Kind child)
            {
              if (this->kind == NUMBERED_LIST)
                this->previous = static_cast<signed char>(this->ordinal());
              else if (child == LIST)
                this->previous = this->previous == '-' ? '*' : '-';
              else if (child == NUMBERED_LIST)
                this->previous = this->previous == '.' ? ')' : '.';
              else
                this->previous = 'p';
            }

          private:
            signed char previous;
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
          bool acceptsBlock() const
          {
            const Kind parent = this->frames[this->depth].kind;
            return parent == ROOT || parent == QUOTE;
          }

          bool acceptsListOrCodeBlock() const
          {
            const Frame &frame = this->frames[this->depth];
            return this->acceptsBlock() || (frame.kind == NESTED_ITEM && !frame.needsHead());
          }

          /** Open the ancestors of a visible leaf, outside in. A pending shell opens with its owned head. */
          void beginLeaf(unsigned level, bool bare)
          {
            Writer &writer = static_cast<Writer &>(*this);
            for (unsigned i = 0; i <= level; ++i)
            {
              const bool leaf = i == level;
              if (!leaf && this->frames[i + 1].emitted())
                continue;
              Frame &parent = this->frames[i];
              const Kind child = leaf ? ROOT : this->frames[i + 1].kind;
              if (parent.emitted() && (parent.kind == ROOT || parent.kind == QUOTE))
                writer.separator(i);
              parent.recordChild(child);
              // A shell's first line can come directly from a leaf or from the first item in a child list.
              if (!leaf)
                writer.open(i + 1, bare && i + 2 >= level);
            }
          }

          void beginLeaf()
          {
            this->beginLeaf(this->depth, false);
          }

          using BlockReader::read;
          /** One projection per accepted item. Shells retain the spelling until a child emits or the shell ends. */
          bool readItem(const AttributedString &content, const NumberedItemProps *numbered)
          {
            Frame &parent = this->frames[this->depth];
            const bool nested = parent.kind == NESTED_ITEM;
            if (nested && !parent.needsHead())
              return false;
            const unsigned level = nested ? this->depth - 1 : this->depth;
            Frame &list = this->frames[level];
            if (numbered ? !list.acceptsOrdinal(numbered->ordinal()) : list.kind != LIST)
              return false;
            Writer &writer = static_cast<Writer &>(*this);
            std::string body;
            if (!writer.spellItem(content, body))
              return false;
            if (numbered)
              list.advanceOrdinal();
            if (nested)
              parent.captureHead(body);
            else if (numbered || !body.empty())
            {
              this->beginLeaf(level, body.empty());
              writer.item(level, body, false);
            }
            return true;
          }
          virtual bool read(const ItemProps &p)
          {
            return this->readItem(p.content, 0);
          }
          virtual bool read(const NumberedItemProps &p)
          {
            return this->readItem(p.content, &p);
          }

          /** Cost: once per image, check its parent and collect its two fields before emitting it. */
          virtual bool read(const ImageProps &p)
          {
            std::string source, alt;
            if (!this->acceptsBlock() || !CollectDocumentLine(p.source, source) || source.empty()
                || !CollectDocumentLine(p.alt, alt))
              return false;
            return static_cast<Writer &>(*this).image(source, alt, p.size.get());
          }

        private:
          /** Cost: the walk checks one parent and initializes one frame per container entry. */
          bool push(Kind kind)
          {
            const Frame &parent = this->frames[this->depth];
            const bool admitted = kind == NESTED_ITEM ? parent.isList()
                                  : kind == QUOTE     ? this->acceptsBlock()
                                                      : this->acceptsListOrCodeBlock();
            if (!admitted || this->depth == MaxDepth)
              return false;
            this->frames[++this->depth].reset(kind);
            return true;
          }
          /** A shell without visible children follows the same emission rule as a flat item. */
          bool pop(Kind kind)
          {
            Frame &frame = this->frames[this->depth];
            if (frame.kind != kind || frame.needsHead())
              return false;
            Writer &writer = static_cast<Writer &>(*this);
            --this->depth;
            if (frame.emitted())
              writer.close(kind);
            else if (kind == NESTED_ITEM && (!frame.head.empty() || this->frames[this->depth].kind == NUMBERED_LIST))
            {
              this->beginLeaf(this->depth, frame.head.empty());
              writer.item(this->depth, frame.head, false);
            }
            return true;
          }
          virtual bool enter(const NestedItemProps &)
          {
            return this->push(NESTED_ITEM);
          }
          virtual bool leave(const NestedItemProps &)
          {
            return this->pop(NESTED_ITEM);
          }
          /** Cost: once per visited List enter, one local frame. */
          virtual bool enter(const ListProps &)
          {
            return this->push(LIST);
          }
          /** Cost: once per visited List leave, one local frame. */
          virtual bool leave(const ListProps &)
          {
            return this->pop(LIST);
          }
          virtual bool enter(const NumberedListProps &)
          {
            return this->push(NUMBERED_LIST);
          }
          virtual bool leave(const NumberedListProps &)
          {
            return this->pop(NUMBERED_LIST);
          }
          /** Cost: once per visited Quote enter, one local frame. */
          virtual bool enter(const QuoteProps &)
          {
            return this->push(QUOTE);
          }
          /** Cost: once per visited Quote leave, one local frame. */
          virtual bool leave(const QuoteProps &)
          {
            return this->pop(QUOTE);
          }
        };
      } // namespace detail
    } // namespace doc
  } // namespace app
} // namespace loka
#endif
