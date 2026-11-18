#include "DocumentVocabularyTests.hpp"
#include "CardTests.hpp"
#include "app/doc/Heading.hpp"
#include "app/doc/Paragraph.hpp"
#include "app/doc/CodeBlock.hpp"
#include "app/doc/Inline.hpp"
#include "support/DocumentFixture.hpp"
#include "platform/null/context/NullAttributedTextContext.hpp"
#include "support/LokaAllocFailure.hpp"
#include "support/TestVerify.hpp"
#include "app/nodes/nestable/LazyView.hpp"
#include "app/nodes/nestable/For.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "platform/null/NullInputDoor.hpp"
#include "core/LokaAlloc.hpp"
#include "app/layout/GridLayout.hpp"
#include <climits>
#include <cstdio>
#if defined(__linux__) && !defined(__SANITIZE_ADDRESS__) && !defined(NDEBUG)
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

namespace
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using loka::dsl::testing::SceneTestAccess;
  using namespace PropsReconciliationSupport;

  AttributedTextNode *leaf(Node *node)
  {
    Node *child = node->asNestable()->childrenHead();
    if (child && child->kind() == NODE_KIND_CARD)
      child = static_cast<CardNode *>(child)->contentHead();
    LOKA_VERIFY(child && child->asAttributedTextNode());
    return child->asAttributedTextNode();
  }
  AttributedString value(AttributedTextNode *node)
  {
    return node->props.text_->get();
  }
  int points(const TextStyle &style)
  {
    int result = 0;
    LOKA_VERIFY(style.size_.queryPoints(result));
    return result;
  }
  class UnreadableBody : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &) const
    {
      return false;
    }
  };
  class Registrar : public DirtySourceRegistrar
  {
  public:
    int calls;
    Registrar()
        : calls(0)
    {
    }
    virtual void markDirtyOnOwnInput(NodeDirtyFlags) {}
    virtual void markDirtyOnChange(StateBase *, NodeDirtyFlags)
    {
      ++this->calls;
    }
  };
} // namespace

void testDocumentVocabularyLowering()
{
  using namespace loka::app;
  for (int level = 1; level <= 6; ++level)
  {
    const int sizes[] = {24, 18, 14, 12, 12, 12};
    AttributedString mixed = Styled("title", TextStyle()) + doc::Link("link", "https://example.com") + doc::Code("code")
                             + Styled("own", FontSize<10>() + TextStyle().weight(TEXT_WEIGHT_NORMAL).italic(false));
    Fixture<doc::HeadingDefinition> f(doc::Heading(level, mixed));
    doc::HeadingNode *node = static_cast<doc::HeadingNode *>(f.node());
    LOKA_VERIFY(node->props.level() == level);
    LOKA_VERIFY(node->props.content.hasSameSegmentValues(mixed));
    LOKA_VERIFY(node->getContext() == 0);
    AttributedTextNode *text = leaf(node);
    LOKA_VERIFY(text->getContext() != 0);
    AttributedString lowered = value(text);
    for (int i = 0; i < 3; ++i)
      LOKA_VERIFY(points(lowered.segment(i).style) == sizes[level - 1]);
    TextWeight weight;
    LOKA_VERIFY(lowered.segment(0).style.queryWeight(weight) && weight == TEXT_WEIGHT_BOLD);
    LOKA_VERIFY(lowered.segment(1).link == mixed.segment(1).link);
    LOKA_VERIFY(lowered.segment(2).style.family() == TEXT_FAMILY_MONOSPACE);
    LOKA_VERIFY(points(lowered.segment(3).style) == 10);
    LOKA_VERIFY(lowered.segment(3).style.queryWeight(weight) && weight == TEXT_WEIGHT_NORMAL);
    bool italic = true;
    LOKA_VERIFY(lowered.segment(3).style.queryItalic(italic) && !italic);
    Registrar registrar;
    text->declareDirtySources(registrar);
    LOKA_VERIFY(registrar.calls == 0);
  }
  Fixture<doc::CodeBlockDefinition> code(doc::CodeBlock("one\ntwo\n"));
  Node *cardNode = code.node()->asNestable()->childrenHead();
  LOKA_VERIFY(cardNode->kind() == NODE_KIND_CARD);
  CardNode *card = static_cast<CardNode *>(cardNode);
  LOKA_VERIFY(card->contentHead() == leaf(code.node()));
  int bands = 0;
  for (Node *n = card->childrenHead()->nextInComposition; n; n = n->nextInComposition)
  {
    LOKA_VERIFY(n->asCardBandNode());
    ++bands;
  }
  LOKA_VERIFY(bands == 4);
  LOKA_VERIFY(value(leaf(code.node())).segment(0).text.equals(String("one\ntwo\n")));
  CardNode absent((CardProps()));
  LOKA_VERIFY(absent.contentHead() == 0);
  Fixture<CardDefinition> empty((Card()));
  LOKA_VERIFY(static_cast<CardNode *>(empty.node())->contentHead() == 0);
}

#ifdef NDEBUG
namespace
{
  // A structurally different declaration exercises the release guard without
  // modifying a mounted tree or its ownership links.
  template <class P> class UnexpectedChildren : public P::NodeType
  {
    typedef typename P::NodeType Base;

  public:
    explicit UnexpectedChildren(const P &props)
        : Base(props)
    {
    }

  protected:
    virtual void composeChildren(NodeComposition &c)
    {
      c.declare(Column() << AttributedText(Styled("existing", TextStyle())));
    }
  };

