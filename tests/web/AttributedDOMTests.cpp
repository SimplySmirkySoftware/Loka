#include "AttributedFixture.hpp"
#include "DocumentFixture.hpp"
#include "WebSession.hpp"
#include "WebScenePlatformController.hpp"
#include "WebAttributedTextContext.hpp"
#include "WebRunsStatus.hpp"
#include "FakeBoundary.hpp"
#include "support/TestVerify.hpp"
#include "support/LokaAllocFailure.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <cstdio>
using namespace loka::app;
using namespace loka::app::scene;
using loka::dsl::testing::SceneTestAccess;
namespace
{
  Node *FindAttributed(Node *node)
  {
    if (node->asAttributedTextNode())
      return node;
    if (INestable *nestable = node->asNestable())
      for (Node *child = nestable->childrenHead(); child; child = child->nextInComposition)
        if (Node *found = FindAttributed(child))
          return found;
    return 0;
  }
  AttributedString InvalidValue()
  {
    const loka::core::String input("refused");
    loka::core::testing::failLokaAllocRaw("AttributedString", "Segments", 1);
    const AttributedString invalid = Styled(input, Bold);
    loka::core::testing::allowLokaAllocRaw();
    LOKA_VERIFY(!invalid.valid());
    return invalid;
  }
  /** Mounted first-value refusal without an earlier successful snapshot. */
  class InvalidFixture : public BoundaryNodeFor<InvalidFixture>
  {
  public:
    explicit InvalidFixture(const BoundaryPropsFor<InvalidFixture> &props)
        : BoundaryNodeFor<InvalidFixture>(props)
    {
    }
    virtual void composeNode(NodeComposition &composition)
    {
      composition.declare(VStack().gap(0) << AttributedText(InvalidValue()) << Text("after"));
    }
  };
  class UnreadableBody : public loka::platform::String
  {
  public:
    virtual bool appendUtf8(std::string &) const
    {
      return false;
    }
  };
  void MountedInvalidSource()
  {
    for (int mode = 0; mode < 3; ++mode)
    {
      webtest::reset();
      webtest::attributedMode() = mode == 2 ? 1 : 0;
      WebSession session;
      WebElement root;
      int width = 100, height = 100;
      LOKA_VERIFY(root.create(session.routes(), session.tokens(), loka::web::ROOT, 0, width, height));
      {
        WebScenePlatformController controller(session, root.token(), width, height, 0);
        NodeDefinitionBase *definition =
            mode == 1 ? static_cast<NodeDefinitionBase *>(Boundary<InvalidFixture>().clone())
                      : static_cast<NodeDefinitionBase *>(Boundary<webtest::AttributedFixture>().clone());
        LOKA_VERIFY(definition);
        Scene scene(definition);
        LOKA_VERIFY(scene.mount(&controller));
        SceneTestAccess::updateAttached(scene, true);
        scene.flushInvalidation();
        Node *boundary = SceneTestAccess::rootBoundary(scene);
        Node *node = FindAttributed(boundary);
        const uint32_t token = webtest::token(loka::web::ATTRIBUTED_TEXT);
        LOKA_VERIFY(token && node);
        webtest::Resource &row = webtest::resources[token];
        unsigned calls = webtest::runsCalls;
        unsigned measures = webtest::measureCalls;
        if (mode != 1)
        {
          if (mode == 2)
          {
            loka::core::Operation turn;
            static_cast<webtest::AttributedFixture *>(boundary)->content.set(Styled("before", Red));
          }
          else
            LOKA_VERIFY(AttributedText(Styled("before", Red)).applyPropsToNode(node));
          controller.requestRelayout();
          scene.flushInvalidation();
          LOKA_VERIFY(row.projected && row.text == "before");
          if (mode == 2)
          {
            loka::core::Operation turn;
            static_cast<webtest::AttributedFixture *>(boundary)->shown.set(false);
          }
          scene.flushInvalidation();
          if (mode == 2)
          {
            LOKA_VERIFY(row.hidden);
            loka::core::Operation turn;
            static_cast<webtest::AttributedFixture *>(boundary)->content.set(InvalidValue());
          }
          else
            LOKA_VERIFY(AttributedText(InvalidValue()).applyPropsToNode(node));
          calls = webtest::runsCalls;
          measures = webtest::measureCalls;
          if (mode == 2)
          {
            loka::core::Operation turn;
            static_cast<webtest::AttributedFixture *>(boundary)->shown.set(true);
          }
          controller.requestRelayout();
          scene.flushInvalidation();
        }
        std::printf("attributed invalid mode=%d projected=%d height=%d runs=%u pending=%d\n",
                    mode,
                    row.projected,
                    row.height,
                    webtest::runsCalls - calls,
                    scene.hasPendingInvalidation());
        LOKA_VERIFY(!row.projected && row.height == 0);
        LOKA_VERIFY(webtest::runsCalls == calls && webtest::measureCalls == measures);
        LOKA_VERIFY(!scene.hasPendingInvalidation());
        if (mode != 2)
          LOKA_VERIFY(webtest::resources[webtest::token(loka::web::TEXT)].y == 0);
        if (mode == 1)
          LOKA_VERIFY(webtest::runsCalls == 0);
        if (mode == 2)
          LOKA_VERIFY(webtest::token(loka::web::ATTRIBUTED_TEXT) == token && !row.hidden);
        LOKA_VERIFY(AttributedText(Styled("after invalid", Blue)).applyPropsToNode(node));
        controller.requestRelayout();
        scene.flushInvalidation();
        LOKA_VERIFY(row.projected && row.text == "after invalid" && webtest::runsCalls == calls + 1);
        LOKA_VERIFY(!scene.hasPendingInvalidation());
        if (mode == 0)
        {
          const loka::core::String unreadable =
              loka::core::String::FromPlatform(loka::core::Managed<loka::platform::String>::Wrap(new UnreadableBody()));
          const AttributedString unreadableValue = Styled(unreadable, Bold);
          LOKA_VERIFY(unreadableValue.valid());
          LOKA_VERIFY(AttributedText(unreadableValue).applyPropsToNode(node));
          controller.requestRelayout();
          scene.flushInvalidation();
          LOKA_VERIFY(row.projected && row.text == "after invalid" && scene.hasPendingInvalidation());
          LOKA_VERIFY(AttributedText(Styled("readable again", Blue)).applyPropsToNode(node));
          controller.requestRelayout();
          scene.flushInvalidation();
          LOKA_VERIFY(row.projected && row.text == "readable again" && !scene.hasPendingInvalidation());
        }
        SceneTestAccess::unmount(scene);
        controller.destroy();
      }
      WebResource *retired = root.revoke(session.routes());
      LOKA_VERIFY(web_release(retired->token) == loka::web::CONSUMED);
      delete retired;
    }
    webtest::attributedMode() = 0;
  }
  void MountedLifecycleAndBottom()
  {
    for (int mode = 1; mode <= 4; ++mode)
    {
      webtest::reset();
      webtest::attributedMode() = mode;
      WebSession session;
      WebElement root;
      int width = 100, height = 100;
      LOKA_VERIFY(root.create(session.routes(), session.tokens(), loka::web::ROOT, 0, width, height));
      {
        WebScenePlatformController controller(session, root.token(), width, height, 0);
        Scene scene((Boundary<webtest::AttributedFixture>()));
        if (mode == 1)
          webtest::failMeasure = -1;
        LOKA_VERIFY(scene.mount(&controller));
        SceneTestAccess::updateAttached(scene, true);
        scene.flushInvalidation();
        webtest::AttributedFixture *boundary =
            static_cast<webtest::AttributedFixture *>(SceneTestAccess::rootBoundary(scene));
        uint32_t token = webtest::token(loka::web::ATTRIBUTED_TEXT);
        LOKA_VERIFY(token);
        if (mode == 1)
        {
          LOKA_VERIFY(!webtest::resources[token].projected && webtest::runsCalls == 0);
          LOKA_VERIFY(Node::LayoutInputsCheckpoint(*FindAttributed(boundary)).inputs() != NODE_DIRTY_NONE);
          webtest::failMeasure = 0;
          scene.flushInvalidation();
          LOKA_VERIFY(webtest::resources[token].text == "redblue");
        }
        if (mode <= 2)
        {
          for (int change = 0; change < 2; ++change)
          {
            const unsigned measures = webtest::measureCalls;
            {
              loka::core::Operation turn;
              boundary->shown.set(false);
            }
            scene.flushInvalidation();
            controller.drainNativeRetirements();
            if (mode == 1)
              LOKA_VERIFY(webtest::resources[token].hidden);
            else
              LOKA_VERIFY(webtest::resources.find(token) == webtest::resources.end());
            {
              loka::core::Operation turn;
              if (change)
                boundary->content.set(Styled("parked change", Blue));
              boundary->shown.set(true);
            }
            scene.flushInvalidation();
            const uint32_t next = webtest::token(loka::web::ATTRIBUTED_TEXT);
            LOKA_VERIFY(next && (mode == 1 ? next == token : next != token));
            LOKA_VERIFY(webtest::resources[next].text == (change ? "parked change" : "redblue"));
            if (mode == 1 && !change)
              LOKA_VERIFY(webtest::measureCalls == measures);
            token = next;
          }
        }
        else
        {
          const webtest::Resource &row = webtest::resources[token];
          const bool fits = mode == 3 || mode == 5;
          std::printf("attributed bottom mode=%d y=%d h=%d pending=%d\n",
                      mode,
                      row.y,
                      row.height,
                      scene.hasPendingInvalidation());
          LOKA_VERIFY(row.height == (fits ? 13 : 0));
          LOKA_VERIFY(!scene.hasPendingInvalidation());
          if (mode == 3 || mode == 4)
            LOKA_VERIFY(webtest::resources[webtest::token(loka::web::TEXT)].y == (fits ? 32767 : 32755));
          controller.requestRelayout();
          scene.flushInvalidation();
          LOKA_VERIFY(!scene.hasPendingInvalidation() && row.height == (fits ? 13 : 0));
          if (mode >= 5)
            LOKA_VERIFY(webtest::resources[webtest::token(loka::web::SCROLL)].extent == 0);
        }
        LOKA_VERIFY(boundary->activated.get() == LinkTarget(loka::core::String::Literal("sentinel")));
        SceneTestAccess::unmount(scene);
        controller.destroy();
      }
      WebResource *retired = root.revoke(session.routes());
      LOKA_VERIFY(web_release(retired->token) == loka::web::CONSUMED);
      delete retired;
    }
    for (int mode = 5; mode <= 6; ++mode)
    {
      webtest::reset();
      webtest::attributedMode() = mode;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateAttributed, 0) == WebSession::STARTED);
      session.wake(loka::web::IMMEDIATE, 0);
      const uint32_t token = webtest::token(loka::web::ATTRIBUTED_TEXT);
      LOKA_VERIFY(token);
      const webtest::Resource &row = webtest::resources[token];
      LOKA_VERIFY(row.height == (mode == 5 ? 13 : 0));
      LOKA_VERIFY((webtest::token(loka::web::TEXT) != 0) == (mode == 5));
      LOKA_VERIFY(webtest::resources[webtest::token(loka::web::SCROLL)].extent == 0);
      LOKA_VERIFY(!webtest::task(loka::web::IMMEDIATE));
      session.resize(webtest::token(loka::web::ROOT), 180, 120, 0);
      LOKA_VERIFY(row.height == (mode == 5 ? 13 : 0));
      LOKA_VERIFY(!webtest::task(loka::web::IMMEDIATE));
      session.dispose();
      webtest::fire(webtest::task(loka::web::SHUTDOWN), 1000);
      LOKA_VERIFY(webtest::resources.empty());
    }
    for (int mode = 7; mode <= 8; ++mode)
    {
      webtest::reset();
      webtest::attributedMode() = mode;
      WebSession session;
      LOKA_VERIFY(session.start(&webtest::CreateAttributed, 0) == WebSession::STARTED);
      {
        loka::core::Operation turn;
        webtest::attributedFixture()->content.set(Styled("a\na\na\na\na\na\na\na", Red));
      }
      session.wake(loka::web::IMMEDIATE, 0);
      const webtest::Resource &row = webtest::resources[webtest::token(loka::web::ATTRIBUTED_TEXT)];
      const webtest::Resource &sibling = webtest::resources[webtest::token(loka::web::TEXT)];
      std::printf("attributed container mode=%d y=%d h=%d sibling=%d\n", mode, row.y, row.height, sibling.y);
      LOKA_VERIFY(row.height == 104);
      if (mode == 7)
        LOKA_VERIFY(row.y + row.height > sibling.y);
      else
        LOKA_VERIFY(row.y == sibling.y && row.height > sibling.height);
      session.dispose();
      webtest::fire(webtest::task(loka::web::SHUTDOWN), 1000);
    }
    webtest::attributedMode() = 0;
  }
} // namespace

