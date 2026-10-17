/** Private collector body shared by the rail and the host fixture.
    Included inside the caller's anonymous namespace. */
  /** Only these built-in drawers are cast. Registration protects the same
      type keys, including in release builds, as on the Win32 rail. */
  bool IsToolboxPaintDrawerType(const void *key)
  {
    return key == loka::app::scene::NodeTypeToken<loka::app::AttributedTextNode>()
           || key == loka::app::scene::NodeTypeToken<loka::app::RectSurfaceNode>()
           || key == loka::app::scene::NodeTypeToken<loka::app::ButtonNode>()
           || key == loka::app::scene::NodeTypeToken<loka::app::CellNode>()
           || key == loka::app::scene::NodeTypeToken<loka::app::RibbonGroupEdgeNode>()
           || key == loka::app::scene::NodeTypeToken<loka::app::CardBandNode>()
           || key == loka::app::scene::NodeTypeToken<loka::app::TextNode>()
           || key == loka::app::scene::NodeTypeToken<loka::app::TextEditorNode>()
           || key == loka::app::scene::NodeTypeToken<loka::app::EditTextNode>()
           || key == loka::app::scene::NodeTypeToken<loka::app::PopupMenuNode>();
  }

  struct ToolboxPaintAnswerSource
  {
    /** Exact damage may share a conservative, non-erasing destination only when true. */
    enum { kMergesExactDamage = 1 };
    explicit ToolboxPaintAnswerSource(ToolboxSceneDebugStats &stats) : stats_(stats) {}

    bool queryPaintAnswer(loka::app::scene::Node *node,
                          loka::app::scene::NodeContext *context,
                          const loka::app::scene::PaintQuery &query,
                          loka::app::scene::PaintAnswer &answer)
    {
      using namespace loka::app::scene;
      this->stats_.noteCollectorVisit();
      if (IsToolboxPaintDrawerType(node->nodeTypeKey()))
      {
        answer = context ? static_cast<NativeNodeContext *>(context)->queryPaintDamage(query)
                         : PaintAnswer::refused(PAINT_REFUSED_NO_CONTEXT);
        return true;
      }
      switch (node->kind())
      {
      case NODE_KIND_OPEN_FILE_DIALOG:
      case NODE_KIND_CONFIRM_DIALOG:
      case NODE_KIND_SCROLL_VIEW:
      case NODE_KIND_BOX:
      case NODE_KIND_CARD:
      case NODE_KIND_RIBBON_GROUP:
      case NODE_KIND_ZSTACK:
      case NODE_KIND_LAYER:
      case NODE_KIND_GRID:
      case NODE_KIND_STACK:
      case NODE_KIND_CANVAS:
        return false;
      case NODE_KIND_ATTRIBUTED_TEXT:
      case NODE_KIND_TEXT_EDITOR:
        break;
      case NODE_KIND_UNKNOWN:
        if (!node->asProjectedLayoutNode())
          return false;
        break;
      case NODE_KIND_RECT_SURFACE:
      case NODE_KIND_TEXT:
      case NODE_KIND_BUTTON:
      case NODE_KIND_POPUP_MENU:
      case NODE_KIND_SCROLL_BAR:
      case NODE_KIND_EDIT_TEXT:
      case NODE_KIND_CELL:
      case NODE_KIND_CARD_BAND:
      case NODE_KIND_RIBBON_GROUP_EDGE:
      case NODE_KIND_IMAGE_VIEW:
        break;
      }
      // A ledger refresh alone is not a completed paint submission. Drawers
      // without their own exact answer preserve widening.
      answer = PaintAnswer::refused(context ? PAINT_REFUSED_UNSUPPORTED_KIND : PAINT_REFUSED_NO_CONTEXT);
      return true;
    }

    ToolboxSceneDebugStats &stats_;
  };
