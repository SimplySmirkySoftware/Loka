#include "JsTreeFacts.hpp"
namespace smirkycard
{
  namespace
  {
    const JsTreeSlot childrenSlots[] = {{EDGE_CHILDREN, TREE_DRAWABLE}};
    const JsTreeSlot textSlots[] = {{EDGE_TEXT, TREE_DRAWABLE}};
    const JsTreeSlot seatSlots[] = {{EDGE_SEAT, TREE_DRAWABLE}};
    const JsTreeSlot buttonSlots[] = {
        {EDGE_LABEL, TREE_DRAWABLE}, {EDGE_ENABLED, TREE_DRAWABLE}, {EDGE_HANDLER, TREE_DRAWABLE}};
    const JsTreeSlot cellSlots[] = {{EDGE_LABEL, TREE_DRAWABLE}, {EDGE_HANDLER, TREE_DRAWABLE}};
    const JsTreeSlot showSlots[] = {{EDGE_SEAT, TREE_DRAWABLE}, {EDGE_CHILD, TREE_DRAWABLE}};
    const JsTreeSlot scrollSlots[] = {{EDGE_CHILD, TREE_DRAWABLE}, {EDGE_VIEWPORT, TREE_DRAWABLE}};
    const JsTreeSlot childSlots[] = {{EDGE_CHILD, TREE_DRAWABLE}};
    const JsTreeSlot itemsSlots[] = {{EDGE_CHILDREN, TREE_TOOLBAR_ITEM}};
    const JsTreeSlot itemButtonSlots[] = {{EDGE_HANDLER, TREE_DRAWABLE}, {EDGE_ENABLED, TREE_DRAWABLE}};
    const JsTreeSlot labelSlots[] = {{EDGE_LABEL, TREE_DRAWABLE}};
  } // namespace
  const JsTreeSchemaFor<VStackFacts> VStackFacts::schema(
      "VStack", TREE_DRAWABLE, 1, childrenSlots, TREE_VSTACK, &VStackFacts::readId, &VStackFacts::writeId);
  const JsTreeSchemaFor<TextFacts>
      TextFacts::schema("Text", TREE_DRAWABLE, 1, textSlots, TREE_TEXT, &TextFacts::readId, &TextFacts::writeId);
  const JsTreeSchemaFor<EditFacts>
      EditFacts::schema("EditText", TREE_DRAWABLE, 1, seatSlots, TREE_EDIT, &EditFacts::readId, &EditFacts::writeId);
  const JsTreeSchemaFor<ButtonFacts> ButtonFacts::schema(
      "Button", TREE_DRAWABLE, 3, buttonSlots, TREE_BUTTON, &ButtonFacts::readId, &ButtonFacts::writeId);
  const JsTreeSchemaFor<RowFacts>
      RowFacts::schema("Row", TREE_DRAWABLE, 1, childrenSlots, TREE_ROW, &RowFacts::readId, &RowFacts::writeId);
  const JsTreeSchemaFor<MarkupFacts>
      MarkupFacts::schema("Markup", TREE_DRAWABLE, 0, 0, TREE_MARKUP, &MarkupFacts::readId, &MarkupFacts::writeId);
  const JsTreeSchemaFor<CellFacts>
      CellFacts::schema("Cell", TREE_DRAWABLE, 2, cellSlots, TREE_CELL, &CellFacts::readId, &CellFacts::writeId);
  const JsTreeSchemaFor<GridFacts>
      GridFacts::schema("Grid", TREE_DRAWABLE, 1, childrenSlots, TREE_GRID, &GridFacts::readId, &GridFacts::writeId);
  const JsTreeSchemaFor<ShowFacts>
      ShowFacts::schema("Show", TREE_DRAWABLE, 2, showSlots, TREE_SHOW, &ShowFacts::readId, &ShowFacts::writeId);
  const JsTreeSchemaFor<FileFacts> FileFacts::schema(
      "OpenFileDialog", TREE_DRAWABLE, 1, seatSlots, TREE_FILE, &FileFacts::readId, &FileFacts::writeId);
  const JsTreeSchemaFor<ImageFacts> ImageFacts::schema(
      "ImageView", TREE_DRAWABLE, 1, seatSlots, TREE_IMAGE, &ImageFacts::readId, &ImageFacts::writeId);
  const JsTreeSchemaFor<ScrollFacts> ScrollFacts::schema(
      "ScrollView", TREE_DRAWABLE, 2, scrollSlots, TREE_SCROLL, &ScrollFacts::readId, &ScrollFacts::writeId);
  const JsTreeSchemaFor<CardFacts>
      CardFacts::schema("Card", TREE_DRAWABLE, 1, childSlots, TREE_CARD, &CardFacts::readId, &CardFacts::writeId);
  const JsTreeSchemaFor<ZStackFacts> ZStackFacts::schema(
      "ZStack", TREE_DRAWABLE, 1, childrenSlots, TREE_ZSTACK, &ZStackFacts::readId, &ZStackFacts::writeId);
  const JsTreeSchemaFor<BoxFacts>
      BoxFacts::schema("Box", TREE_DRAWABLE, 1, childSlots, TREE_BOX, &BoxFacts::readId, &BoxFacts::writeId);
  const JsTreeSchemaFor<ClipBoxFacts> ClipBoxFacts::schema(
      "ClipBox", TREE_DRAWABLE, 1, childSlots, TREE_CLIPBOX, &ClipBoxFacts::readId, &ClipBoxFacts::writeId);
  const JsTreeSchemaFor<ToolbarFacts> ToolbarFacts::schema(
      "Toolbar", TREE_DRAWABLE, 1, itemsSlots, TREE_TOOLBAR, &ToolbarFacts::readId, &ToolbarFacts::writeId);
  const JsTreeSchemaFor<ItemButtonFacts> ItemButtonFacts::schema("ToolbarItem",
                                                                 TREE_TOOLBAR_ITEM,
                                                                 2,
                                                                 itemButtonSlots,
                                                                 TREE_ITEM_BUTTON,
                                                                 &ItemButtonFacts::readId,
                                                                 &ItemButtonFacts::writeId);
  const JsTreeSchemaFor<ItemLabelFacts> ItemLabelFacts::schema(
      "label", TREE_TOOLBAR_ITEM, 1, labelSlots, TREE_ITEM_LABEL, &ItemLabelFacts::readId, &ItemLabelFacts::writeId);
  const JsTreeSchemaFor<ItemFlexFacts> ItemFlexFacts::schema("flexibleSpace", TREE_TOOLBAR_ITEM, 0, 0, TREE_ITEM_FLEX);
  const JsTreeSchemaFor<ItemFixedFacts> ItemFixedFacts::schema("fixedSpace", TREE_TOOLBAR_ITEM, 0, 0, TREE_ITEM_FIXED);
} // namespace smirkycard
