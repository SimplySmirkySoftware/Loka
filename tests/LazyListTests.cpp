#include "LazyListTests.hpp"
#include "../example/LazyList/src/MainNode.hpp"
#include "app/scene/Scene.hpp"
#include "platform/null/NullScenePlatformController.hpp"
#include "support/TestVerify.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstring>

namespace
{
  unsigned constructions[32768];
  using namespace loka::app;
  using namespace loka::app::scene;
  using namespace loka::core;

  Node *find(Node *node, const char *id, unsigned &skip)
  {
    if (!node)
      return 0;
    if (node->testId() == id)
    {
      if (!skip)
        return node;
      --skip;
    }
    INestable *nest = node->asNestable();
    for (Node *child = nest ? nest->childrenHead() : 0; child; child = child->nextInComposition)
    {
      Node *match = find(child, id, skip);
      if (match)
        return match;
    }
    return 0;
  }
  lazylist::CardNode *card(Node *node, short number)
  {
    if (!node)
      return 0;
    if (node->propsTypeId() == lazylist::CardProps::staticTypeId())
    {
      lazylist::CardNode *value = static_cast<lazylist::CardNode *>(node);
      if (value->props.number == number)
        return value;
    }
    INestable *nest = node->asNestable();
    for (Node *child = nest ? nest->childrenHead() : 0; child; child = child->nextInComposition)
    {
      lazylist::CardNode *found = card(child, number);
      if (found)
        return found;
    }
    return 0;
  }
  ScrollViewNode *scrollView(Node *node)
  {
    if (node->asScrollViewNode())
      return node->asScrollViewNode();
    INestable *children = node->asNestable();
    for (Node *child = children ? children->childrenHead() : 0; child; child = child->nextInComposition)
    {
      ScrollViewNode *found = scrollView(child);
      if (found)
        return found;
    }
    return 0;
  }
  void reapply(lazylist::CardNode *resident)
  {
    NodeDefinition<lazylist::CardProps, lazylist::CardNode> definition(resident->props);
    LOKA_VERIFY(definition.applyPropsToNode(resident));
  }
  void countWrite(void *context)
  {
    ++*static_cast<unsigned *>(context);
  }
  unsigned markers(Node *node)
  {
    if (!node)
      return 0;
    unsigned count = node->testId() == "LazyList.Marker" ? 1 : 0;
    INestable *nest = node->asNestable();
    for (Node *child = nest ? nest->childrenHead() : 0; child; child = child->nextInComposition)
      count += markers(child);
    return count;
  }
  struct Fixture
  {
    Fixture()
        : model(),
          platform(),
          scene(Boundary<lazylist::MainNode>(lazylist::MainProps(&this->model)))
    {
      std::memset(constructions, 0, sizeof(constructions));
      this->scene.mount(&this->platform);
      LayoutState bounds;
      bounds.width = lazylist::kWindowWidth;
      bounds.height = lazylist::kWindowHeight;
      this->platform.projectLayoutForTesting(this->root(), bounds);
      loka::dsl::testing::SceneTestAccess::updateAttached(this->scene, true);
      this->drain();
    }
    ~Fixture()
    {
      loka::dsl::testing::SceneTestAccess::unmount(this->scene);
      this->drain();
    }
    Node *root()
    {
      return loka::dsl::testing::SceneTestAccess::rootNode(this->scene);
    }
    Node *node(const char *id, unsigned skip = 0)
    {
      return find(this->root(), id, skip);
    }
    void drain()
    {
      this->scene.flushInvalidation();
      this->scene.flushInvalidation();
      this->platform.drainNativeRetirements();
      {
        const bool fact = !this->scene.hasPendingInvalidation();
        LOKA_VERIFY(fact);
      }
    }
    void click(const char *id, unsigned skip = 0)
    {
      Node *n = this->node(id, skip);
      LOKA_VERIFY(n && n->asButtonNode() && n->asButtonNode()->props.onClick_);
      n->asButtonNode()->props.onClick_->emit();
      this->drain();
    }
    void scrollTo(int y)
    {
      // Null emulates the rail's clamped offset fact, never an app request.
      const int extent = this->model.cards.size() * lazylist::kCellHeight - lazylist::kViewportHeight;
      const int maximum = extent > 0 ? extent : 0;
      ScrollViewNode *scroll = scrollView(this->root());
      LOKA_VERIFY(scroll != 0);
      scroll->publishOrigin(0, y < 0 ? 0 : (y > maximum ? maximum : y));
      this->drain();
    }
    void nextPage()
    {
      this->scrollTo(this->model.viewport().get().y + lazylist::kViewportHeight);
    }
    void prevPage()
    {
      this->scrollTo(this->model.viewport().get().y - lazylist::kViewportHeight);
    }
    String label(unsigned row)
    {
      Node *n = this->node("LazyList.Label", row);
      LOKA_VERIFY(n && n->asTextNode());
      return n->asTextNode()->props.text_->get();
    }
    void pageLabels(int first)
    {
      const unsigned index = static_cast<unsigned>(this->model.viewport().get().y / lazylist::kCellHeight);
      const unsigned near = index ? index - 1 : 0;
      const unsigned far = index + 9 < this->model.cards.size() ? index + 9 : this->model.cards.size();
      LOKA_VERIFY(markers(this->root()) == far - near);
      const bool ledgerFact = this->platform.ledger().size() == 4 + far - near;
      LOKA_VERIFY(ledgerFact);
      for (unsigned i = 0; i < far - near; ++i)
        LOKA_VERIFY(this->label(i).equals(String::Literal("Card ") + String::FromInt(first - (index ? 1 : 0) + i)));
    }
    lazylist::LazyListModel model;
    NullScenePlatformController platform;
    Scene scene;
  };
} // namespace

