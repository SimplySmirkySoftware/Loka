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

      /** Measured, content-relative Column advances: each span starts at the
          previous child's bottom and includes the gap before its own child.
          Valid while child order, each child height, resolved gap and width
          remain unchanged since recording.
          Construction is pass-local;
          only finish() publishes a reusable table. No resident pointers are retained.
          The owner invalidates on constraints, structure, props, reattach or lost
          eligibility. Reuse also compares the resolved gap recorded by begin().
          The owner records the viewport only after successful placement. */
      class StackSpans
      {
      public:
        StackSpans()
            : gap_(0),
              filled_(0),
              completed_(false),
              placedViewport_(0, 0, 0, 0)
        {
        }

        bool begin(unsigned count, int gap)
        {
          this->invalidate();
          this->gap_ = static_cast<short>(gap);
          if (count == UINT_MAX || !this->ends_.allocate(static_cast<std::size_t>(count) + 1))
            return false;
          this->ends_[0] = 0;
          return true;
        }

        bool append(int start, int end)
        {
          if (!this->ends_.valid() || this->completed_ || this->filled_ + 1 >= this->ends_.size()
              || start != this->ends_[this->filled_] || end < start || end > SHRT_MAX)
          {
            this->invalidate();
            return false;
          }
          this->ends_[++this->filled_] = static_cast<short>(end);
          return true;
        }

        bool finish()
        {
          this->completed_ = this->ends_.valid() && this->filled_ + 1 == this->ends_.size();
          return this->completed_;
        }

        bool valid() const
        {
          return this->completed_;
        }
        bool validForGap(int gap) const
        {
          return this->valid() && this->gap_ == gap;
        }
        void invalidate()
        {
          this->ends_.clear();
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
          return this->ends_[index];
        }
        short end(unsigned index) const
        {
          assert(this->valid() && index < this->filled_);
          return this->ends_[index + 1];
        }
        short total() const
        {
          return this->valid() ? this->ends_[this->filled_] : 0;
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
            if (this->ends_[mid + 1] <= top)
              lo = mid + 1;
            else
              hi = mid;
          }
          const unsigned first = lo;
          hi = this->filled_;
          while (lo < hi)
          {
            const unsigned mid = lo + (hi - lo) / 2;
            if (this->ends_[mid] < bottom)
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
        detail::TextMeasureTable<short> ends_;
        short gap_;
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
