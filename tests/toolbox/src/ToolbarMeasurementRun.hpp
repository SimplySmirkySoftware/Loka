#ifndef LOKA_TESTS_TOOLBAR_MEASUREMENT_RUN_HPP
#define LOKA_TESTS_TOOLBAR_MEASUREMENT_RUN_HPP
#include "ToolboxDrawMeasurement.hpp"
#include <cstdio>

namespace loka
{
  namespace testing
  {
    /** Fixture-owned bounded journal. An operation remains open until the next
        settled idle, so every full draw in its completion is attributed to it.
        operation_total_us sums erase-to-finish intervals, excluding layout. */
    class ToolbarMeasurementRun
    {
    public:
      enum
      {
        capacity = 128
      };
      ToolbarMeasurementRun()
          : operation_(0),
            used_(0),
            operations_(0),
            missing_(0),
            maximum_(0),
            overflow_(false)
      {
      }
      void begin(const char *name, long step, bool verdict = true)
      {
        this->close();
        this->operation_ = name;
        this->step_ = step;
        this->first_ = this->used_;
        this->expected_ = 0;
        this->verdict_ = verdict;
        if (verdict)
          ++this->operations_;
      }
      void expectDraw()
      {
        if (this->operation_)
          ++this->expected_;
      }
      void accept(const WholeDrawSample &sample)
      {
        if (!this->operation_)
          return;
        if (this->used_ == capacity)
        {
          this->overflow_ = true;
          return;
        }
        this->rows_[this->used_++] = Row(this->operation_, this->step_, sample, this->verdict_);
        if (!sample.complete)
          ++this->missing_;
        if (this->verdict_ && sample.paintUs > this->maximum_)
          this->maximum_ = sample.paintUs;
      }
      void close()
      {
        if (!this->operation_)
          return;
        const unsigned received = this->used_ - this->first_;
        if (this->expected_ > received)
          this->missing_ += this->expected_ - received;
        else if (!received)
          ++this->missing_;
        unsigned long total = 0;
        for (unsigned i = this->first_; i < this->used_; ++i)
          total += this->rows_[i].paint;
        for (unsigned i = this->first_; i < this->used_; ++i)
        {
          this->rows_[i].draws = this->used_ - this->first_;
          this->rows_[i].total = total;
        }
        this->operation_ = 0;
      }
      bool passed() const
      {
        return !this->operation_ && this->operations_ >= 10 && !this->missing_ && !this->overflow_
               && this->maximum_ <= 17000;
      }
      void write(std::FILE *out) const
      {
        unsigned count = 0;
        for (unsigned i = 0; i < this->used_; ++i)
        {
          const Row &r = this->rows_[i];
          if (r.verdict)
            ++count;
          std::fprintf(out,
                       "toolbar_draw operation=%s step=%ld erase_us=%lu layout_us=%lu draws=%u operation_total_us=%lu "
                       "verdict_sample=%d complete=%d\n",
                       r.operation,
                       r.step,
                       r.paint,
                       r.layout,
                       r.draws,
                       r.total,
                       r.verdict,
                       r.complete);
        }
        std::fprintf(
            out,
            "toolbar_summary verdict=%s max_us=%lu count=%u operations=%u missing=%u overflow=%d limit_us=17000\n",
            this->passed() ? "PASS" : "FAIL",
            this->maximum_,
            count,
            this->operations_,
            this->missing_,
            this->overflow_);
      }

    private:
      struct Row
      {
        const char *operation;
        long step;
        unsigned long paint, layout, total;
        unsigned draws;
        bool verdict, complete;
        Row()
            : operation(0),
              step(0),
              paint(0),
              layout(0),
              total(0),
              draws(0),
              verdict(false),
              complete(false)
        {
        }
        Row(const char *op, long s, const WholeDrawSample &sample, bool v)
            : operation(op),
              step(s),
              paint(sample.paintUs),
              layout(sample.layoutUs),
              total(0),
              draws(0),
              verdict(v),
              complete(sample.complete)
        {
        }
      };
      Row rows_[capacity];
      const char *operation_;
      long step_;
      unsigned expected_, first_, used_, operations_, missing_;
      unsigned long maximum_;
      bool overflow_, verdict_;
    };
  } // namespace testing
} // namespace loka
#endif