namespace lazylist
{
  namespace testing
  {
    void cardConstructed(short number)
    {
      LOKA_VERIFY(number >= 0);
      ++constructions[number];
    }
  } // namespace testing
} // namespace lazylist

void testLazyListPagesAndClamps()
{
  Fixture f;
  f.pageLabels(0);
  for (unsigned i = 0; i < 100; ++i)
    LOKA_VERIFY(constructions[i] == (i < 9 ? 1u : 0u));
  f.nextPage();
  f.pageLabels(8);
  LOKA_VERIFY(f.model.viewport().get().y == 256);
  f.prevPage();
  f.pageLabels(0);
  LOKA_VERIFY(constructions[0] == 2);
  f.prevPage();
  LOKA_VERIFY(constructions[0] == 2);
  for (unsigned i = 0; i < 20; ++i)
    f.nextPage();
  f.pageLabels(92);
  LOKA_VERIFY(f.model.viewport().get().y == 2944);
  LOKA_VERIFY(f.model.removeFirst() == EDIT_OK);
  f.drain();
  LOKA_VERIFY(f.model.viewport().get().y == 2912);
  f.pageLabels(92);
}

void testLazyListRenamesRetainVisibleStateAndReadHiddenValues()
{
  Fixture f;
  Node *marker = f.node("LazyList.Marker", 3);
  f.click("LazyList.Rename");
  LOKA_VERIFY(f.label(3).equals(String::Literal("Card 3 (1)")));
  LOKA_VERIFY(f.node("LazyList.Marker", 3) == marker);
  LOKA_VERIFY(constructions[3] == 1);
  f.click("LazyList.Marker", 3);
  LOKA_VERIFY(f.label(3).equals(String::Literal("* Card 3 (1)")));
  f.click("LazyList.Rename");
  LOKA_VERIFY(f.label(3).equals(String::Literal("* Card 3 (2)")));
  LOKA_VERIFY(f.node("LazyList.Marker", 3) == marker);
  LOKA_VERIFY(constructions[3] == 1);
  LOKA_VERIFY(f.model.renameCard(12) == EDIT_OK);
  f.drain();
  LOKA_VERIFY(constructions[12] == 0);
  f.nextPage();
  LOKA_VERIFY(f.label(5).equals(String::Literal("Card 12 (1)")));
  LOKA_VERIFY(constructions[12] == 1);
  f.prevPage();
  LOKA_VERIFY(f.label(3).equals(String::Literal("Card 3 (2)")));
  LOKA_VERIFY(constructions[3] == 2);
}

