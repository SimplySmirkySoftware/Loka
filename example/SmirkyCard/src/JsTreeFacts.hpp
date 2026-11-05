#ifndef SMIRKYCARD_JS_TREE_FACTS_HPP
#define SMIRKYCARD_JS_TREE_FACTS_HPP
#include "JsTreeDescription.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/ImageView.hpp"
#include "app/style/AttributedString.hpp"
#include "app/layout/LayoutGap.hpp"
namespace smirkycard
{
  enum JsTreeTextSource
  {
    TREE_LITERAL,
    TREE_LIVE_SEAT
  };
  /** Identified facts keep the sole app ID value; schemas mediate generic ID copying. */
  template <class T> struct JsIdentifiedFacts
  {
    loka::core::String testId;
    static const loka::core::String *readId(const void *p)
    {
      return &static_cast<const T *>(p)->testId;
    }
    static void writeId(void *p, const loka::core::String &id)
    {
      static_cast<T *>(p)->testId = id;
    }
  };
  /** Normalized VStack facts; JS edges live only in the schema slots. */
  struct VStackFacts : JsIdentifiedFacts<VStackFacts>
  {
    loka::app::ColumnUndeclaredHeight columnHeight;
    VStackFacts()
        : columnHeight(loka::app::COLUMN_UNDECLARED_HEIGHT_REMAINING)
    {
    }
    static const JsTreeSchemaFor<VStackFacts> schema;
  };
  /** Normalized Text facts; JS edges live only in the schema slots. */
  struct TextFacts : JsIdentifiedFacts<TextFacts>
  {
    JsTreeTextSource source;
    loka::core::String text;
    loka::app::TextStyle style;
    loka::app::BlockStyle block;
    TextFacts()
        : source(TREE_LITERAL)
    {
    }
    static const JsTreeSchemaFor<TextFacts> schema;
  };
  /** Normalized Edit facts; JS edges live only in the schema slots. */
  struct EditFacts : JsIdentifiedFacts<EditFacts>
  {

    static const JsTreeSchemaFor<EditFacts> schema;
  };
  /** Normalized Button facts; JS edges live only in the schema slots. */
  struct ButtonFacts : JsIdentifiedFacts<ButtonFacts>
  {
    JsTreeTextSource source;
    loka::core::String label;
    ButtonFacts()
        : source(TREE_LITERAL)
    {
    }
    static const JsTreeSchemaFor<ButtonFacts> schema;
  };
  /** Normalized Row facts; JS edges live only in the schema slots. */
  struct RowFacts : JsIdentifiedFacts<RowFacts>
  {
    loka::app::RowUndeclaredWidth rowWidth;
    RowFacts()
        : rowWidth(loka::app::ROW_UNDECLARED_WIDTH_SHARED)
    {
    }
    static const JsTreeSchemaFor<RowFacts> schema;
  };
  /** Normalized Markup facts; JS edges live only in the schema slots. */
  struct MarkupFacts : JsIdentifiedFacts<MarkupFacts>
  {
    loka::app::AttributedString text;
    loka::app::BlockStyle block;
    static const JsTreeSchemaFor<MarkupFacts> schema;
  };
  /** Normalized Cell facts; JS edges live only in the schema slots. */
  struct CellFacts : JsIdentifiedFacts<CellFacts>
  {
    JsTreeTextSource source;
    loka::core::String label;
    CellFacts()
        : source(TREE_LITERAL)
    {
    }
    static const JsTreeSchemaFor<CellFacts> schema;
  };
  /** Normalized Grid facts; JS edges live only in the schema slots. */
  struct GridFacts : JsIdentifiedFacts<GridFacts>
  {
    int rows;
    int cols;
    GridFacts()
        : rows(0),
          cols(0)
    {
    }
    static const JsTreeSchemaFor<GridFacts> schema;
  };
  /** Normalized Show facts; JS edges live only in the schema slots. */
  struct ShowFacts : JsIdentifiedFacts<ShowFacts>
  {