  template <class P, class D> void checkUnexpectedShape(const P &initial, const D &replacement)
  {
    typedef NodeDefinition<P, UnexpectedChildren<P> > Unexpected;
    Fixture<Unexpected> f((Unexpected(initial)));
    typename P::NodeType *node = static_cast<typename P::NodeType *>(f.node());
    Node *column = node->childrenHead();
    LOKA_VERIFY(column && column->asStackNode());
    AttributedTextNode *existing = column->asNestable()->childrenHead()->asAttributedTextNode();
    LOKA_VERIFY(existing);
    LOKA_VERIFY(replacement.applyPropsToNode(node));
    LOKA_VERIFY(!(node->props < replacement.props) && !(replacement.props < node->props));
    LOKA_VERIFY(node->childrenHead() == column && value(existing).segment(0).text.equals(String("existing")));
  }
} // namespace
#endif
void testDocumentVocabularyRetainedApply()
{
#ifdef NDEBUG
  checkUnexpectedShape(doc::HeadingProps(1, "old"), doc::Heading(3, "new"));
  checkUnexpectedShape(doc::ParagraphProps("old"), doc::Paragraph("new"));
  checkUnexpectedShape(doc::CodeBlockProps("old"), doc::CodeBlock("new"));
#endif
  MutableState<LinkTarget> a, b;
  NodeState<LinkTarget> boxA(&a, 0), boxB(&b, 0);
  Fixture<doc::HeadingDefinition> h(doc::Heading(1, doc::Link("link", "https://example.com")).activatedLink(boxA));
  Node *identity = leaf(h.node());
  LOKA_VERIFY(doc::Heading(3, "new").activatedLink(boxB).applyPropsToNode(h.node()));
  settle(h.scene);
  LOKA_VERIFY(leaf(h.node()) == identity);
  LOKA_VERIFY(points(value(leaf(h.node())).segment(0).style) == 14);
  LOKA_VERIFY(value(leaf(h.node())).segment(0).text.equals(String("new")));
  LOKA_VERIFY(leaf(h.node())->props.activatedLink_.state() == &b);
  LOKA_VERIFY(doc::Heading(2, "none").applyPropsToNode(h.node()));
  LOKA_VERIFY(!leaf(h.node())->props.activatedLink_.isValid());
  LOKA_VERIFY(doc::Heading(2, "again").activatedLink(boxA).applyPropsToNode(h.node()));
  LOKA_VERIFY(leaf(h.node())->props.activatedLink_.state() == &a);
  Fixture<doc::ParagraphDefinition> p(doc::Paragraph("old").activatedLink(boxA));
  identity = leaf(p.node());
  LOKA_VERIFY(doc::Paragraph("new").activatedLink(boxB).applyPropsToNode(p.node()));
  LOKA_VERIFY(leaf(p.node()) == identity && leaf(p.node())->props.activatedLink_.state() == &b);
  LOKA_VERIFY(doc::Paragraph("new").applyPropsToNode(p.node()));
  LOKA_VERIFY(!leaf(p.node())->props.activatedLink_.isValid());
  LOKA_VERIFY(doc::Paragraph("new").activatedLink(boxA).applyPropsToNode(p.node()));
  LOKA_VERIFY(leaf(p.node())->props.activatedLink_.state() == &a);
  Fixture<doc::CodeBlockDefinition> c(doc::CodeBlock("old"));
  identity = leaf(c.node());
  LOKA_VERIFY(doc::CodeBlock("new\nlong long long").applyPropsToNode(c.node()));
  LOKA_VERIFY(leaf(c.node()) == identity);
  LOKA_VERIFY(value(leaf(c.node())).segment(0).text.equals(String("new\nlong long long")));
  doc::HeadingNode unattached(doc::HeadingProps(1, "old"));
  // The generic Component path uses the same specialization as the named definition.
  unattached.setPropsTypeId(doc::HeadingProps::staticTypeId());
  LOKA_VERIFY(Component(doc::HeadingProps(4, "latest")).applyPropsToNode(&unattached));
  LOKA_VERIFY(unattached.childrenHead() == 0 && unattached.props.level() == 4);
  ComponentContext context;
  context.setBoundary(root(c.scene));
  context.setStateOwner(root(c.scene));
  context.setScene(&c.scene);
  context.setPlatformController(&c.controller);
  BoundaryNode::composeSubtree(&unattached, context, COMPOSE_EVENT_ATTACH, root(c.scene));
  LOKA_VERIFY(value(leaf(&unattached)).segment(0).text.equals(String("latest")));
  BoundaryNode::composeSubtree(&unattached, context, COMPOSE_EVENT_DETACH, root(c.scene));
}

