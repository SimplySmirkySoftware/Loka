#ifndef LOKA_APP_LAYOUT_STACK_SPANS_HPP
#define LOKA_APP_LAYOUT_STACK_SPANS_HPP

#include "app/layout/LazyLayout.hpp"
#include "dsl/composition/CompositionList.hpp"
#include "app/layout/TextLineBreaker.hpp"

namespace loka
{
  namespace app
  {
    namespace layout
    {

      /** Measured, content-relative Column advances. Construction is pass-local;
          only finish() publishes a reusable table. No resident pointers are retained.
          The owner invalidates on constraints, structure, props, reattach or lost
          eligibility, and records the viewport only after successful placement. */
      class StackSpans
      {
      public:
        StackSpans()
            : filled_(0),
              completed_(false),
              placedViewport_(0, 0, 0, 0)
        {
        }

        bool begin(unsigned count)
        {
          this->invalidate();
          if (count == UINT_MAX || !this->spans_.allocate(static_cast<std::size_t>(count) + 1))
            return false;
          this->spans_[0].start = 0;
          this->spans_[0].end = 0;
          return true;
        }

        bool append(int start, int end)
        {
          if (!this->spans_.valid() || this->completed_ || this->filled_ + 1 >= this->spans_.size()
              || start < this->spans_[this->filled_].end || end < start || end > SHRT_MAX)
          {
            this->invalidate();
            return false;
          }
          ++this->filled_;
          this->spans_[this->filled_].start = static_cast<short>(start);
          this->spans_[this->filled_].end = static_cast<short>(end);
          return true;
        }

        bool finish()
        {
          this->completed_ = this->spans_.valid() && this->filled_ + 1 == this->spans_.size();
          return this->completed_;
        }

        bool valid() const
        {
          return this->completed_;
        }
        void invalidate()
        {
          this->spans_.clear();
          this->filled_ = 0;
          this->completed_ = false;
          this->placedViewport_ = core::Frame(0, 0, 0, 0);
        }
        unsigned size() const
        {
          return this->valid() ? this->filled_ : 0;
        }
        short start(unsigned index) const
        {
          assert(this->valid() && index < this->filled_);
          return this->spans_[index + 1].start;
        }
        short end(unsigned index) const
        {
          assert(this->valid() && index < this->filled_);
          return this->spans_[index + 1].end;
        }
        short total() const
        {
          return this->valid() ? this->spans_[this->filled_].end : 0;
        }
        const core::Frame &placedViewport() const
        {
          return this->placedViewport_;
        }
        void placed(const core::Frame &viewport)
        {
          assert(this->valid());
          this->placedViewport_ = viewport;
        }

        LazyWindow indicesIn(int top, int bottom) const
        {
          LazyWindow result = {0, 0};
          if (!this->valid() || bottom <= top || bottom <= 0 || top >= this->total())
            return result;
          unsigned lo = 0, hi = this->filled_;
          while (lo < hi)
          {
            const unsigned mid = lo + (hi - lo) / 2;
            if (this->spans_[mid + 1].end <= top)
              lo = mid + 1;
            else
              hi = mid;
          }
          const unsigned first = lo;
          hi = this->filled_;
          while (lo < hi)
          {
            const unsigned mid = lo + (hi - lo) / 2;
            if (this->spans_[mid + 1].start < bottom)
              lo = mid + 1;
            else
              hi = mid;
          }
          result.first = first;
          result.count = lo - first;
          return result;
        }

        LazyWindow indicesIn(const core::Frame &viewport) const
        {
          if (!viewport.hasSize() || viewport.y > INT_MAX - viewport.height)
          {
            LazyWindow empty = {0, 0};
            return empty;
          }
          return this->indicesIn(viewport.y, viewport.y + viewport.height);
        }

      private:
        struct Span
        {
          short start;
          short end;
        };
        detail::TextMeasureTable<Span> spans_;
        unsigned filled_;
        bool completed_;
        core::Frame placedViewport_;
        StackSpans(const StackSpans &);
        StackSpans &operator=(const StackSpans &);
      };

      /** Only text leaves have the projection contract required for banded placement. */
      inline bool mayBandColumn(StackNode *column)
      {
        if (!column || column->props.effectiveAxis() != STACK_AXIS_COLUMN)
          return false;
        dsl::CompositionCursor<scene::Node> children(column->childrenHead(), column->childrenCount());
        for (scene::Node *child = children.next(); child; child = children.next())
          if (child->kind() != scene::NODE_KIND_TEXT && child->kind() != scene::NODE_KIND_ATTRIBUTED_TEXT)
            return false;
        return true;
      }

    } // namespace layout
  } // namespace app
} // namespace loka
#endif
