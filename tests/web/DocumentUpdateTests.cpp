#include "app/doc/Paragraph.hpp"
#include "FakeBoundary.hpp"
#include "WebScenePlatformController.hpp"
#include "WebSession.hpp"
#include "app/nodes/nestable/LazyView.hpp"
#include "app/nodes/nestable/Show.hpp"
#include "support/PropsReconciliation.hpp"

namespace webtest
{
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;
  using loka::dsl::testing::SceneTestAccess;

  template <class D> D constant(const char *text);
  template <> Text constant<Text>(const char *text)
  {
    return Text(text);
  }
  template <> AttributedText constant<AttributedText>(const char *text)
  {
    return AttributedText(Styled(text, TextStyle()));
  }
  template <> doc::ParagraphDefinition constant<doc::ParagraphDefinition>(const char *text)
  {
    return doc::Paragraph(text);
  }

  // LazyView items must be Components. These controls forward only the value
  // to their one ordinary constant leaf, with no scene or controller access.
  template <class D> class PlainItem;
  template <class D> struct PlainProps : NodePropsBase<PlainProps<D> >
  {
    typedef PlainItem<D> NodeType;
    struct TypeTag
    {
    };
    D definition;
    explicit PlainProps(const char *text = "")
        : definition(constant<D>(text))
    {
    }
    bool operator<(const PropsBase &other) const
    {
      return this->definition.props < static_cast<const PlainProps &>(other).definition.props;
    }
  };
  template <class D> class PlainItem : public ComponentNodeWithProps<PlainProps<D> >
  {
  public:
    explicit PlainItem(const PlainProps<D> &p)
        : ComponentNodeWithProps<PlainProps<D> >(p)
    {
    }
    virtual void composeChildren(NodeComposition &c)
    {
      c.declare(this->props.definition);
    }
  };
} // namespace webtest
namespace loka
{
  namespace app
  {
    namespace scene
    {
      template <class D> struct NodePropsApplier<webtest::PlainItem<D>, webtest::PlainProps<D> >
      {
        static bool apply(webtest::PlainItem<D> *node, const webtest::PlainProps<D> &p)
        {
          if (Node *leaf = node->childrenHead())
            LOKA_VERIFY(p.definition.applyPropsToNode(leaf));
          node->props = p;
          return true;
        }
      };
    } // namespace scene
  } // namespace app
} // namespace loka

namespace webtest
{
  namespace
  {
    template <class D, class Checks> void mounted(const D &definition, Checks &checks)
    {
      reset();
      WebSession session;
      WebElement window;
      int width = 180, height = 100;
      LOKA_VERIFY(window.create(session.routes(), session.tokens(), loka::web::ROOT, 0, width, height));
      {
        WebScenePlatformController controller(session, window.token(), width, height, 0);
        Scene scene(definition);
        LOKA_VERIFY(scene.mount(&controller));
        SceneTestAccess::updateAttached(scene, true);
        scene.flushInvalidation();
        LOKA_VERIFY(!scene.hasPendingInvalidation());
        checks(scene);
        SceneTestAccess::unmount(scene);
        controller.destroy();
      }
      WebResource *retired = window.revoke(session.routes());
      LOKA_VERIFY(web_release(retired->token) == loka::web::CONSUMED);
      delete retired;
    }

    Node *findProps(Node *node, const void *type)
    {
      if (node->propsTypeId() == type)
        return node;
      if (INestable *parent = node->asNestable())
        for (Node *child = parent->childrenHead(); child; child = child->nextInComposition)
          if (Node *found = findProps(child, type))
            return found;
      return 0;
    }

    void checkLeaf(Node *tree, int kind, const char *expected)
    {
      const bool plain = kind == loka::web::TEXT;
      Node *leaf = findProps(tree, plain ? TextProps::staticTypeId() : AttributedTextProps::staticTypeId());
      LOKA_VERIFY(leaf);
      const String text = plain ? leaf->asTextNode()->props.text_->get()
                                : leaf->asAttributedTextNode()->props.text_->get().segment(0).text;
      LOKA_VERIFY(text.equals(String(expected)));
    }