void testDocumentVocabularyFailure()
{
#ifdef TEST_BUILD
  using namespace loka::core::testing;
  failLokaAllocRaw("AttributedString", "Segments", 1);
  {
    AttributedString invalid = Styled("refused", TextStyle());

    LOKA_VERIFY(!invalid.valid());
    Fixture<doc::ParagraphDefinition> p(doc::Paragraph("before"));
    Node *identity = leaf(p.node());
    LOKA_VERIFY(doc::Paragraph(invalid).applyPropsToNode(p.node()));
    LOKA_VERIFY(!value(leaf(p.node())).valid());
    LOKA_VERIFY(!static_cast<doc::ParagraphNode *>(p.node())->props.content.valid());
    LOKA_VERIFY(doc::Paragraph("after").applyPropsToNode(p.node()));
    LOKA_VERIFY(leaf(p.node()) == identity && value(leaf(p.node())).valid());
    Fixture<doc::HeadingDefinition> h(doc::Heading(1, "before"));
    doc::HeadingDefinition replacement = doc::Heading(2, "after");
    failLokaAllocRaw("AttributedString", "Segments", 1);
    LOKA_VERIFY(replacement.applyPropsToNode(h.node()));

    LOKA_VERIFY(!value(leaf(h.node())).valid());
    LOKA_VERIFY(static_cast<doc::HeadingNode *>(h.node())->props.level() == 2);
    LOKA_VERIFY(replacement.applyPropsToNode(h.node()));
    LOKA_VERIFY(value(leaf(h.node())).valid());
    Fixture<doc::CodeBlockDefinition> c(doc::CodeBlock("before"));
    doc::CodeBlockDefinition next = doc::CodeBlock("after");
    failLokaAllocRaw("AttributedString", "Segments", 1);
    LOKA_VERIFY(next.applyPropsToNode(c.node()));

    LOKA_VERIFY(!value(leaf(c.node())).valid());
    LOKA_VERIFY(static_cast<doc::CodeBlockNode *>(c.node())->props.text.equals(String("after")));
    LOKA_VERIFY(next.applyPropsToNode(c.node()));
    LOKA_VERIFY(value(leaf(c.node())).valid());
    const String opaque = String::FromPlatform(Managed<loka::platform::String>::Wrap(new UnreadableBody()));
    doc::CodeBlockDefinition unreadable = doc::CodeBlock(opaque);
    LOKA_VERIFY(unreadable.applyPropsToNode(c.node()));
    LOKA_VERIFY(value(leaf(c.node())).valid());
    LOKA_VERIFY(value(leaf(c.node())).segment(0).text.sharesRepresentation(opaque));
    failLokaAllocRaw("AttributedString", "Segments", 1);
    LOKA_VERIFY(unreadable.applyPropsToNode(c.node()));

    LOKA_VERIFY(!value(leaf(c.node())).valid());
    LOKA_VERIFY(static_cast<doc::CodeBlockNode *>(c.node())->props.text.sharesRepresentation(opaque));
  }
  allowLokaAllocRaw();
#endif
}

void testDocumentVocabularyOrdering()
{
  MutableState<LinkTarget> aBox, bBox;
  doc::HeadingProps one = doc::Heading(1, "same").activatedLink(NodeState<LinkTarget>(&aBox, 0)).props;
  doc::HeadingProps two = doc::Heading(1, "same").activatedLink(NodeState<LinkTarget>(&bBox, 0)).props;
  doc::HeadingProps noBox(1, "same");
  LOKA_VERIFY((one < two) != (two < one));
  LOKA_VERIFY((one < noBox) != (noBox < one));
  LOKA_VERIFY(one < doc::HeadingProps(2, "a"));
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  AttributedString invalid = Styled("invalid", TextStyle());
  loka::core::testing::allowLokaAllocRaw();
  doc::ParagraphProps bad(invalid), sameBad(invalid), empty((AttributedString()));
  LOKA_VERIFY(!invalid.valid() && bad < empty && !(empty < bad));
  LOKA_VERIFY(!(bad < sameBad) && !(sameBad < bad));
  AttributedString a = Styled("ab", TextStyle());
  AttributedString b = Styled("a", TextStyle()) + Styled("b", TextStyle());
  doc::ParagraphProps values[] = {doc::ParagraphProps(a),
                                  doc::ParagraphProps(b),
                                  doc::ParagraphProps(Styled("ab", Bold)),
                                  doc::ParagraphProps(doc::Link("ab", "url"))};
  for (int i = 0; i < 4; ++i)
  {
    LOKA_VERIFY(!(values[i] < values[i]));
    for (int j = i + 1; j < 4; ++j)
      LOKA_VERIFY((values[i] < values[j]) != (values[j] < values[i]));
    for (int j = 0; j < 4; ++j)
      for (int k = 0; k < 4; ++k)
        if (values[i] < values[j] && values[j] < values[k])
          LOKA_VERIFY(values[i] < values[k]);
  }
}

void testDocumentVocabularyLevels()
{
#if !defined(NDEBUG) && (!defined(__linux__) || defined(__SANITIZE_ADDRESS__))
  std::puts("[skip] Heading level assertion checks require a Linux debug build without AddressSanitizer.");
#else
  const int levels[] = {0, -1, 7, INT_MIN, INT_MAX};
  for (int i = 0; i < 5; ++i)
  {
#ifdef NDEBUG
    doc::HeadingProps p(levels[i], "x");
    LOKA_VERIFY(p.level() == (levels[i] < 1 ? 1 : 6));
    LOKA_VERIFY(doc::Lower(p).props.text_->get().valid());
#else
    pid_t pid = fork();
    LOKA_VERIFY(pid >= 0);
    if (pid == 0)
    {
      doc::HeadingProps p(levels[i], "x");
      (void)p;
      _exit(0);
    }
    int status = 0;
    LOKA_VERIFY(waitpid(pid, &status, 0) == pid);
    LOKA_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
#endif
  }
#endif
}