void WebDocumentVocabularyFixture()
{
  webtest::reset();
  WebSession session;
  LOKA_VERIFY(session.start(&webtest::CreateDocument, 0) == WebSession::STARTED);
  session.wake(loka::web::IMMEDIATE, 0);
  std::vector<webtest::Resource> leaves;
  for (std::map<uint32_t, webtest::Resource>::const_iterator it = webtest::resources.begin();
       it != webtest::resources.end();
       ++it)
    if (it->second.kind == loka::web::ATTRIBUTED_TEXT)
      leaves.push_back(it->second);
  LOKA_VERIFY(leaves.size() == 4);
  // One window, four text projections and the code block's four frame strips.
  LOKA_VERIFY(webtest::resources.size() == 9);
  const int x[] = {0, 0, 3, 0}, y[] = {0, 16, 35, 83};
  const int widths[] = {240, 240, 231, 240}, heights[] = {13, 13, 39, 13};
  const char *texts[] = {"Title codelink", "paragraph", "firstsecond", "after"};
  for (unsigned i = 0; i < leaves.size(); ++i)
  {
    LOKA_VERIFY(leaves[i].x == x[i] && leaves[i].y == y[i]);
    LOKA_VERIFY(leaves[i].width == widths[i] && leaves[i].height == heights[i]);
    LOKA_VERIFY(leaves[i].projected && leaves[i].text == texts[i]);
  }
  session.dispose();
  webtest::fire(webtest::task(loka::web::SHUTDOWN), 1000);
  LOKA_VERIFY(webtest::resources.empty());
}

