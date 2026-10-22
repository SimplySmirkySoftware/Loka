#ifndef LOKA_MARKDOWN_EDITOR_PREVIEW_SEAT_HPP
#define LOKA_MARKDOWN_EDITOR_PREVIEW_SEAT_HPP
#include "Markdown.hpp"
#include "app/nodes/AttributedText.hpp"
#include "app/nodes/controls/Card.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "app/nodes/nestable/Match.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ScrollView.hpp"
#include "app/nodes/Text.hpp"
#include "app/RectSurface.hpp"

namespace markdowneditor
{
  /** MainNode owns these slots; definitions borrow only read-only live inputs. */
  struct PreviewSeat
  {
    loka::app::scene::NodeState<BlockKind> kind;
    loka::app::scene::NodeState<loka::app::AttributedString> text;
    loka::app::scene::NodeState<loka::core::String> marker;
    loka::app::scene::DerivedNodeState<bool> visible;
  };
  class SeatVisible : public loka::core::DerivedState<bool>::EvalFn
  {
  public:
    explicit SeatVisible(const loka::app::scene::NodeState<BlockKind> &kind)
        : kind_(kind)
    {
    }
    virtual bool operator()()
    {
      return this->kind_.get() != UNUSED;
    }

  private:
    const loka::app::scene::NodeState<BlockKind> &kind_;
  };

  /** One declaration per fixed position; text changes never rebuild the arm. */
  class PreviewFactory
  {
  public:
    explicit PreviewFactory(loka::core::State<loka::app::RectSurfaceModel> *rule)
        : rule_(rule)
    {
    }
    loka::app::ShowDefinition operator()(const PreviewSeat &seat, std::size_t) const
    {
      using namespace loka::app;
      const AttributedTextDefinitionWithAttr text =
          AttributedText(seat.text.state()).TEST_ID("MarkdownEditor.Block") + BlockStyle().wrap(TEXT_WRAP_WORD);
      return Show(*seat.visible.state())
             << Match(*seat.kind.state())
                    .arm(CODE,
                         Card() << AttributedText(seat.text.state()).TEST_ID("MarkdownEditor.Block")
                                       + BlockStyle().wrap(TEXT_WRAP_NONE).truncation(TEXT_TRUNCATION_CLIP))
                    .arm(QUOTE, Row().gap(0) << Box().width(16) << text)
                    .arm(BULLET,
                         Row().gap(0) << Box().width(16) << (Box().width(24) << Text(seat.marker.state())) << text)
                    .arm(NUMBERED,
                         Row().gap(0) << Box().width(16) << (Box().width(24) << Text(seat.marker.state())) << text)
                    .arm(RULE, RectSurface(this->rule_).size(340, 1).TEST_ID("MarkdownEditor.Rule"))
                    .otherwise(text);
    }

  private:
    loka::core::State<loka::app::RectSurfaceModel> *rule_;
  };
} // namespace markdowneditor
#endif