namespace
{
  Node *findDocument(Node *node, const void *type)
  {
    if (!node)
      return 0;
    if (node->propsTypeId() == type)
      return node;
    INestable *parent = node->asNestable();
    for (Node *child = parent ? parent->childrenHead() : 0; child; child = child->nextInComposition)
      if (Node *found = findDocument(child, type))
        return found;
    return 0;
  }
  class ParkRoot : public BoundaryNodeFor<ParkRoot>
  {
  public:
    NodeState<bool> shown;
    NodeState<LinkTarget> a, b;
    explicit ParkRoot(const BoundaryPropsFor<ParkRoot> &p)
        : BoundaryNodeFor<ParkRoot>(p)
    {
      this->state(this->shown, true);
      this->state(this->a, LinkTarget());
      this->state(this->b, LinkTarget());
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Show(*this->shown.state())
                << doc::Heading(1, doc::Link("before", "url")).activatedLink(this->a) << doc::CodeBlock("before")
                << doc::Paragraph(doc::Link("before", "url")).activatedLink(this->a));
    }
    void setNextHeading(const doc::HeadingProps &props)
    {
      IBranchSeatDefinition *seat = this->composition().root()->asBranchSeatDefinition();
      LOKA_VERIFY(seat);
      NodeDefinitionBase *arm = seat->armDefinition(1);
      NodeDefinitionBase *heading = arm->asNestableDefinition()->childrenHead();
      LOKA_VERIFY(heading->propsBase()->propsTypeId() == doc::HeadingProps::staticTypeId());
      typedef NodeDefinition<doc::HeadingProps, doc::HeadingNode> D;
      static_cast<D *>(heading)->props = props;
    }
    bool reconcile(Node *node, NodeDefinitionBase &definition, Scene &scene, IPlatformController &controller)
    {
      ComponentContext context;
      context.setBoundary(this);
      context.setStateOwner(this);
      context.setScene(&scene);
      context.setPlatformController(&controller);
      return this->reconcileParkedBranch(context, node, &definition);
    }
  };
  void lazyCodeFailure()
  {
    typedef doc::CodeBlockProps P;
    typedef reservation::Nodes<
        doc::CodeBlockNode,
        1,
        reservation::Nodes<
            CardNode,
            1,
            reservation::Nodes<StackNode,
                               1,
                               reservation::Nodes<CardBandNode, 4, reservation::Nodes<AttributedTextNode, 1> > > > >
        Nodes;
    PushStateTracker tracker;
    ObservableList<P> list;
    MutableState<Frame> viewport(Frame(0, 0, 200, 100));
    LOKA_VERIFY(list.attach(&tracker, 4) == ATTACH_OK);
    LOKA_VERIFY(list.insert(0, P("before")) == EDIT_OK);
    Fixture<LazyGrid<P> > f(LazyColumn(list, reservation::SeatNodes<Nodes>(), 4).cells(200, 30).viewport(viewport));
    Node *node = findDocument(f.node(), P::staticTypeId());
    LOKA_VERIFY(node);
    Node *child = leaf(node);
    P after("updated");
    loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
    LOKA_VERIFY(list.update(list.at(0).id, after) == EDIT_OK);
    f.scene.flushInvalidation();
    LOKA_VERIFY(leaf(node) == child && !value(leaf(node)).valid());
    LOKA_VERIFY(static_cast<doc::CodeBlockNode *>(node)->props.text.equals(after.text));
    LOKA_VERIFY(list.update(list.at(0).id, P("recovered")) == EDIT_OK);
    settle(f.scene);
    LOKA_VERIFY(leaf(node) == child && value(leaf(node)).segment(0).text.equals(String("recovered")));
  }

  template <class P> void lazyDelivery(const P &before, const P &after, const P &invalid)
  {
    PushStateTracker tracker;
    ObservableList<P> list;
    MutableState<Frame> viewport(Frame(0, 0, 200, 100));
    LOKA_VERIFY(list.attach(&tracker, 4) == ATTACH_OK);
    LOKA_VERIFY(list.insert(0, before) == EDIT_OK);
    typedef reservation::Nodes<typename P::NodeType, 1, reservation::Nodes<AttributedTextNode, 1> > Nodes;
    typedef LazyGrid<P> D;
    D d = LazyColumn(list, reservation::SeatNodes<Nodes>(), 4).cells(200, 30).viewport(viewport);
    Fixture<D> f(d);
    Node *node = findDocument(f.node(), P::staticTypeId());
    LOKA_VERIFY(node);
    Node *child = leaf(node);
    LOKA_VERIFY(list.update(list.at(0).id, after) == EDIT_OK);
    settle(f.scene);
    LOKA_VERIFY(findDocument(f.node(), P::staticTypeId()) == node && leaf(node) == child);
    LOKA_VERIFY(value(leaf(node)).hasSameSegmentValues(doc::Lower(after).props.text_->get()));
    MutableState<LinkTarget> a, b;
    const AttributedString link = doc::Link("link", "https://example.com");
    for (int phase = 0; phase < 4; ++phase)
    {
      P linked = after;
      linked.content = link;
      if (phase != 2)
        linked.activatedLink_ = NodeState<LinkTarget>(phase == 1 ? &b : &a, 0);
      LOKA_VERIFY(list.update(list.at(0).id, linked) == EDIT_OK);
      settle(f.scene);
      LOKA_VERIFY(leaf(node) == child);
      a.set(LinkTarget());
      b.set(LinkTarget());
      NullAttributedTextContext *input = static_cast<NullAttributedTextContext *>(child->getContext());
      for (int click = 0; click < 2; ++click)
        LOKA_VERIFY(NullInputDoor::activateLink(*input, 0) == (phase != 2));
      LOKA_VERIFY(a.get().isNone() == (phase == 1 || phase == 2));
      LOKA_VERIFY(b.get().isNone() == (phase != 1));
    }
    LOKA_VERIFY(list.update(list.at(0).id, invalid) == EDIT_OK);
    f.scene.flushInvalidation();
    LOKA_VERIFY(leaf(node) == child && !value(leaf(node)).valid());
    LOKA_VERIFY(list.update(list.at(0).id, after) == EDIT_OK);
    settle(f.scene);
    LOKA_VERIFY(leaf(node) == child && value(leaf(node)).valid());
  }
} // namespace
void testDocumentVocabularyOwnerPaths()
{
#ifdef TEST_BUILD
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  AttributedString invalid = Styled("invalid", TextStyle());
  loka::core::testing::allowLokaAllocRaw();
  LOKA_VERIFY(!invalid.valid());
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 0);
  lazyCodeFailure();
  loka::core::testing::allowLokaAllocRaw();
  lazyDelivery(doc::HeadingProps(1, "old"), doc::HeadingProps(3, Styled("new", Italic)), doc::HeadingProps(2, invalid));
  lazyDelivery(doc::ParagraphProps("old"), doc::ParagraphProps(Styled("new", Italic)), doc::ParagraphProps(invalid));