namespace webtest
{
  void documentUpdatePaths();
}
void WebAttributedDOMTests()
{
  WebDocumentVocabularyFixture();
  webtest::documentUpdatePaths();
  MountedInvalidSource();
  MountedLifecycleAndBottom();
  webtest::reset();
  WebSession session;
  WebElement root;
  int width = 100, height = 100;
  LOKA_VERIFY(root.create(session.routes(), session.tokens(), loka::web::ROOT, 0, width, height));
  {
    WebScenePlatformController controller(session, root.token(), width, height, 0);
    Scene scene((Boundary<webtest::AttributedFixture>()));
    LOKA_VERIFY(scene.mount(&controller));
    SceneTestAccess::updateAttached(scene, true);
    scene.flushInvalidation();
    webtest::AttributedFixture *boundary =
        static_cast<webtest::AttributedFixture *>(SceneTestAccess::rootBoundary(scene));
    Node *column = boundary->asNestable()->childrenHead();
    Node *node = column->asNestable()->childrenHead();
    const uint32_t token = webtest::token(loka::web::ATTRIBUTED_TEXT);
    LOKA_VERIFY(token && node->getContext());
    webtest::Resource &row = webtest::resources[token];
    std::printf("attributed mounted: height=%d sibling=%d fragments=%d bytes=%s\n",
                row.height,
                webtest::resources[webtest::token(loka::web::TEXT)].y,
                row.runs[1],
                row.text.c_str());
    LOKA_VERIFY(row.height == 13 && row.text == "redblue" && row.runs[1] == 2);
    LOKA_VERIFY(webtest::resources[webtest::token(loka::web::TEXT)].y == 13);
    const unsigned measures = webtest::measureCalls;
    controller.requestRelayout();
    scene.flushInvalidation();
    LOKA_VERIFY(webtest::measureCalls == measures);
    LOKA_VERIFY(AttributedText(boundary->other.state()).applyPropsToNode(node));
    controller.requestRelayout();
    scene.flushInvalidation();
    LOKA_VERIFY(webtest::measureCalls == measures);
    {
      loka::core::Operation turn;
      boundary->content.set(Styled("unbound", Red));
    }
    scene.flushInvalidation();
    LOKA_VERIFY(row.text == "redblue");
    LOKA_VERIFY(AttributedText(boundary->content.state()).applyPropsToNode(node));
    controller.requestRelayout();
    scene.flushInvalidation();
    LOKA_VERIFY(row.text == "unbound");
    {
      loka::core::Operation turn;
      boundary->content.set(boundary->other.get());
    }
    scene.flushInvalidation();
    webtest::runsStatus = WEB_RUNS_REFUSED;
    {
      loka::core::Operation turn;
      boundary->content.set(Styled("changed", Bold));
    }
    unsigned calls = webtest::runsCalls;
    scene.flushInvalidation();
    LOKA_VERIFY(webtest::runsCalls == calls + 1 && row.text == "redblue" && scene.hasPendingInvalidation());
    LOKA_VERIFY(Node::LayoutInputsCheckpoint(*node).inputs() == NODE_DIRTY_NONE);
    calls = webtest::runsCalls;
    const unsigned builtMeasures = webtest::measureCalls;
    scene.flushInvalidation();
    LOKA_VERIFY(webtest::runsCalls == calls + 1 && webtest::measureCalls == builtMeasures);
    webtest::runsStatus = WEB_RUNS_APPLIED;
    scene.flushInvalidation();
    LOKA_VERIFY(row.text == "changed" && !scene.hasPendingInvalidation());
#ifdef NDEBUG
    webtest::runsStatus = WEB_RUNS_PROTOCOL;
    controller.requestRelayout();
    scene.flushInvalidation();
    LOKA_VERIFY(!scene.hasPendingInvalidation() && row.text == "changed");
    webtest::runsStatus = WEB_RUNS_APPLIED;
#endif
    WebAttributedTextContext *context = static_cast<WebAttributedTextContext *>(node->getContext());
    LayoutState offered;
    offered.width = 100;
    offered.y = 32754;
    offered.height = 99;
    LOKA_VERIFY(context->layout(&controller, offered) == 32767 && offered.height == 13);
    const int oldY = row.y;
    for (unsigned repeat = 0; repeat < 2; ++repeat)
    {
      offered.y = 32755;
      offered.height = 99;
      LOKA_VERIFY(context->layout(&controller, offered) == 32755);
      context->render(&controller);
      LOKA_VERIFY(offered.height == 99 && row.y == oldY && !scene.hasPendingInvalidation());
    }
    LOKA_VERIFY(AttributedText(Styled("new\ncontent", Red)).applyPropsToNode(node));
    offered.y = 32755;
    offered.height = 99;
    LOKA_VERIFY(context->layout(&controller, offered) == 32755 && offered.height == 99);
    context->render(&controller);
    LOKA_VERIFY(row.y == oldY && row.height == 13 && row.text == "newcontent");
    offered.y = 0;
    LOKA_VERIFY(context->layout(&controller, offered) == 26 && row.y == 0 && row.height == 26);
    LOKA_VERIFY((AttributedText(Styled("abcdefghij", Red) + Styled("klmnopqrst", Blue))
                 + BlockStyle().wrap(TEXT_WRAP_CHAR).align(TEXT_ALIGN_RIGHT))
                    .applyPropsToNode(node));
    offered.width = 50;
    context->layout(&controller, offered);
    context->render(&controller);
    bool wrapped = false, aligned = false;
    for (int i = 0; i < row.runs[1]; ++i)
    {
      wrapped = wrapped || row.runs[3 + i * 7 + 2] > 0;
      aligned = aligned || row.runs[3 + i * 7 + 1] > 0;
    }
    LOKA_VERIFY(wrapped && aligned && row.height > 13);
    LOKA_VERIFY(AttributedText(boundary->content.state()).applyPropsToNode(node));
    {
      loka::core::Operation turn;
      boundary->content.set(AttributedString());
    }
    scene.flushInvalidation();
    LOKA_VERIFY(row.text.empty() && row.runs.size() == 3 && row.runs[1] == 0 && row.runs[2] == 0);
    {
      loka::core::Operation turn;
      boundary->content.set(Styled("\n", Bold));
    }
    scene.flushInvalidation();
    LOKA_VERIFY(row.text.empty() && row.runs.size() == 3 && row.height == 26);
    webtest::failMeasure = -1;
    {
      loka::core::Operation turn;
      boundary->content.set(Styled("refused", Red));
    }
    scene.flushInvalidation();
    LOKA_VERIFY(row.text.empty() && row.height == 26 && scene.hasPendingInvalidation());
    LOKA_VERIFY(Node::LayoutInputsCheckpoint(*node).inputs() != NODE_DIRTY_NONE);
    webtest::failMeasure = 0;
    scene.flushInvalidation();
    LOKA_VERIFY(row.text == "refused");
    SceneTestAccess::unmount(scene);
    controller.destroy();
  }
  WebResource *retired = root.revoke(session.routes());
  LOKA_VERIFY(web_release(retired->token) == loka::web::CONSUMED);
  delete retired;
  std::puts("attributed DOM: mounted routing, retry, empty and absolute bottom passed");
}
/** Cost: death-test process once; one mounted fixture and one refused render. */
void WebAttributedDOMProtocolDeath()
{
  webtest::reset();
  webtest::attributedMode() = 0;
  WebSession session;
  LOKA_VERIFY(session.start(&webtest::CreateAttributed, 0) == WebSession::STARTED);
  webtest::runsStatus = WEB_RUNS_PROTOCOL;
  session.resize(webtest::token(loka::web::ROOT), 180, 120, 0);
  session.dispose();
  webtest::fire(webtest::task(loka::web::SHUTDOWN), 1000);
}
