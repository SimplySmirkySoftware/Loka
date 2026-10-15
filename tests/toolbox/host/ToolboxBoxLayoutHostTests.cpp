#include "app/nodes/controls/Card.hpp"
#include "ToolboxNodeDispatch.hpp"
#include "ToolboxPlatformLayoutHandlers.hpp"
#include "app/nodes/controls/Ribbon.hpp"
#include "app/nodes/nestable/Box.hpp"
#include "app/nodes/nestable/Fragment.hpp"
#include "app/ConfirmDialog.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "app/nodes/nestable/Grid.hpp"
#include "app/nodes/nestable/ZStack.hpp"
#include "app/nodes/ImageView.hpp"
#include <cstdio>
#include <cstring>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  class Probe : public Node, public IProjectedLayoutNode
  {
  public:
    LayoutState offer;
    unsigned calls;
    const short advance;
    explicit Probe(short height = 7) : calls(0), advance(height) {}
    virtual IProjectedLayoutNode *asProjectedLayoutNode() { return this; }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      ++this->calls;
      this->offer = state;
      state.y = static_cast<short>(state.y + this->advance);
      return 23;
    }
  };

  void framedRoutingPin(ToolboxScenePlatformController &controller, bool handler)
  {
    RibbonGroupNode frame((RibbonGroupProps()));
    Probe *content = new Probe(7);
    frame.addChild(content);
    LOKA_VERIFY((controller.registry.find(&frame) != 0) == handler);
    LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 100;
    state.height = 80;
    LOKA_VERIFY(LayoutNode(&frame, state, &controller, 0) == 100);
    LOKA_VERIFY(state.y == 35);
    LOKA_VERIFY(content->calls == 1);
    LOKA_VERIFY(content->offer.x == 14 && content->offer.y == 24);
    LOKA_VERIFY(content->offer.width == 92 && content->offer.height == 72);
  }

  void cardRoutingPin(ToolboxScenePlatformController &controller, bool handler)
  {
    CardNode card((CardProps()));
    Probe *content = new Probe(1);
    card.addChild(content);
    for (int role = 0; role < CARD_BAND_COUNT; ++role)
      card.addChild(new CardBandNode(CardBandProps(static_cast<CardBandRole>(role))));
    LOKA_VERIFY((controller.registry.find(&card) != 0) == handler);
    LayoutState state;
    state.x = 10;
    state.y = 20;
    state.width = 100;
    state.height = 80;
    LOKA_VERIFY(LayoutNode(&card, state, &controller, 0) == 100);
    LOKA_VERIFY(state.y == 35 && content->calls == 1);
    LOKA_VERIFY(content->offer.x == 13 && content->offer.y == 23);
    LOKA_VERIFY(content->offer.width == 91 && content->offer.height == 71);
  }

  void cardScrollRangePin(ToolboxScenePlatformController &controller)
  {
    StackNode column((StackProps(STACK_AXIS_COLUMN)));
    CardNode *card = new CardNode(CardProps());
    Probe *content = new Probe(0);
    Probe *sibling = new Probe(0);
    card->addChild(content);
    column.addChild(card);
    column.addChild(sibling);
    ProjectionParentScope scope;
    LOKA_VERIFY(
        controller.projectionParentScopes_.current().deriveScrolled(0, 0, 0, loka::core::Frame(0, 0, 80, 80), scope));
    ProjectionParentScopeGuard guard(controller.projectionParentScopes_, scope);
    LOKA_VERIFY(guard.isActive());
    LayoutState state;
    state.width = 80;
    state.height = 80;
    state.y = SHRT_MAX - 12;
    LayoutNode(&column, state, &controller, 0);
    LOKA_VERIFY(content->calls == 1 && content->offer.y == SHRT_MAX - 9);
    LOKA_VERIFY(controller.projectionParentScopes_.current().hasShortRangeRefusal());
    LOKA_VERIFY(sibling->calls == 0 && state.y >= 0);
  }

  class DirectStack : public StackNode
  {
  public:
    explicit DirectStack(const StackProps &props) : StackNode(props) {}
    virtual const void *nodeTypeKey() const { return NodeTypeToken<DirectStack>(); }
  };

  class TextProbe : public TextNode
  {
  public:
    LayoutState offer;
    const short advance;
    explicit TextProbe(short height)
        : TextNode(TextProps("text")),
          advance(height)
    {
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      this->offer = state;
      state.y = static_cast<short>(state.y + this->advance);
      return 23;
    }
  };

  class VisitedDialog : public ConfirmDialogNode
  {
  public:
    unsigned calls;
    explicit VisitedDialog(const NodeState<ConfirmResult> &result)
        : ConfirmDialogNode(ConfirmDialogProps().result(result)),
          calls(0)
    {
    }
    virtual short layoutProjected(IPlatformController *, LayoutState &)
    {
      ++this->calls;
      return 0;
    }
  };

  class VisitedFragment : public FragmentNode
  {
  public:
    unsigned dispatchCalls;
    VisitedFragment()
        : FragmentNode(FragmentProps()),
          dispatchCalls(0)
    {
    }
    // LayoutNode's first question; the seat verdict reads kind() and never asks it.
    virtual BoundaryNode *asBoundary()
    {
      ++this->dispatchCalls;
      return FragmentNode::asBoundary();
    }
  };

  void passThroughGapPin()
  {
    ToolboxScenePlatformController controller;
    LOKA_VERIFY(RegisterToolboxPlatformLayoutHandlers(controller.registry));
    loka::core::MutableState<ConfirmResult> storage;
    NodeState<ConfirmResult> result(&storage, 0);
    StackNode column((StackProps(STACK_AXIS_COLUMN)));
    FragmentNode *fragment = new FragmentNode(FragmentProps());
    TextProbe *first = new TextProbe(5), *last = new TextProbe(7);
    VisitedDialog *dialog = new VisitedDialog(result);
    fragment->addChild(first);
    VisitedFragment *empty = new VisitedFragment();
    fragment->addChild(empty);
    fragment->addChild(dialog);
    fragment->addChild(last);
    column.addChild(fragment);
    LayoutState state;
    state.y = 20;
    state.width = 100;
    state.spacing = 9;
    LOKA_VERIFY(LayoutNode(&column, state, &controller, 0) == 23);
    LOKA_VERIFY(first->offer.y == 20 && last->offer.y == 34 && state.y == 41);
    LOKA_VERIFY(dialog->calls == 1 && empty->dispatchCalls == 1);
  }

  void containerGapPin(ToolboxScenePlatformController &controller)
  {
    for (unsigned kind = 0; kind != 3; ++kind)
    {
      NestableNode *container = kind == 0   ? static_cast<NestableNode *>(new StackNode(StackProps(STACK_AXIS_COLUMN)))
                                : kind == 1 ? static_cast<NestableNode *>(new BoxNode(BoxProps()))
                                            : static_cast<NestableNode *>(new FragmentNode(FragmentProps()));
      Probe *first = new Probe(5), *last = new Probe(7);
      container->addChild(first);
      container->addChild(new FragmentNode(FragmentProps()));
      container->addChild(last);
      LayoutState state;
      state.y = 20;
      state.width = 100;
      state.spacing = 9;
      LOKA_VERIFY(LayoutNode(container, state, &controller, 0) == 23);
      LOKA_VERIFY(first->calls == 1 && last->calls == 1);
      LOKA_VERIFY(first->offer.y == 20 && last->offer.y == 34 && state.y == 41);
      delete container;

      container = kind == 0   ? static_cast<NestableNode *>(new StackNode(StackProps(STACK_AXIS_COLUMN)))
                  : kind == 1 ? static_cast<NestableNode *>(new BoxNode(BoxProps()))
                              : static_cast<NestableNode *>(new FragmentNode(FragmentProps()));
      BoxProps large;
      large.setSize(10, 32760);
      container->addChild(new BoxNode(large));
      last = new Probe(1);
      container->addChild(last);
      ProjectionParentScope scope;
      LOKA_VERIFY(controller.projectionParentScopes_.current().deriveScrolled(
          0, 0, 0, loka::core::Frame(0, 0, 100, 100), scope));
      {
        ProjectionParentScopeGuard guard(controller.projectionParentScopes_, scope);
        state.y = 0;
        state.spacing = 12;
        LayoutNode(container, state, &controller, 0);
        LOKA_VERIFY(controller.projectionParentScopes_.current().hasShortRangeRefusal());
        LOKA_VERIFY(last->calls == 0);
      }
      delete container;
    }
  }

  void derivedStackPin(ToolboxScenePlatformController &controller, bool row)
  {
    StackProps props(row ? STACK_AXIS_ROW : STACK_AXIS_COLUMN);
    props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
    StackNode plain(props);
    DirectStack derived(props);
    LOKA_VERIFY(controller.registry.find(&plain) != 0);
    LOKA_VERIFY(controller.registry.find(&derived) == 0);
    Probe *expected[2] = {new Probe(7), new Probe(11)};
    Probe *actual[2] = {new Probe(7), new Probe(11)};
    for (int i = 0; i != 2; ++i)
    {
      plain.addChild(expected[i]);
      derived.addChild(actual[i]);
    }
    LayoutState baseline;
    baseline.x = 10; baseline.y = 20; baseline.width = 240;
    baseline.height = 80; baseline.spacing = 6;
    LayoutState state = baseline;
    const short expectedWidth = LayoutNode(&plain, baseline, &controller, 0);
    const short actualWidth = LayoutNode(&derived, state, &controller, 0);
    std::printf("derived-%s: plain calls=%u/%u width=%d Y=%d; derived calls=%u/%u width=%d Y=%d\n",
                row ? "row" : "column", expected[0]->calls, expected[1]->calls,
                expectedWidth, baseline.y, actual[0]->calls, actual[1]->calls, actualWidth, state.y);
    std::fflush(stdout);
    for (int i = 0; i != 2; ++i)
    {
      LOKA_VERIFY(expected[i]->calls == 1);
      LOKA_VERIFY(actual[i]->calls == expected[i]->calls);
      LOKA_VERIFY(actual[i]->offer.x == expected[i]->offer.x);
      LOKA_VERIFY(actual[i]->offer.y == expected[i]->offer.y);
      LOKA_VERIFY(actual[i]->offer.width == expected[i]->offer.width);
      LOKA_VERIFY(actual[i]->offer.height == expected[i]->offer.height);
    }
    LOKA_VERIFY(actualWidth == expectedWidth);
    LOKA_VERIFY(state.y == baseline.y);
  }

  void zStackPin(ToolboxScenePlatformController &controller, bool handler)
  {
    ZStackNode stack((ZStackProps()));
    Probe *a = new Probe(7);
    Probe *b = new Probe(11);
    stack.addChild(a);
    stack.addChild(b);
    LOKA_VERIFY((controller.registry.find(&stack) != 0) == handler);
    LayoutState state;
    state.x = 10; state.y = 20; state.width = 80; state.height = 60;
    const short width = LayoutNode(&stack, state, &controller, 0);
    LOKA_VERIFY(a->calls == 1 && b->calls == 1);
    LOKA_VERIFY(a->offer.x == 10 && b->offer.x == 10);
    LOKA_VERIFY(a->offer.y == 20 && b->offer.y == 20);
    LOKA_VERIFY(a->offer.width == 80 && b->offer.width == 80);
    LOKA_VERIFY(a->offer.height == 60 && b->offer.height == 60);
    LOKA_VERIFY(width == 23 && state.y == 31);
  }

  void scrollRangePin(ToolboxScenePlatformController &controller, bool handler, bool refuses)
  {
    StackNode column((StackProps(STACK_AXIS_COLUMN)));
    BoxNode *box = new BoxNode(BoxProps().setPadding(10));
    Probe *child = new Probe(0);
    Probe *sibling = new Probe(0);
    box->addChild(child);
    column.addChild(box);
    column.addChild(sibling);
    LOKA_VERIFY((controller.registry.find(box) != 0) == handler);
    ProjectionParentScope scrollScope;
    LOKA_VERIFY(controller.projectionParentScopes_.current().deriveScrolled(
        0, 0, 0, loka::core::Frame(0, 0, 80, 80), scrollScope));
    ProjectionParentScopeGuard guard(controller.projectionParentScopes_, scrollScope);
    LOKA_VERIFY(guard.isActive());
    LayoutState state;
    state.width = 80; state.height = 80;
    // Child Y remains representable; only the Box's bottom padding overflows.
    state.y = static_cast<short>(SHRT_MAX - (refuses ? 15 : 20));
    LayoutNode(&column, state, &controller, 0);
    const bool refused = controller.projectionParentScopes_.current().hasShortRangeRefusal();
    std::printf("scroll range: refused=%d child calls=%u sibling calls=%u sibling Y=%d\n",
                refused, child->calls, sibling->calls, sibling->offer.y);
    std::fflush(stdout);
    LOKA_VERIFY(child->calls == 1);
    LOKA_VERIFY(child->offer.y == SHRT_MAX - (refuses ? 5 : 10));
    LOKA_VERIFY(refused == refuses);
    LOKA_VERIFY(sibling->calls == (refuses ? 0u : 1u));
    LOKA_VERIFY(state.y >= 0);
    if (!refuses) LOKA_VERIFY(sibling->offer.y == SHRT_MAX);
  }

  class HeightProbe : public ImageViewNode
  {
  public:
    unsigned calls;
    LayoutState offer;
    explicit HeightProbe(int height) : ImageViewNode(ImageViewProps().size(10, height)), calls(0) {}
    virtual IProjectedLayoutNode *asProjectedLayoutNode() { return this; }
    virtual short layoutProjected(IPlatformController *, LayoutState &state)
    {
      ++this->calls;
      this->offer = state;
      return 10;
    }
  };

  void offsetRangePin(ToolboxScenePlatformController &controller, bool refuses)
  {
    StackNode column((StackProps(STACK_AXIS_COLUMN)));
    StackNode *row = new StackNode(StackProps(STACK_AXIS_ROW).alignVertical(VERTICAL_ALIGNMENT_BOTTOM));
    HeightProbe *child = new HeightProbe(1);
    row->addChild(child);
    row->addChild(new HeightProbe(20));
    column.addChild(row);
    Probe *sibling = new Probe(0);
    column.addChild(sibling);
    ProjectionParentScope scope;
    LOKA_VERIFY(controller.projectionParentScopes_.current().deriveScrolled(
        0, 0, 0, loka::core::Frame(0, 0, 80, 80), scope));
    ProjectionParentScopeGuard guard(controller.projectionParentScopes_, scope);
    LOKA_VERIFY(guard.isActive());
    LayoutState state;
    state.width = 80;
    state.spacing = 0;
    state.y = static_cast<short>(SHRT_MAX - (refuses ? 18 : 20));
    LayoutNode(&column, state, &controller, 0);
    std::printf("row offset: child calls=%u child Y=%d sibling calls=%u\n",
                child->calls, child->offer.y, sibling->calls);
    std::fflush(stdout);
    LOKA_VERIFY(controller.projectionParentScopes_.current().hasShortRangeRefusal() == refuses);
    LOKA_VERIFY(child->calls == (refuses ? 0u : 1u));
    LOKA_VERIFY(sibling->calls == (refuses ? 0u : 1u));
    if (!refuses) LOKA_VERIFY(sibling->offer.y == SHRT_MAX);
  }

  void handlerRangePin(ToolboxScenePlatformController &controller, bool handler,
                       const char *kind, bool refuses)
  {
    StackNode column((StackProps(STACK_AXIS_COLUMN)));
    const bool row = std::strncmp(kind, "row", 3) == 0;
    const bool largeAdvance = std::strcmp(kind, "row-advance") == 0;
    NestableNode *subject = row
        ? static_cast<NestableNode *>(new StackNode(
            StackProps(STACK_AXIS_ROW).alignVertical(VERTICAL_ALIGNMENT_TOP)))
        : static_cast<NestableNode *>(new GridNode(GridProps(2, 1)));
    Probe *child = new Probe(0);
    Probe *sibling = new Probe(0);
    subject->addChild(child);
    column.addChild(subject);
    column.addChild(sibling);
    LOKA_VERIFY((controller.registry.find(subject) != 0) == handler);
    ProjectionParentScope scope;
    LOKA_VERIFY(controller.projectionParentScopes_.current().deriveScrolled(
        0, 0, 0, loka::core::Frame(0, 0, 80, 80), scope));
    ProjectionParentScopeGuard guard(controller.projectionParentScopes_, scope);
    LOKA_VERIFY(guard.isActive());
    LayoutState state;
    state.width = 80; state.height = 20; state.lineHeight = 20; state.spacing = 5;
    if (largeAdvance) { state.lineHeight = 30000; state.spacing = 10000; }
    const int advance = largeAdvance ? 40000 : (!row && refuses ? 20 : 25);
    state.y = static_cast<short>(SHRT_MAX - advance + (refuses ? 1 : 0));
    LayoutNode(&column, state, &controller, 0);
    const bool refused = controller.projectionParentScopes_.current().hasShortRangeRefusal();
    std::printf("%s range: refused=%d child calls=%u sibling calls=%u sibling Y=%d\n",
                kind, refused, child->calls, sibling->calls, sibling->offer.y);
    std::fflush(stdout);
    LOKA_VERIFY(refused == refuses);
    LOKA_VERIFY(sibling->calls == (refuses ? 0u : 1u));
    if (!largeAdvance) LOKA_VERIFY(state.y >= 0);
    if (!row)
      LOKA_VERIFY(child->calls == (refuses ? 0u : 1u));
    if (!refuses) LOKA_VERIFY(sibling->offer.y == SHRT_MAX);
  }

  void bandRangePin(ToolboxScenePlatformController &controller, bool refuses)
  {
    // Empty selection still reports the cached full extent. No child can cause
    // this refusal: the Column adds the retained total to its new origin.
    StackNode column((StackProps(STACK_AXIS_COLUMN)));
    loka::app::layout::StackSpans spans;
    // A text-kind probe preserves the production band eligibility rule.
    class TextProbe : public Probe
    {
    public:
      TextProbe() : Probe(0) {}
      virtual NodeKind kind() const { return NODE_KIND_TEXT; }
    };
    column.addChild(new TextProbe());
    LOKA_VERIFY(spans.begin(1));
    LOKA_VERIFY(spans.append(0, 20));
    LOKA_VERIFY(spans.finish());
    const loka::app::layout::LazyWindow range = {0, 0};
    ProjectionParentScope scope;
    LOKA_VERIFY(controller.projectionParentScopes_.current().deriveScrolled(
        0, 0, 0, loka::core::Frame(0, 0, 80, 80), scope));
    ProjectionParentScopeGuard guard(controller.projectionParentScopes_, scope);
    LOKA_VERIFY(guard.isActive());
    LayoutState state;
    state.y = static_cast<short>(SHRT_MAX - 20 + (refuses ? 1 : 0));
    LayoutNode(&column, state, &controller, 0, &range, &spans);
    Probe sibling(0);
    LayoutNode(&sibling, state, &controller, 0);
    const bool refused = controller.projectionParentScopes_.current().hasShortRangeRefusal();
    std::printf("band range: refused=%d sibling calls=%u Y=%d\n", refused, sibling.calls, state.y);
    std::fflush(stdout);
    LOKA_VERIFY(refused == refuses);
    LOKA_VERIFY(sibling.calls == (refuses ? 0u : 1u));
    LOKA_VERIFY(state.y >= 0);
    if (!refuses) LOKA_VERIFY(state.y == SHRT_MAX);
  }

}
// A null controller is a contract violation at both layout entries (#562):
// there is no registry to reach the single layout landing through.
static void nullControllerPin()
{
#if !defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
  for (int entry = 0; entry != 2; ++entry)
  {
    const pid_t child = fork();
    LOKA_VERIFY(child >= 0);
    if (!child)
    {
      StackNode column((StackProps(STACK_AXIS_COLUMN)));
      LayoutState state;
      if (entry == 0) LayoutNode(&column, state, 0, 0);
      else LayoutChildren(&column, state, 0, 0);
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(child, &status, 0) == child);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
  }
#else
  std::printf("[skip] null-controller layout pin requires a debug build without ASan.\n");
#endif
}