#ifdef LOKA_LIFECYCLE_AUDIT
  const int liveBefore = loka::core::LokaAllocAuditTotalLiveCount();
#endif
  {
    NullScenePlatformController controller;
    Scene scene((Boundary<ParkRoot>()));
    scene.mount(&controller);
    SceneTestAccess::updateAttached(scene, true);
    ParkRoot *root = static_cast<ParkRoot *>(SceneTestAccess::rootBoundary(scene));
    Node *node = findDocument(root, doc::HeadingProps::staticTypeId());
    LOKA_VERIFY(node);
    Node *child = leaf(node);
    Node *code = findDocument(root, doc::CodeBlockProps::staticTypeId());
    Node *codeLeaf = leaf(code);
    Node *paragraph = findDocument(root, doc::ParagraphProps::staticTypeId());
    Node *paragraphLeaf = leaf(paragraph);

    root->shown.set(false);
    settle(scene);
    LOKA_VERIFY(node->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
    doc::HeadingDefinition next = doc::Heading(3, doc::Link("parked", "url")).activatedLink(root->b);
    LOKA_VERIFY(root->reconcile(node, next, scene, controller));
    LOKA_VERIFY(leaf(node) == child && points(value(leaf(node)).segment(0).style) == 14);
    doc::HeadingDefinition refused = doc::Heading(2, invalid);
    LOKA_VERIFY(root->reconcile(node, refused, scene, controller));
    LOKA_VERIFY(leaf(node) == child && !value(leaf(node)).valid());
    LOKA_VERIFY(root->reconcile(node, next, scene, controller));
    LOKA_VERIFY(value(leaf(node)).valid());
    LOKA_VERIFY(leaf(node)->props.activatedLink_.state() == root->b.state());
    doc::ParagraphDefinition paragraphRefusal = doc::Paragraph(invalid);
    LOKA_VERIFY(root->reconcile(paragraph, paragraphRefusal, scene, controller));
    LOKA_VERIFY(leaf(paragraph) == paragraphLeaf && !value(leaf(paragraph)).valid());
    const AttributedString link = doc::Link("before", "url");
    const NodeState<LinkTarget> boxes[] = {root->a, root->b, NodeState<LinkTarget>(), root->a};
    for (unsigned i = 0; i < sizeof(boxes) / sizeof(boxes[0]); ++i)
    {
      doc::ParagraphDefinition linked = doc::Paragraph(link).activatedLink(boxes[i]);
      LOKA_VERIFY(root->reconcile(paragraph, linked, scene, controller));
      LOKA_VERIFY(leaf(paragraph) == paragraphLeaf && value(leaf(paragraph)).hasSameSegmentValues(link));
      LOKA_VERIFY(leaf(paragraph)->props.activatedLink_.state() == boxes[i].state());
    }
    doc::CodeBlockDefinition updated = doc::CodeBlock("parked code");
    loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
    LOKA_VERIFY(root->reconcile(code, updated, scene, controller));
    loka::core::testing::allowLokaAllocRaw();
    LOKA_VERIFY(codeLeaf == leaf(code) && !value(leaf(code)).valid());
    LOKA_VERIFY(static_cast<doc::CodeBlockNode *>(code)->props.text.equals(String("parked code")));
    LOKA_VERIFY(root->reconcile(code, updated, scene, controller));
    LOKA_VERIFY(codeLeaf == leaf(code) && value(leaf(code)).valid());
    root->setNextHeading(next.props);
    root->shown.set(true);
    settle(scene);
    // The seat's retained declaration is authoritative on reentry.
    LOKA_VERIFY(findDocument(root, doc::HeadingProps::staticTypeId()) == node);
    LOKA_VERIFY(leaf(node) == child && node->lifecycleFact() == NODE_FACT_ATTACHED);
    LOKA_VERIFY(points(value(leaf(node)).segment(0).style) == 14);
    LOKA_VERIFY(leaf(node)->props.activatedLink_.state() == root->b.state());
    NullAttributedTextContext *input = static_cast<NullAttributedTextContext *>(child->getContext());
    LOKA_VERIFY(NullInputDoor::activateLink(*input, 0));
    LOKA_VERIFY(root->a.get().isNone() && !root->b.get().isNone());
  }
  {
    MutableState<bool> shown(true);
    Fixture<ShowDefinition> f(Show(shown).destroyOnDetach() << doc::Paragraph("fresh"));
    Node *fresh = findDocument(f.node(), doc::ParagraphProps::staticTypeId());
    LOKA_VERIFY(doc::Paragraph("applied").applyPropsToNode(fresh));
    shown.set(false);
    f.scene.flushInvalidation();
    LOKA_VERIFY(!findDocument(f.node(), doc::ParagraphProps::staticTypeId()));
    LOKA_VERIFY(root(f.scene)->parkedBranchCountForTesting() == 0);
    shown.set(true);
    f.scene.flushInvalidation();
    Node *replacement = findDocument(f.node(), doc::ParagraphProps::staticTypeId());
    LOKA_VERIFY(value(leaf(replacement)).segment(0).text.equals(String("fresh")));
  }
#ifdef LOKA_LIFECYCLE_AUDIT
  LOKA_VERIFY(loka::core::LokaAllocAuditTotalLiveCount() == liveBefore);
#endif
#endif
}