    template <class D> class ParkOwner : public BoundaryNodeFor<ParkOwner<D> >
    {
    public:
      NodeState<bool> visible;
      explicit ParkOwner(const BoundaryPropsFor<ParkOwner> &p)
          : BoundaryNodeFor<ParkOwner>(p)
      {
        this->state(this->visible, true);
      }
      virtual void composeNode(NodeComposition &c)
      {
        c.declare(Show(*this->visible.state()) << constant<D>("old"));
      }
      void changeParkedDeclaration()
      {
        // Show will reconcile its stored arm declaration on reentry.
        IBranchSeatDefinition *seat = this->composition().root()->asBranchSeatDefinition();
        NodeDefinitionBase *content = seat->armDefinition(1)->asNestableDefinition()->childrenHead();
        typedef typename D::CloneType Stored;
        LOKA_VERIFY(content->propsBase()->propsTypeId() == D::PropsType::staticTypeId());
        static_cast<Stored *>(content)->props = constant<D>("parked").props;
      }
    };

    template <class D> struct DirectAndParked
    {
      int kind;
      void operator()(Scene &scene) const
      {
        ParkOwner<D> *owner = static_cast<ParkOwner<D> *>(SceneTestAccess::rootBoundary(scene));
        Node *node = findProps(owner, D::PropsType::staticTypeId());
        const uint32_t id = token(this->kind);
        LOKA_VERIFY(node && id && resources[id].text == "old");
        LOKA_VERIFY(constant<D>("direct").applyPropsToNode(node));
        scene.flushInvalidation();
        checkLeaf(node, this->kind, "direct");
        LOKA_VERIFY(!resources[id].hidden && resources[id].text == "old");
        owner->visible.set(false);
        scene.flushInvalidation();
        LOKA_VERIFY(node->lifecycleFact() == NODE_FACT_DETACHED_RETAINED);
        owner->changeParkedDeclaration();
        owner->visible.set(true);
        scene.flushInvalidation();
        LOKA_VERIFY(findProps(owner, D::PropsType::staticTypeId()) == node);
        checkLeaf(node, this->kind, "parked");
        const uint32_t next = token(this->kind);
        LOKA_VERIFY(next && !resources[next].hidden);
        LOKA_VERIFY(resources[next].text == "parked");
      }
    };
    template <class P> struct ListEdit
    {
      ObservableList<P> &list;
      int kind;
      void operator()(Scene &scene) const
      {
        BoundaryNode *root = SceneTestAccess::rootBoundary(scene);
        Node *item = findProps(root, P::staticTypeId());
        const uint32_t id = token(this->kind);
        LOKA_VERIFY(id && item && resources[id].text == "old");
        LOKA_VERIFY(this->list.update(this->list.at(0).id, P("edited")) == EDIT_OK);
        scene.flushInvalidation();
        LOKA_VERIFY(findProps(root, P::staticTypeId()) == item && token(this->kind) == id);
        checkLeaf(item, this->kind, "edited");
        LOKA_VERIFY(!resources[id].hidden && resources[id].text == "edited");
      }
    };

    template <class D, class P, class Leaf> void updatePaths(int kind)
    {
      DirectAndParked<D> arm = {kind};
      mounted(Boundary<ParkOwner<D> >(), arm);

      PushStateTracker tracker;
      ObservableList<P> list;
      LOKA_VERIFY(list.attach(&tracker, 1) == ATTACH_OK && list.insert(0, P("old")) == EDIT_OK);
      MutableState<Frame> viewport(Frame(0, 0, 180, 100));
      typedef reservation::Nodes<typename P::NodeType, 1, reservation::Nodes<Leaf, 1> > Nodes;
      typedef LazyGrid<P> Grid;
      Grid grid = LazyColumn(list, reservation::SeatNodes<Nodes>(), 1).cells(180, 40).viewport(viewport);
      ListEdit<P> edit = {list, kind};
      mounted(Boundary<PropsReconciliationSupport::Tree<Grid> >(PropsReconciliationSupport::Props<Grid>(&grid)), edit);
    }
  } // namespace
  void documentUpdatePaths()
  {
    updatePaths<doc::ParagraphDefinition, doc::ParagraphProps, AttributedTextNode>(loka::web::ATTRIBUTED_TEXT);
    updatePaths<Text, PlainProps<Text>, TextNode>(loka::web::TEXT);
    updatePaths<AttributedText, PlainProps<AttributedText>, AttributedTextNode>(loka::web::ATTRIBUTED_TEXT);
  }
} // namespace webtest
