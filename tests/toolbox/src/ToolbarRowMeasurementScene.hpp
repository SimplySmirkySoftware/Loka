#ifndef LOKA_TESTS_TOOLBAR_ROW_MEASUREMENT_SCENE_HPP
#define LOKA_TESTS_TOOLBAR_ROW_MEASUREMENT_SCENE_HPP
#include "app/nodes/boundary/StdComposition.hpp"
#include "ToolbarRowMeasurement.hpp"
#include "testing/scene/SceneTestFlow.hpp"
namespace loka
{
  namespace testing
  {
    inline app::RowDefinition ToolbarMeasuredRow(unsigned count)
    {
      const char *labels[] = {"I", "Small", "Medium title", "A very long title for the water filling measurement"};
      app::RowDefinition row = app::Row().naturalWidth();
      for (unsigned i = 0; i != count; ++i)
        row << (app::Text(labels[i]) + app::TextStyle(app::TextSizeValue::points(12)).family(app::TEXT_FAMILY_SYSTEM)
                + app::BlockStyle().truncation(app::TEXT_TRUNCATION_ELLIPSIS));
      return row;
    }
    class ToolbarRows : public app::scene::BoundaryNodeFor<ToolbarRows>
    {
    public:
      explicit ToolbarRows(const app::scene::BoundaryPropsFor<ToolbarRows> &props)
          : app::scene::BoundaryNodeFor<ToolbarRows>(props)
      {
      }
      virtual void composeNode(app::scene::NodeComposition &c)
      {
        c.declare(app::Column() << (app::Box().width(1) << ToolbarMeasuredRow(1).TEST_ID("k1"))
                                << (app::Box().width(20) << ToolbarMeasuredRow(2).TEST_ID("k2"))
                                << (app::Box().width(180) << ToolbarMeasuredRow(4).TEST_ID("k4"))
                                << (app::Box().width(600) << ToolbarMeasuredRow(4).TEST_ID("fit"))
                                << (app::Box().width(28) << ToolbarMeasuredRow(4).TEST_ID("floor")));
      }
    };
    /** Counting is a separate untimed probe; the timed source is the rail itself. */
    class ToolbarQueryCount : public app::layout::INaturalWidthSource
    {
      const app::layout::INaturalWidthSource &source_;
      mutable unsigned count_;

    public:
      explicit ToolbarQueryCount(const app::layout::INaturalWidthSource &source)
          : source_(source),
            count_(0)
      {
      }
      virtual bool queryNaturalWidth(app::scene::Node *node, short &width) const
      {
        ++this->count_;
        return this->source_.queryNaturalWidth(node, width);
      }
      unsigned count() const
      {
        return this->count_;
      }
    };
    /** Integer-branch calibration only: Chicago 12 labels do not naturally
        measure one pixel. Never use this synthetic source for native cost deltas. */
    class ToolbarUnitNaturalWidths : public app::layout::INaturalWidthSource
    {
    public:
      virtual bool queryNaturalWidth(app::scene::Node *node, short &width) const
      {
        if (!node->asTextNode())
          return false;
        width = 1;
        return true;
      }
    };
    template <class Clock>
    bool RunToolbarRows(app::scene::Scene &scene,
                        const app::layout::INaturalWidthSource &source,
                        Clock &clock,
                        std::FILE *out)
    {
      const char *names[] = {"k1", "k2", "k4", "fit", "floor"};
      unsigned long medians[5], maxima[5];
      for (unsigned i = 0; i != 5; ++i)
      {
        app::scene::Node *found = 0;
        dsl::FlowError error;
        if (dsl::testing::LookupNodeById<app::scene::Node>(&scene, names[i], found, error) != dsl::FLOW_STEP_SUCCEEDED)
          return false;
        app::StackNode *row = found->asStackNode();
        if (!row)
          return false;
        const unsigned k = static_cast<unsigned>(row->childrenCount());
        const int natural = MeasureToolbarRowPass(*row, 30000, source);
        const int gaps = (k - 1) * 8;
        const int offer = i == 3 ? natural : i == 4 ? gaps + k : gaps + (natural - gaps) * 3 / 4;
        ToolbarQueryCount probe(source);
        const int occupied = MeasureToolbarRowPass(*row, offer, probe);
        const unsigned scans = probe.count() / k - 2;
        if (occupied != offer || (i == 2 && scans < 2) || ((i == 0 || i == 3) && scans != 0))
          return false;
        std::fprintf(
            out,
            "toolbar_row_inputs case=%s k=%u natural=%d scans=%u clock=Microseconds font=Chicago-12 source=toolbox\n",
            names[i],
            k,
            natural,
            scans);
        const ToolbarRowTimes times = MeasureToolbarRow(*row, offer, source, clock, out, names[i]);
        medians[i] = times.median;
        maxima[i] = times.maximum;
        if (i == 4)
        {
          ToolbarUnitNaturalWidths units;
          ToolbarQueryCount unitProbe(units);
          if (MeasureToolbarRowPass(*row, 27, unitProbe) != 28 || unitProbe.count() != 8)
            return false;
          std::fprintf(
              out,
              "toolbar_row_inputs case=b-equals-s k=4 natural=28 scans=0 source=synthetic-unit clock=Microseconds\n");
          MeasureToolbarRow(*row, 27, units, clock, out, "b-equals-s");
        }
      }
      for (unsigned i = 1; i != 3; ++i)
        std::fprintf(out,
                     "toolbar_row_delta case=%s median_over_fit_us=%ld maximum_over_fit_us=%ld median_over_k1_us=%ld "
                     "maximum_over_k1_us=%ld\n",
                     names[i],
                     static_cast<long>(medians[i]) - static_cast<long>(medians[3]),
                     static_cast<long>(maxima[i]) - static_cast<long>(maxima[3]),
                     static_cast<long>(medians[i]) - static_cast<long>(medians[0]),
                     static_cast<long>(maxima[i]) - static_cast<long>(maxima[0]));
      return true;
    }
  } // namespace testing
} // namespace loka
#endif