namespace
{
  template <class D> void activationDelivery(const D &initial)
  {
    MutableState<LinkTarget> a, b;
    NodeState<LinkTarget> boxA(&a, 0), boxB(&b, 0);
    D first = initial;
    first.activatedLink(boxA);
    Fixture<D> f(first);
    AttributedTextNode *child = leaf(f.node());
    NullAttributedTextContext *context = static_cast<NullAttributedTextContext *>(child->getContext());
    LOKA_VERIFY(context);
    for (int phase = 0; phase < 4; ++phase)
    {
      D next = initial;
      if (phase == 0 || phase == 3)
        next.activatedLink(boxA);
      if (phase == 1)
        next.activatedLink(boxB);
      LOKA_VERIFY(next.applyPropsToNode(f.node()));
      settle(f.scene);
      a.set(LinkTarget());
      b.set(LinkTarget());
      for (int click = 0; click < 2; ++click)
      {
        const bool activated = NullInputDoor::activateLink(*context, 0);
        LOKA_VERIFY(activated == (phase != 2));
      }
      LOKA_VERIFY(a.get().isNone() == (phase != 0 && phase != 3));
      LOKA_VERIFY(b.get().isNone() == (phase != 1));
      LOKA_VERIFY(leaf(f.node()) == child);
    }
  }
  struct GridLayoutOffers
  {
    LayoutState first, second;
    int count;
    GridLayoutOffers()
        : count(0)
    {
    }
    static int capture(void *context, Node *, const LayoutState &state)
    {
      GridLayoutOffers &offers = *static_cast<GridLayoutOffers *>(context);
      if (offers.count++ == 0)
        offers.first = state;
      else
        offers.second = state;
      return state.y + state.height;
    }
  };
  template <class D, class Direct> void equivalentLayout(const D &wrapped, const Direct &direct, short width)
  {
    Fixture<D> a(wrapped);
    Fixture<Direct> b(direct);
    LayoutState first, second;
    first.width = second.width = width;
    const int wrappedBottom = a.controller.projectLayoutForTesting(a.node(), first);
    const int directBottom = b.controller.projectLayoutForTesting(b.node(), second);
    LOKA_VERIFY(wrappedBottom == directBottom);
    AttributedTextNode *left = leaf(a.node());
    AttributedTextNode *right = b.node()->asAttributedTextNode();
    if (!right)
      right = static_cast<CardNode *>(b.node())->contentHead()->asAttributedTextNode();
    const NullTextMeasurement &x = static_cast<NullAttributedTextContext *>(left->getContext())->measurement();
    const NullTextMeasurement &y = static_cast<NullAttributedTextContext *>(right->getContext())->measurement();
    LOKA_VERIFY(x.width() == y.width() && x.height() == y.height() && x.lineCount() == y.lineCount());
  }
} // namespace
void testDocumentVocabularyActivationAndLayout()
{
  activationDelivery(doc::Heading(2, doc::Link("click", "https://example.com")));
  activationDelivery(doc::Paragraph(doc::Link("click", "https://example.com")));
  // Aligned rows consult a preferred-height table, while grids divide the
  // offer into equal cells. Neither expands its offer to a heading's font.
  Fixture<RowDefinition> row(Row().alignVertical(VERTICAL_ALIGNMENT_CENTER) << doc::Heading(1, "A\nB"));
  Node *heading = findDocument(row.node(), doc::HeadingProps::staticTypeId());
  LOKA_VERIFY(layout::preferredChildHeightForRow(heading, 7, 11, 12, 13, 14, 15, 0) == 7);
  LayoutState bounds;
  bounds.width = 100;
  bounds.height = 18;
  const int rowBottom = row.controller.projectLayoutForTesting(row.node(), bounds);
  Fixture<GridDefinition> grid(Grid().rows(2).cols(1) << doc::Heading(1, "A\nB") << doc::Paragraph("tail"));
  const int gridBottom = grid.controller.projectLayoutForTesting(grid.node(), bounds);
  LOKA_VERIFY(rowBottom == 48 && gridBottom == 48);
  // The second row starts at half the offer even when the title exceeds that cell.
  GridLayoutOffers offers;
  layout::computeGridLayoutResultY(
      static_cast<GridNode *>(grid.node()), bounds, layout::GridLayoutMetrics(), &offers, &GridLayoutOffers::capture);
  LOKA_VERIFY(offers.count == 2 && offers.first.height == 9 && offers.second.y == 9);
  const char *cases[] = {
      "one\ntwo", "last\n", "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz", "one two three four five"};
  for (int i = 0; i < 4; ++i)
  {
    const AttributedString text = Styled(cases[i], TextStyle());
    equivalentLayout(doc::Heading(2, text),
                     AttributedText((FontSize<18>() + Bold) + text) + BlockStyle().wrap(TEXT_WRAP_WORD),
                     short(25));
    equivalentLayout(doc::Paragraph(text), AttributedText(text) + BlockStyle().wrap(TEXT_WRAP_WORD), short(25));
    equivalentLayout(doc::CodeBlock(cases[i]),
                     Card() << (AttributedText(Styled(cases[i], Monospace))
                                + BlockStyle().wrap(TEXT_WRAP_NONE).truncation(TEXT_TRUNCATION_CLIP)),
                     short(25));
  }
}

namespace
{
#ifdef TEST_BUILD
  class RefusedHeading : public doc::HeadingNode
  {
  public:
    explicit RefusedHeading(const doc::HeadingProps &p)
        : doc::HeadingNode(p)
    {
    }

  protected:
    virtual void composeChildren(NodeComposition &c)
    {
      doc::HeadingNode::composeChildren(c);
      if (this->props.level() == 1)
        loka::core::testing::failLokaAllocRaw("NodeDefinition", "Node", 1);
    }
  };
  class RefusedRoot : public BoundaryNodeFor<RefusedRoot>
  {
  public:
    NodeState<bool> shown;
    void park(Node *node)
    {
      this->detachRetainedSubtree(node);
    }
    explicit RefusedRoot(const BoundaryPropsFor<RefusedRoot> &p)
        : BoundaryNodeFor<RefusedRoot>(p)
    {
      this->state(this->shown, true);
    }
    virtual bool flushViewDirtyImmediately(NodeDirtyFlags) const
    {
      return false;
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(Show(*this->shown.state())
                << NodeDefinition<doc::HeadingProps, RefusedHeading>(doc::HeadingProps(1, "old")));
    }
  };

