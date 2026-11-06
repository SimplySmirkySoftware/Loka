#ifndef LOKA_TEST_TOOLBOX_LEAF_PREFLIGHT_PINS_HPP
#define LOKA_TEST_TOOLBOX_LEAF_PREFLIGHT_PINS_HPP
#include "context/ToolboxPaintSupport.hpp"
#include "ToolboxPaintSuppressScope.hpp"

namespace
{
  void preflightDraw(ToolboxProjectedNodeContext &context,
                     ToolboxScenePlatformController &controller,
                     const std::string &id,
                     HostControl &control,
                     std::string &label)
  {
    if (id == "text-repaint")
      static_cast<ToolboxTextContext &>(context).repaint();
    else if (id == "button")
      static_cast<ToolboxButtonContext &>(context).repaint(&control, label);
    else if (id == "popup")
      static_cast<ToolboxPopupMenuContext &>(context).repaint();
    else if (id == "edit")
      static_cast<ToolboxEditTextContext &>(context).repaint(
          ToolboxEditPresentation(controller.editControls_[0].te, controller.editControls_[0].installed));
    else
      context.render(&controller);
  }

  void leafPreflightPins()
  {
    const char *ids[] = {"cell", "text", "attributed", "button", "popup", "edit"};
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
    {
      const std::string id(ids[i]);
      ToolboxWindow window;
      HostPortScope port(window.window());
      ToolboxScenePlatformController controller(&window);
      Scene scene((Boundary<PaintNode>(PaintProps())));
      scene.mount(&controller);
      typedef loka::dsl::testing::SceneTestAccess Access;
      Access::updateAttached(scene, true);
      Node *node = 0;
      loka::dsl::FlowError error;
      LOKA_VERIFY(loka::dsl::testing::LookupNodeById<Node>(&scene, id.c_str(), node, error));
      ToolboxProjectedNodeContext *context = 0;
      if (id == "cell")
        context = new ToolboxCellContext(node->asCellNode(), &controller);
      if (id == "text")
        context = new ToolboxTextContext(node->asTextNode(), &controller);
      if (id == "attributed")
        context = new ToolboxAttributedTextContext(node->asAttributedTextNode(), &controller);
      if (id == "button")
        context = new ToolboxButtonContext(node->asButtonNode(), &controller);
      if (id == "popup")
        context = new ToolboxPopupMenuContext(node->asPopupMenuNode(), &controller);
      if (id == "edit")
        context = new ToolboxEditTextContext(node->asEditTextNode(), &controller);
      node->setContext(context);
      LayoutState seat = Seat();
      context->layout(&controller, seat);
      HostControl control;
      control.owner = window.window();
      std::string label;
      const Rect broad = {0, 0, 300, 300}, miss = {200, 200, 210, 210};
      ClipRect(&broad);
      // Establish native acquisition and settled history before measuring leaf clips.
      context->render(&controller);
      preflightDraw(*context, controller, id, control, label);
      const PaintQuery query = {ToolboxPaintScope(), PLACEMENT_ELIGIBLE};
      ExactEmpty(*context, query);
      for (int hit = 0; hit < 2; ++hit)
      {
        ClipRect(hit ? &broad : &miss);
        controller.observation_.reset();
        const int before = toolbox_host::regions;
        const int hits = toolbox_host::textHits;
        const std::size_t controls = controller.editControls_.size();
        preflightDraw(*context, controller, id, control, label);
        LOKA_VERIFY(toolbox_host::regions - before == (hit ? 2 : 0));
        LOKA_VERIFY(controller.observation_.outsideRefusal().reason != TOOLBOX_PAINT_REFUSAL_CLIP_ALLOCATION);
        LOKA_VERIFY(EqualRect(&window.port.native.clip, hit ? &broad : &miss));
        ExactEmpty(*context, query);
        LOKA_VERIFY(controller.editControls_.size() == controls);
        if (id == "text")
          LOKA_VERIFY(toolbox_host::textHits == hits + 1);
      }
      RgnHandle scratch = NewRgn();
      {
        ToolboxPaintSuppressScope suppress(scratch);
        const int before = toolbox_host::regions;
        preflightDraw(*context, controller, id, control, label);
        LOKA_VERIFY(toolbox_host::regions == before);
        LOKA_VERIFY(EmptyRect(&window.port.native.clip));
        ExactEmpty(*context, query);
      }
      DisposeRgn(scratch);
      LOKA_VERIFY(EqualRect(&window.port.native.clip, &broad));
      if (id == "text" || id == "attributed")
      {
        // Repaint has no outer draw measure scope to mask a misplaced preflight.
        const std::string scopedId = id == "text" ? "text-repaint" : id;
        ToolboxWindow foreign;
        for (int hit = 0; hit < 2; ++hit)
        {
          SetPort(window.window());
          ClipRect(hit ? &broad : &miss);
          SetPort(foreign.window());
          ClipRect(hit ? &miss : &broad);
          const int before = toolbox_host::regions;
          preflightDraw(*context, controller, scopedId, control, label);
          LOKA_VERIFY(toolbox_host::regions - before == (hit ? 2 : 0));
          GrafPtr current;
          GetPort(&current);
          LOKA_VERIFY(current == foreign.window());
          LOKA_VERIFY(EqualRect(&window.port.native.clip, hit ? &broad : &miss));
          LOKA_VERIFY(EqualRect(&foreign.port.native.clip, hit ? &miss : &broad));
          ExactEmpty(*context, query);
        }
        SetPort(window.window());
        ClipRect(&broad);
        SetPort(foreign.window());
        scratch = NewRgn();
        {
          ToolboxPaintSuppressScope suppress(scratch);
          const int before = toolbox_host::regions;
          preflightDraw(*context, controller, scopedId, control, label);
          LOKA_VERIFY(toolbox_host::regions - before == 2);
          LOKA_VERIFY(EmptyRect(&foreign.port.native.clip));
        }
        DisposeRgn(scratch);
        SetPort(window.window());
      }
      // Both exact and conservative queries must leave the hole unpainted.
      if (id == "cell")
      {
        RgnHandle ring = NewRgn(), hole = NewRgn();
        RectRgn(ring, &broad);
        const Rect cutout = {1, 1, 150, 150};
        RectRgn(hole, &cutout);
        DiffRgn(ring, hole, ring);
        SetClip(ring);
        for (int conservative = 0; conservative < 2; ++conservative)
        {
          toolbox_host::conservativeRectQuery = conservative != 0;
          const int before = toolbox_host::regions;
          const std::size_t draws = toolbox_host::pascalDraws.size();
          preflightDraw(*context, controller, id, control, label);
          LOKA_VERIFY(toolbox_host::regions - before == (conservative ? 2 : 0));
          LOKA_VERIFY(toolbox_host::pascalDraws.size() == draws);
          LOKA_VERIFY(EqualRgn(window.port.clipRgn, ring));
          ExactEmpty(*context, query);
        }
        toolbox_host::conservativeRectQuery = false;
        toolbox_host::failRegions = 1;
        const int before = toolbox_host::regions;
        preflightDraw(*context, controller, id, control, label);
        LOKA_VERIFY(toolbox_host::regions == before && toolbox_host::failRegions == 1);
        toolbox_host::failRegions = 0;
        DisposeRgn(hole);
        DisposeRgn(ring);
      }
      Access::unmount(scene);
      std::printf("clip preflight %s pair/history/suppression pins passed\n", id.c_str());
    }
    toolbox_host::reset();
  }
} // namespace
#endif