void testLazyListStructuralEditsReplaceGeneration()
{
  Fixture f;
  f.click("LazyList.Marker", 1);
  f.click("LazyList.Remove");
  f.pageLabels(1);
  LOKA_VERIFY(constructions[1] == 2);
  LOKA_VERIFY(constructions[8] == 2);
  f.click("LazyList.Marker");
  f.click("LazyList.Insert");
  LOKA_VERIFY(f.label(0).equals(String::Literal("Card 100")));
  LOKA_VERIFY(f.label(1).equals(String::Literal("Card 1")));
  LOKA_VERIFY(constructions[1] == 3);
  LOKA_VERIFY(constructions[100] == 1);
  f.click("LazyList.Marker", 1);
  f.click("LazyList.Move");
  f.pageLabels(1);
  LOKA_VERIFY(constructions[1] == 4);
  LOKA_VERIFY(f.model.cards.at(99).value.number == 100);
}

void testLazyListRefusalsAndUnmount()
{
  Fixture f;
  const ListRevision before = f.model.cards.revision().get();
  f.click("LazyList.Insert");
  LOKA_VERIFY(!(f.model.cards.revision().get() != before));
  LOKA_VERIFY(f.node("LazyList.Result")
                  ->asTextNode()
                  ->props.text_->get()
                  .equals(String::Literal("List full: remove a card before inserting")));
  LOKA_VERIFY(f.model.insertAtTop() == EDIT_CAPACITY_EXCEEDED);
  LOKA_VERIFY(f.model.renameCard(100) == EDIT_INDEX_OUT_OF_RANGE);
  loka::dsl::testing::SceneTestAccess::unmount(f.scene);
  f.drain();
  {
    const bool fact = f.platform.ledger().size() == 0;
    LOKA_VERIFY(fact);
  }
  LOKA_VERIFY(f.model.reportScrollOffset(256) == EDIT_OK);
  LOKA_VERIFY(f.model.renameCard(12) == EDIT_OK);
  LOKA_VERIFY(f.model.removeFirst() == EDIT_OK);
  {
    const bool fact = !f.scene.hasPendingInvalidation();
    LOKA_VERIFY(fact);
  }
  while (f.model.cards.size())
    LOKA_VERIFY(f.model.removeFirst() == EDIT_OK);
  LOKA_VERIFY(f.model.removeFirst() == EDIT_ID_NOT_FOUND);
  LOKA_VERIFY(f.model.moveFirstToEnd() == EDIT_ID_NOT_FOUND);
  LOKA_VERIFY(f.model.viewport().get().y == 0);
}

void testLazyListUnchangedBindingsStayQuiet()
{
  Fixture f;
  lazylist::CardNode *resident = card(f.root(), 3);
  LOKA_VERIFY(resident != 0);
  State<String> *label = f.node("LazyList.Label", 3)->asTextNode()->props.text_;
  unsigned writes = 0;
  label->bind(&countWrite, &writes, false);
  reapply(resident);
  f.drain();
  LOKA_VERIFY(writes == 0);
  f.click("LazyList.Rename");
  LOKA_VERIFY(writes == 1);
  reapply(resident);
  f.drain();
  LOKA_VERIFY(writes == 1);
  label->unbind(&countWrite, &writes);
}

void testLazyListNativeOffsetSelectsWindow()
{
  Fixture f;
  f.scrollTo(512);
  LOKA_VERIFY(f.model.viewport().get().y == 512);
  LOKA_VERIFY(card(f.root(), 0) == 0 && card(f.root(), 16) != 0);
  f.pageLabels(16);
}