  template <class D> void childlessApply(const D &replacement)
  {
    typedef typename D::NodeType NodeType;
    typedef typename D::PropsType Props;
    const Props initial;
    NodeType node(initial);
    node.setPropsTypeId(Props::staticTypeId());
    const int attempts = loka::core::testing::lokaAllocRawAttempts();
    LOKA_VERIFY(replacement.applyPropsToNode(&node));
    LOKA_VERIFY(loka::core::testing::lokaAllocRawAttempts() == attempts);
    LOKA_VERIFY(!node.childrenHead());
    LOKA_VERIFY(!(node.props < replacement.props) && !(replacement.props < node.props));
  }
#endif
} // namespace
void testDocumentVocabularyChildStates()
{
#ifdef TEST_BUILD
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 0);
  {
    const AttributedString content = Styled("new", Bold) + Styled(" content", Italic);
    childlessApply(doc::Heading(2, content));
    childlessApply(doc::Paragraph(content));
    childlessApply(doc::CodeBlock("new code"));
  }
  loka::core::testing::allowLokaAllocRaw();
  loka::core::testing::failLokaAllocRaw("NodeDefinition", "Node", 0);
  {
    // Materialization refusal leaves an attached semantic component without children.
    NullScenePlatformController controller;
    Scene scene((Boundary<RefusedRoot>()));
    scene.mount(&controller);
    SceneTestAccess::updateAttached(scene, true);
    RefusedRoot *owner = static_cast<RefusedRoot *>(SceneTestAccess::rootBoundary(scene));
    Node *node = findDocument(owner, doc::HeadingProps::staticTypeId());
    LOKA_VERIFY(node && !node->asNestable()->childrenHead());
    LOKA_VERIFY(node->lifecycleFact() == NODE_FACT_ATTACHED);
    LOKA_VERIFY(doc::Heading(2, "attached childless").applyPropsToNode(node));
    LOKA_VERIFY(!node->asNestable()->childrenHead());
    LOKA_VERIFY(static_cast<doc::HeadingNode *>(node)->props.level() == 2);
    owner->shown.set(false);
    // The owner retries its pending attach before parking. It materializes from
    // the accepted props, then retains that recovered child in the parked arm.
    LOKA_VERIFY(scene.flushInvalidation());
    LOKA_VERIFY(!findDocument(owner, doc::HeadingProps::staticTypeId()));
    LOKA_VERIFY(node->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
    LOKA_VERIFY(value(leaf(node)).segment(0).text.equals(String("attached childless")));

    // Isolate the other table row using the same retained-detach and attach
    // doors without the seat's intervening retry.
    doc::HeadingNode empty(doc::HeadingProps(1, "not materialized"));
    empty.setPropsTypeId(doc::HeadingProps::staticTypeId());
    owner->park(&empty);
    LOKA_VERIFY(empty.lifecycleFact() == NODE_FACT_DETACHED_RETAINED && !empty.childrenHead());
    LOKA_VERIFY(doc::Heading(3, "parked childless").applyPropsToNode(&empty));
    LOKA_VERIFY(!empty.childrenHead() && empty.props.level() == 3);
    ComponentContext context;
    context.setBoundary(owner);
    context.setStateOwner(owner);
    context.setScene(&scene);
    context.setPlatformController(&controller);
    BoundaryNode::composeSubtree(&empty, context, COMPOSE_EVENT_ATTACH, owner);
    LOKA_VERIFY(points(value(leaf(&empty)).segment(0).style) == 14);
    LOKA_VERIFY(value(leaf(&empty)).segment(0).text.equals(String("parked childless")));
    BoundaryNode::composeSubtree(&empty, context, COMPOSE_EVENT_DETACH, owner);
  }
  loka::core::testing::allowLokaAllocRaw();
  // First lowering refusal is different: structure exists and later apply recovers it.
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  AttributedString invalid = Styled("bad", TextStyle());
  loka::core::testing::allowLokaAllocRaw();
  Fixture<doc::ParagraphDefinition> first(doc::Paragraph(invalid), false);
  Node *child = leaf(first.node());
  LOKA_VERIFY(!value(leaf(first.node())).valid());
  LOKA_VERIFY(doc::Paragraph("recovered").applyPropsToNode(first.node()));
  LOKA_VERIFY(leaf(first.node()) == child && value(leaf(first.node())).valid());
#endif
}
#ifdef LOKA_CARD_CLONE_FAULT_PINS
namespace
{
  class RefusedCodeBlock : public doc::CodeBlockNode
  {
  public:
    explicit RefusedCodeBlock(const doc::CodeBlockProps &p)
        : doc::CodeBlockNode(p)
    {
    }