    static const JsTreeSchemaFor<ShowFacts> schema;
  };
  /** Normalized File facts; JS edges live only in the schema slots. */
  struct FileFacts : JsIdentifiedFacts<FileFacts>
  {

    static const JsTreeSchemaFor<FileFacts> schema;
  };
  /** Normalized Image facts; JS edges live only in the schema slots. */
  struct ImageFacts : JsIdentifiedFacts<ImageFacts>
  {
    int width;
    int height;
    bool hasFit;
    loka::app::ImageFit fit;
    ImageFacts()
        : width(0),
          height(0),
          hasFit(false),
          fit(loka::app::IMAGE_FIT_CONTAIN)
    {
    }
    static const JsTreeSchemaFor<ImageFacts> schema;
  };
  /** Normalized Scroll facts; JS edges live only in the schema slots. */
  struct ScrollFacts : JsIdentifiedFacts<ScrollFacts>
  {
    int insetTop;
    int insetBottom;
    ScrollFacts()
        : insetTop(0),
          insetBottom(0)
    {
    }
    static const JsTreeSchemaFor<ScrollFacts> schema;
  };
  /** Normalized Card facts; JS edges live only in the schema slots. */
  struct CardFacts : JsIdentifiedFacts<CardFacts>
  {

    static const JsTreeSchemaFor<CardFacts> schema;
  };
  /** Normalized ZStack facts; JS edges live only in the schema slots. */
  struct ZStackFacts : JsIdentifiedFacts<ZStackFacts>
  {
    loka::app::ZStackUnitGround unitGround;
    ZStackFacts()
        : unitGround(loka::app::ZSTACK_UNIT_GROUND_WINDOW)
    {
    }
    static const JsTreeSchemaFor<ZStackFacts> schema;
  };
  /** Normalized Box facts; JS edges live only in the schema slots. */
  struct BoxFacts : JsIdentifiedFacts<BoxFacts>
  {
    int width;
    int height;
    loka::app::layout::LayoutGap padding;
    BoxFacts()
        : width(0),
          height(0)
    {
    }
    static const JsTreeSchemaFor<BoxFacts> schema;
  };
  /** Normalized ClipBox facts; JS edges live only in the schema slots. */
  struct ClipBoxFacts : JsIdentifiedFacts<ClipBoxFacts>
  {
    int width;
    int height;
    loka::app::layout::LayoutGap padding;
    ClipBoxFacts()
        : width(0),
          height(0)
    {
    }
    static const JsTreeSchemaFor<ClipBoxFacts> schema;
  };
  /** Normalized Toolbar facts; JS edges live only in the schema slots. */
  struct ToolbarFacts : JsIdentifiedFacts<ToolbarFacts>
  {

    static const JsTreeSchemaFor<ToolbarFacts> schema;
  };
  /** Normalized ItemButton facts; JS edges live only in the schema slots. */
  struct ItemButtonFacts : JsIdentifiedFacts<ItemButtonFacts>
  {
    loka::core::String label;
    static const JsTreeSchemaFor<ItemButtonFacts> schema;
  };
  /** Normalized ItemLabel facts; JS edges live only in the schema slots. */
  struct ItemLabelFacts : JsIdentifiedFacts<ItemLabelFacts>
  {
    JsTreeTextSource source;
    loka::core::String label;
    loka::app::TextStyle style;
    ItemLabelFacts()
        : source(TREE_LITERAL)
    {
    }
    static const JsTreeSchemaFor<ItemLabelFacts> schema;
  };
  /** Normalized ItemFlex facts; JS edges live only in the schema slots. */
  struct ItemFlexFacts
  {

    static const JsTreeSchemaFor<ItemFlexFacts> schema;
  };
  /** Normalized ItemFixed facts; JS edges live only in the schema slots. */
  struct ItemFixedFacts
  {
    int width;
    ItemFixedFacts()
        : width(0)
    {
    }
    static const JsTreeSchemaFor<ItemFixedFacts> schema;
  };
} // namespace smirkycard
#endif