int main(int argc, char **argv)
{
  LOKA_VERIFY(argc == 3 || argc == 4);
  if (std::strcmp(argv[1], "null-controller") == 0)
  {
    nullControllerPin();
    return 0;
  }
  LOKA_VERIFY(std::strcmp(argv[1], "handler") == 0 || std::strcmp(argv[1], "fallback") == 0);
  ToolboxScenePlatformController controller;
  const bool handler = std::strcmp(argv[1], "handler") == 0;
  if (handler)
  {
    const bool registered = RegisterToolboxPlatformLayoutHandlers(controller.registry);
    LOKA_VERIFY(registered);
  }
  passThroughGapPin();
  containerGapPin(controller);
  const char *mode = argv[2];
  if (std::strcmp(mode, "normal") == 0)
  {
    framedRoutingPin(controller, handler);
    cardRoutingPin(controller, handler);
    cardScrollRangePin(controller);
  }
  if (std::strcmp(mode, "derived-column") == 0 || std::strcmp(mode, "derived-row") == 0)
  {
    derivedStackPin(controller, std::strcmp(mode, "derived-row") == 0);
    return 0;
  }
  if (std::strcmp(mode, "zstack") == 0)
  {
    zStackPin(controller, handler);
    return 0;
  }
  if (argc == 4)
  {
    const bool refuses = std::strcmp(argv[3], "scroll-refusal") == 0;
    if (std::strcmp(mode, "band") == 0) bandRangePin(controller, refuses);
    else if (std::strcmp(mode, "row-offset") == 0) offsetRangePin(controller, refuses);
    else handlerRangePin(controller, handler, mode, refuses);
    return 0;
  }
  if (std::strcmp(mode, "scroll-refusal") == 0 || std::strcmp(mode, "scroll-limit") == 0)
  {
    scrollRangePin(controller, handler, std::strcmp(mode, "scroll-refusal") == 0);
    return 0;
  }
  if (std::strcmp(mode, "natural-row") == 0)
  {
    StackProps props;
    props.rowUndeclaredWidth_ = ROW_UNDECLARED_WIDTH_NATURAL;
    StackNode row(props);
    Probe *a = new Probe();
    Probe *b = new Probe();
    row.addChild(a);
    row.addChild(b);
    LOKA_VERIFY((controller.registry.find(&row) != 0) == handler);
    LayoutState state;
    state.width = 240;
    state.spacing = 6;
    LayoutNode(&row, state, &controller, 0);
    LOKA_VERIFY(a->offer.width == 37 && b->offer.width == 37);
    LOKA_VERIFY(b->offer.x == a->offer.x + 37 + 6);
    return 0;
  }
  const bool nested = std::strcmp(mode, "nested") == 0;
  const bool fixed = std::strcmp(mode, "fixed") == 0;
  const bool empty = std::strcmp(mode, "empty") == 0;
  const short extent = std::strcmp(mode, "small") == 0 ? 6 :
      std::strcmp(mode, "equal") == 0 ? 10 :
      std::strcmp(mode, "zero") == 0 ? 0 :
      std::strcmp(mode, "negative") == 0 ? -3 : 80;
  StackNode column((StackProps(STACK_AXIS_COLUMN)));
  BoxProps props;
  props.setPadding(5);
  if (fixed) props.setSize(100, 60);
  BoxNode *box = new BoxNode(props);
  column.addChild(box);
  // Nested Boxes use the same handler/fallback path as their enclosing Column.
  LOKA_VERIFY((controller.registry.find(box) != 0) == handler);
  Probe *child = 0;
  if (!empty)
  {
    child = new Probe();
    if (nested)
    {
      BoxNode *inner = new BoxNode(props);
      box->addChild(inner);
      inner->addChild(child);
    }
    else box->addChild(child);
  }
  Probe *sibling = new Probe();
  column.addChild(sibling);
  LayoutState state;
  state.x = 10; state.y = 20; state.width = extent; state.height = extent;
  const short width = LayoutNode(&column, state, &controller, 0);
  const short bottom = fixed ? 80 : empty ? 30 : nested ? 47 : 37;
  if (child)
  {
    const short inset = nested ? 10 : 5;
    LOKA_VERIFY(child->offer.x == 10 + inset);
    LOKA_VERIFY(child->offer.y == 20 + inset);
    LOKA_VERIFY(child->offer.width == (fixed ? 90 : extent > 2 * inset ? extent - 2 * inset : 0));
    LOKA_VERIFY(child->offer.height == (fixed ? 50 : extent > 2 * inset ? extent - 2 * inset : 0));
  }
  LOKA_VERIFY(width == (fixed ? 100 : empty ? 23 : nested ? 43 : 33));
  LOKA_VERIFY(sibling->offer.y == bottom);
  LOKA_VERIFY(state.y == bottom + 7);
  std::printf("%s/%s passed: sibling Y=%d, width=%d\n", argv[1], mode, sibling->offer.y, width);
}