  protected:
    virtual void composeChildren(NodeComposition &c)
    {
      if (this->props.text.equals(String("first")))
        refuseNextDefinitionAllocation(sizeof(NodeDefinition<AttributedTextProps, AttributedTextNode>));
      doc::CodeBlockNode::composeChildren(c);
    }
  };
} // namespace
#endif
void testDocumentVocabularyInitialRefusalAndFor()
{
#ifdef LOKA_CARD_CLONE_FAULT_PINS
  {
    typedef NodeDefinition<doc::CodeBlockProps, RefusedCodeBlock> FaultDefinition;
    Fixture<FaultDefinition> f(FaultDefinition(doc::CodeBlockProps("first")), false);
    Node *block = f.node();
    LOKA_VERIFY(block);
    LOKA_VERIFY(!block->asNestable()->childrenHead());
    LOKA_VERIFY(doc::CodeBlock("latest").applyPropsToNode(block));
    LOKA_VERIFY(!block->asNestable()->childrenHead());
    ComponentContext context;
    context.setBoundary(root(f.scene));
    context.setStateOwner(root(f.scene));
    context.setScene(&f.scene);
    context.setPlatformController(&f.controller);
    BoundaryNode::composeSubtree(block, context, COMPOSE_EVENT_ATTACH, root(f.scene));
    LOKA_VERIFY(f.node() == block && value(leaf(block)).segment(0).text.equals(String("latest")));
  }
#else
  std::puts(
      "[skip] CodeBlock leaf capture refusal needs the wrapped-new allocation hook (LOKA_CARD_CLONE_FAULT_PINS).");
#endif
#ifdef TEST_BUILD
  doc::HeadingDefinition heading = doc::Heading(1, "first");
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  {
    Fixture<doc::HeadingDefinition> h(heading, false);
    LOKA_VERIFY(!value(leaf(h.node())).valid());
    LOKA_VERIFY(static_cast<doc::HeadingNode *>(h.node())->props.content.valid());
    LOKA_VERIFY(heading.applyPropsToNode(h.node()));
    LOKA_VERIFY(value(leaf(h.node())).valid());
  }
  loka::core::testing::allowLokaAllocRaw();
  doc::CodeBlockDefinition code = doc::CodeBlock("first\n");
  loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
  {
    Fixture<doc::CodeBlockDefinition> c(code, false);
    LOKA_VERIFY(!value(leaf(c.node())).valid());
    LOKA_VERIFY(code.applyPropsToNode(c.node()));
    LOKA_VERIFY(value(leaf(c.node())).valid());
  }
  loka::core::testing::allowLokaAllocRaw();
#endif
  const doc::HeadingProps headings[] = {doc::HeadingProps(2, "first"), doc::HeadingProps(3, "second")};
  ColumnDefinition column;
  column << For(123, headings, ComponentItemFactory<doc::HeadingProps>());
  Fixture<ColumnDefinition> f(column);
  Node *first = findDocument(f.node(), doc::HeadingProps::staticTypeId());
  LOKA_VERIFY(first && static_cast<doc::HeadingNode *>(first)->props.level() == 2);
  LOKA_VERIFY(points(value(leaf(first)).segment(0).style) == 18);
}

void testDocumentVocabularyIndependentHeadingEdits()
{
  // Change one semantic input at a time through each of the three owner paths.
  typedef doc::HeadingProps P;
  typedef reservation::Nodes<doc::HeadingNode, 1, reservation::Nodes<AttributedTextNode, 1> > Nodes;
  PushStateTracker tracker;
  ObservableList<P> list;
  MutableState<Frame> viewport(Frame(0, 0, 200, 100));
  const P before(1, "old");
  LOKA_VERIFY(list.attach(&tracker, 1) == ATTACH_OK);
  LOKA_VERIFY(list.insert(0, before) == EDIT_OK);
  Fixture<LazyGrid<P> > lazy(LazyColumn(list, reservation::SeatNodes<Nodes>(), 1).cells(180, 40).viewport(viewport));
  Fixture<doc::HeadingDefinition> direct((doc::HeadingDefinition(before)));
  NullScenePlatformController controller;
  Scene scene((Boundary<ParkRoot>()));
  LOKA_VERIFY(scene.mount(&controller));
  SceneTestAccess::updateAttached(scene, true);
  ParkRoot *park = static_cast<ParkRoot *>(root(scene));
  Node *parents[] = {
      direct.node(), findDocument(lazy.node(), P::staticTypeId()), findDocument(park, P::staticTypeId())};
  AttributedTextNode *children[] = {leaf(parents[0]), leaf(parents[1]), leaf(parents[2])};
  park->shown.set(false);
  settle(scene);
  LOKA_VERIFY(doc::HeadingDefinition(before).applyPropsToNode(parents[2]));
  const P edits[] = {P(1, "new"), P(2, "new"), P(2, Styled("new", Italic))};
  for (unsigned i = 0; i < 3; ++i)
  {
    doc::HeadingDefinition d(edits[i]);
    LOKA_VERIFY(d.applyPropsToNode(parents[0]));
    LOKA_VERIFY(list.update(list.at(0).id, edits[i]) == EDIT_OK);
    LOKA_VERIFY(park->reconcile(parents[2], d, scene, controller));
    for (unsigned path = 0; path < 3; ++path)
    {
      LOKA_VERIFY(leaf(parents[path]) == children[path]);
      AttributedString text = value(children[path]);
      LOKA_VERIFY(text.segmentCount() == 1 && text.segment(0).text.equals(String("new")));
      LOKA_VERIFY(points(text.segment(0).style) == (i == 0 ? 24 : 18));
      bool italic = false;
      const bool declaresItalic = text.segment(0).style.queryItalic(italic);
      LOKA_VERIFY(declaresItalic == (i == 2) && italic == (i == 2));
    }
    LayoutState offer;
    offer.width = 180;
    direct.controller.projectLayoutForTesting(parents[0], offer);
    lazy.controller.projectLayoutForTesting(parents[1], offer);
    for (unsigned path = 0; path < 2; ++path)
    {
      NullAttributedTextContext *context = static_cast<NullAttributedTextContext *>(children[path]->getContext());
      LOKA_VERIFY(context->measurement().height() == (i == 0 ? 24 : 18));
    }
  }
  park->setNextHeading(edits[2]);
  park->shown.set(true);
  settle(scene);
  LOKA_VERIFY(leaf(findDocument(park, P::staticTypeId())) == children[2]);
  NullAttributedTextContext *context = static_cast<NullAttributedTextContext *>(children[2]->getContext());
  LOKA_VERIFY(context && context->measurement().height() == 18);
}
