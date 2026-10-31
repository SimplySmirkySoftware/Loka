#ifndef LOKA_TESTS_TOOLBAR_ROW_MEASUREMENT_HPP
#define LOKA_TESTS_TOOLBAR_ROW_MEASUREMENT_HPP
#include "app/layout/LayoutHeuristics.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "ToolboxLayoutMetrics.hpp"
#include <algorithm>
#include <cstdio>

namespace loka
{
  namespace testing
  {
    /** A synchronous pass; node construction, painting and reporting are outside
        its clock bracket. The caller owns the Row and the measurement port. */
    inline int MeasureToolbarRowPass(app::StackNode &row,
                                     int offer,
                                     const app::layout::INaturalSizeSource &source,
                                     app::RowUndeclaredWidth policy = app::ROW_UNDECLARED_WIDTH_NATURAL)
    {
      app::layout::RowWidthConsultation widths(
          row.childrenHead(), row.childrenCount(), offer, 8, ToolboxLayoutMetrics::spacing(), policy, &source);
      dsl::CompositionCursor<app::scene::Node> seats(row.childrenHead(), row.childrenCount());
      int total = 0;
      for (app::scene::Node *seat = seats.next(); seat; seat = seats.next())
      {
        const app::layout::RowChildWidth width = widths.next(seat);
        total += width.width() + (width.hasGapBefore() ? 8 : 0);
      }
      return total;
    }

    /** Completed statistics for 31 separate one-pass samples. */
    struct ToolbarRowTimes
    {
      const unsigned long median, maximum;
      ToolbarRowTimes(unsigned long middle, unsigned long last)
          : median(middle),
            maximum(last)
      {
      }
    };
    template <class Clock>
    ToolbarRowTimes MeasureToolbarRow(app::StackNode &row,
                                      int offer,
                                      const app::layout::INaturalSizeSource &source,
                                      Clock &clock,
                                      std::FILE *out,
                                      const char *name,
                                      app::RowUndeclaredWidth policy = app::ROW_UNDECLARED_WIDTH_NATURAL)
    {
      unsigned long samples[31];
      int total = 0;
      for (unsigned i = 0; i != 31; ++i)
      {
        const unsigned long start = clock.now();
        total = MeasureToolbarRowPass(row, offer, source, policy);
        samples[i] = clock.now() - start;
      }
      std::sort(samples, samples + 31);
      std::fprintf(out,
                   "toolbar_row case=%s repetitions=31 median_us=%lu maximum_us=%lu offer=%d occupied=%d\n",
                   name,
                   samples[15],
                   samples[30],
                   offer,
                   total);
      return ToolbarRowTimes(samples[15], samples[30]);
    }
  } // namespace testing
} // namespace loka
#endif
