#include "WebTextLayout.hpp"
#include "WebSession.hpp"
#include "WebScenePlatformController.hpp"
#include "FakeBoundary.hpp"
#include "support/TestVerify.hpp"
#include "support/LifecycleFactTestAccess.hpp"
#include "app/nodes/Text.hpp"
#include "app/nodes/controls/Button.hpp"
#include "app/nodes/boundary/StdComposition.hpp"
#include "app/nodes/nestable/RowColumn.hpp"
#include "testing/scene/SceneTestFlow.hpp"
#include <climits>
#include <cstring>
#include <cstdio>
#include <new>
#include <sstream>
using namespace loka::app;
using namespace loka::app::scene;
namespace
{
  class Controller : public WebScenePlatformController
  {
  public:
    unsigned requests;
    Controller(WebSession &s, uint32_t root)
        : WebScenePlatformController(s, root, 320, 200),
          requests(0)
    {
    }
    virtual void requestRelayout()
    {
      ++this->requests;
    }
  };
  class Rig
  {
  public:
    WebSession session;
    WebElement root;
    Controller *controller;
    TextNode node;
    LayoutState state;
    Rig()
        : controller(0),
          node(Text("old").props)
    {
      webtest::reset();
      int width = 320, height = 200;
      LOKA_VERIFY(this->root.create(this->session.routes(), this->session.tokens(), loka::web::ROOT, 0, width, height));
      this->controller = new Controller(this->session, this->root.token());
      this->node.setPropsTypeId(TextProps::staticTypeId());
      this->state.width = 100;
      LOKA_VERIFY(this->controller->prepareProjectedLayout(&this->node, this->state));
    }
    ~Rig()
    {
      LifecycleFactTestAccess::MarkSubtreeRetired(&this->node);
      this->node.setContext(0);
      delete this->controller;
      WebResource *row = this->root.revoke(this->session.routes());
      LOKA_VERIFY(web_release(row->token) == loka::web::CONSUMED);
      delete row;
    }
    void visit()
    {
      WebElementContext *context = static_cast<WebElementContext *>(this->node.getContext());
      context->layout(this->controller, this->state);
      context->render(this->controller);
    }
    const std::string &text() const
    {
      return webtest::resources[webtest::token(loka::web::TEXT)].text;
    }
    void apply(const char *value, const TextStyle &style = TextStyle(), const BlockStyle &block = BlockStyle())
    {
      TextProps props(value);
      props.textStyle_ = style;
      props.blockStyle_ = block;
      LOKA_VERIFY(Text(props).applyPropsToNode(&this->node));
    }
  };

  class LiveText : public BoundaryNodeFor<LiveText>
  {
  public:
    NodeState<loka::core::String> first, second, sibling;
    explicit LiveText(const BoundaryPropsFor<LiveText> &p)
        : BoundaryNodeFor<LiveText>(p)
    {
      this->state(this->first, loka::core::String::Literal("live"));
      this->state(this->second, loka::core::String::Literal("live"));
      this->state(this->sibling, loka::core::String::Literal("sibling"));
    }
    virtual void composeNode(NodeComposition &c)
    {
      c.declare(VStack() << Text(this->first.state()) << Text(this->sibling.state()));
    }
  };
  void SceneRouting()
  {
    using loka::dsl::testing::SceneTestAccess;
    webtest::reset();
    WebSession session;
    WebElement root;
    int width = 320, height = 200;
    LOKA_VERIFY(root.create(session.routes(), session.tokens(), loka::web::ROOT, 0, width, height));
    {
      WebScenePlatformController controller(session, root.token(), width, height);
      Scene scene((Boundary<LiveText>()));
      LOKA_VERIFY(scene.mount(&controller));
      SceneTestAccess::updateAttached(scene, true);
      scene.flushInvalidation();
      LiveText *boundary = static_cast<LiveText *>(SceneTestAccess::rootBoundary(scene));
      Node *text = 0;
      // The scene's own declaration tree, inspected only by this test.
      Node *column = boundary->asNestable()->childrenHead();
      text = column->asNestable()->childrenHead();
      unsigned builds = webtest::textBuilds;
      LOKA_VERIFY(Text(boundary->second.state()).applyPropsToNode(text));
      boundary->markViewDirty(NODE_DIRTY_PROPS);
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds); // New handle, equal normalized value.
      {
        loka::core::Operation turn;
        boundary->first.set(loka::core::String::Literal("ignored"));
      }
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds);
      {
        loka::core::Operation turn;
        boundary->second.set(loka::core::String::Literal("changed"));
      }
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds + 1); // NONE still layouts through Web onChange.
      {
        loka::core::Operation turn;
        boundary->second.set(loka::core::String(std::string(2048, 'a')));
      }
      scene.flushInvalidation();
      builds = webtest::textBuilds;
      webtest::textScans = 0;
      {
        loka::core::Operation turn;
        boundary->sibling.set(loka::core::String::Literal("other"));
      }
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds + 1 && webtest::textScans == 2);
      // One comparison for the unchanged large text; sibling comparison misses and builds.
      builds = webtest::textBuilds;
      LOKA_VERIFY(Text(boundary->first.state()).applyPropsToNode(text));
      boundary->markViewDirty(NODE_DIRTY_PROPS);
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds + 1);
      LOKA_VERIFY(Text(boundary->second.state()).applyPropsToNode(text));
      boundary->markViewDirty(NODE_DIRTY_PROPS);
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds + 2);
      webtest::failMeasure = -1;
      {
        loka::core::Operation turn;
        boundary->second.set(loka::core::String::Literal("refused"));
      }
      builds = webtest::textBuilds;
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds + 1);
      builds = webtest::textBuilds;
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds + 1);
      webtest::failMeasure = 0;
      scene.flushInvalidation();
#ifdef NDEBUG
      webtest::measureStatus = WEB_MEASURE_PROTOCOL;
      webtest::failMeasure = 1;
      {
        loka::core::Operation turn;
        boundary->second.set(loka::core::String::Literal("protocol"));
      }
      builds = webtest::textBuilds;
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds + 1 && !scene.hasPendingInvalidation());
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds + 1);
      {
        loka::core::Operation turn;
        boundary->second.set(loka::core::String::Literal("recovery"));
      }
      scene.flushInvalidation();
      LOKA_VERIFY(webtest::textBuilds == builds + 2);
      webtest::measureStatus = WEB_MEASURE_REFUSED;
#endif
      SceneTestAccess::unmount(scene);
      controller.destroy();
    }
    WebResource *row = root.revoke(session.routes());
    LOKA_VERIFY(web_release(row->token) == loka::web::CONSUMED);
    delete row;
  }
  void Normalization()
  {
    const char *inputs[] = {"a\r\nb",
                            "a\rb",
                            "a\tb",
                            "a\fb",
                            "a\0b",
                            "\x80\x80"
                            "a",
                            "\xc0\x80",
                            "\xed\xa0\x80",
                            "\xe3\x81"
                            "a",
                            "\xe3\x81",
                            "a\xe3\x81\x82\xf0\x9f\x98\x80"
                            "b"};
    const unsigned sizes[] = {4, 3, 3, 3, 3, 3, 2, 3, 3, 2, 9};
    const char *expected[] = {"a\nb",
                              "a\nb",
                              "a b",
                              "a b",
                              "a\xef\xbf\xbd"
                              "b",
                              "\xef\xbf\xbd\xef\xbf\xbd"
                              "a",
                              "\xef\xbf\xbd\xef\xbf\xbd",
                              "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd",
                              "\xef\xbf\xbd"
                              "a",
                              "\xef\xbf\xbd",
                              "a\xe3\x81\x82\xf0\x9f\x98\x80"
                              "b"};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
      const loka::core::String value = loka::core::String::Utf8(inputs[i], sizes[i]);
      WebTextSnapshot *built = 0;
      LOKA_VERIFY(WebTextLayout::build(value, WebTextConstraint(16, BlockStyle(), 100), false, built)
                  == WEB_MEASURE_OK);
      WebTextSlot slot;
      slot.replace(built);
      LOKA_VERIFY(std::string(built->prepared(), built->preparedLength()) == expected[i]);
      bool equal = false;
      LOKA_VERIFY(built->matches(value, false, equal) == WEB_MEASURE_OK && equal);
    }
    const loka::core::String value = loka::core::String::Literal("a\xe3\x81\x82"
                                                                 "b");
    WebTextSnapshot *built = 0;
    LOKA_VERIFY(WebTextLayout::build(value, WebTextConstraint(16, BlockStyle().wrap(TEXT_WRAP_CHAR), 7), false, built)
                == WEB_MEASURE_OK);
    WebTextSlot slot;
    slot.replace(built);
    LOKA_VERIFY(std::string(built->joined(), built->joinedLength()) == "a\n\xe3\x81\x82\nb");
  }
  void ReuseAndKeepLast()
  {
    Rig rig;
    rig.visit();
    LOKA_VERIFY(rig.text() == "old" && rig.state.height == 13);
    const unsigned builds = webtest::textBuilds;
    webtest::calls.clear();
    rig.visit();
    LOKA_VERIFY(webtest::textBuilds == builds && webtest::textScans == 1);
    LOKA_VERIFY(webtest::calls.size() == 6);
    LOKA_VERIFY(webtest::calls[1].find("projected:") == 0 && webtest::calls[2].find("style:") == 0
                && webtest::calls[3].find("line-height:") == 0 && webtest::calls[4].find("block:") == 0
                && webtest::calls[5].find("text:") == 0);
    rig.state.width = 90;
    rig.visit();
    LOKA_VERIFY(webtest::textBuilds == builds + 1);
    rig.apply("old", Red);
    rig.visit();
    LOKA_VERIFY(webtest::textBuilds == builds + 1);
    rig.apply("old", Blue + Underline);
    rig.visit();
    LOKA_VERIFY(webtest::textBuilds == builds + 1);
    bool blueUnderline = false;
    for (unsigned i = 0; i < webtest::calls.size(); ++i)
      if (webtest::calls[i].find(",1,7") != std::string::npos)
        blueUnderline = true;
    LOKA_VERIFY(blueUnderline);
    rig.apply("new\ntext", FontSize<24>() + Red, BlockStyle().align(TEXT_ALIGN_RIGHT));
    webtest::failMeasure = 1;
    webtest::calls.clear();
    rig.visit();
    LOKA_VERIFY(rig.text() == "old" && rig.state.height == 13 && rig.controller->requests == 1);
    bool oldFont = false, oldBlock = false;
    for (unsigned i = 0; i < webtest::calls.size(); ++i)
    {
      if (webtest::calls[i].find("=2,9,1,12,") != std::string::npos)
        oldFont = true;
      if (webtest::calls[i].find("block:") == 0 && webtest::calls[i].find("=0,0") != std::string::npos)
        oldBlock = true;
    }
    LOKA_VERIFY(oldFont && oldBlock);
    rig.visit();
    LOKA_VERIFY(rig.text() == "new\ntext" && rig.state.height == 26);
    rig.apply("a\nb", TextStyle(), BlockStyle().wrap(TEXT_WRAP_CHAR).truncation(TEXT_TRUNCATION_ELLIPSIS));
    rig.visit();
    LOKA_VERIFY(rig.text() == "a" && rig.state.height == 13);
    rig.apply("a\nb");
    rig.visit();
    LOKA_VERIFY(rig.text() == "a\nb" && rig.state.height == 26);
  }
  void ButtonFailureExits()
  {
    Rig rig;
    ButtonNode button(Button("old").props);
    button.setPropsTypeId(ButtonProps::staticTypeId());
    LOKA_VERIFY(rig.controller->prepareProjectedLayout(&button, rig.state));
    WebElementContext *context = static_cast<WebElementContext *>(button.getContext());
    const uint32_t token = webtest::token(loka::web::BUTTON);
    std::ostringstream hidden, visible;
    hidden << "projected:" << token << "=0";
    visible << "projected:" << token << "=1";
    for (int phase = 0; phase < 4; ++phase)
    {
#ifndef NDEBUG
      if (phase == 3)
        break;
#endif
      if (phase >= 2)
        LOKA_VERIFY(Button("new").applyPropsToNode(&button));
      webtest::failMeasure = phase == 1 ? 0 : 1;
      webtest::measureStatus = phase == 3 ? WEB_MEASURE_PROTOCOL : WEB_MEASURE_REFUSED;
      webtest::calls.clear();
      context->layout(rig.controller, rig.state);
      context->render(rig.controller);
      const bool empty = phase == 0;
      LOKA_VERIFY(webtest::resources[token].projected == !empty);
      LOKA_VERIFY(webtest::resources[token].height == (empty ? 0 : 23));
      LOKA_VERIFY(webtest::resources[token].text == (empty ? "" : "old"));
      bool wroteHidden = false, wroteVisible = false;
      for (unsigned i = 0; i < webtest::calls.size(); ++i)
      {
        wroteHidden = wroteHidden || webtest::calls[i] == hidden.str();
        wroteVisible = wroteVisible || webtest::calls[i] == visible.str();
      }
      LOKA_VERIFY(wroteHidden == empty && wroteVisible == !empty);
    }
    webtest::measureStatus = WEB_MEASURE_REFUSED;
    LifecycleFactTestAccess::MarkSubtreeRetired(&button);
    button.setContext(0);
  }
  void FailureExits()
  {
    ButtonFailureExits();
    Rig rig;
    webtest::failMeasure = 1;
    rig.visit();
    LOKA_VERIFY(rig.text().empty() && rig.state.height == 0 && rig.controller->requests == 1);
    rig.visit();
    LOKA_VERIFY(rig.text() == "old");
    // Each injected service stage fails once, then identical input succeeds.
    for (int stage = 1; stage <= 4; ++stage)
    {
      rig.apply("abc");
      rig.visit();
      rig.apply("def");
      webtest::failMeasure = stage;
      const unsigned before = rig.controller->requests;
      rig.visit();
      LOKA_VERIFY(rig.text() == "abc" && rig.controller->requests == before + 1);
      rig.visit();
      LOKA_VERIFY(rig.text() == "def");
    }
#ifdef NDEBUG
    rig.apply("protocol");
    webtest::measureStatus = WEB_MEASURE_PROTOCOL;
    webtest::failMeasure = 1;
    const unsigned requests = rig.controller->requests;
    rig.visit();
    LOKA_VERIFY(rig.text() == "def" && rig.controller->requests == requests);
    rig.apply("recovered");
    rig.visit();
    LOKA_VERIFY(rig.text() == "recovered");
#else
    std::puts("[skip] measurement protocol keep-last: NDEBUG only; separate death invocation checks debug assert");
#endif
    webtest::measureStatus = WEB_MEASURE_REFUSED;
  }
  int failAllocation = 0;
  void *Allocate(std::size_t size, const loka::core::LokaAllocationSite &site)
  {
    if (std::strcmp(site.ownerTag, "WebTextSnapshot") == 0 && failAllocation > 0 && --failAllocation == 0)
      return 0;
    return ::operator new(size, std::nothrow);
  }
  void Free(void *p, const loka::core::LokaAllocationSite &)
  {
    ::operator delete(p);
  }
  void AllocationStages()
  {
    // Backend is installed only across allocation-balanced build scopes.
    const loka::core::String value = loka::core::String::Literal("abc");
    for (int stage = 1; stage <= 5; ++stage)
    {
      loka::core::LokaAllocSetBackend(&Allocate, &Free);
      {
        failAllocation = stage;
        WebTextSnapshot *built = 0;
        LOKA_VERIFY(WebTextLayout::build(value, WebTextConstraint(16, BlockStyle(), 100), false, built)
                    == WEB_MEASURE_REFUSED);
        LOKA_VERIFY(!built);
        LOKA_VERIFY(WebTextLayout::build(value, WebTextConstraint(16, BlockStyle(), 100), false, built)
                    == WEB_MEASURE_OK);
        WebTextSlot slot;
        slot.replace(built);
      }
      loka::core::LokaAllocSetBackend(0, 0);
    }
  }
  void NaturalAndMemo()
  {
    const int points[] = {-32768, 0, 9, 11, 16, 21, 24, 32767};
    const int keys[] = {0, 0, 0, 8, 24, 32, 40, 40};
    for (unsigned i = 0; i < sizeof(points) / sizeof(points[0]); ++i)
      LOKA_VERIFY(WebFontKey(TextStyle(TextSizeValue::points(points[i]))) == keys[i]);
    LOKA_VERIFY(WebFontKey(SmallLabel) == 8);
    LOKA_VERIFY(WebFontKey(TextStyle()) == 16 && WebFontKey(Red + Underline) == 16);
    LOKA_VERIFY(WebFontKey(FontSize<24>() + Bold + Italic + Monospace) == 47);
    Rig rig;
    short out = 123;
    rig.apply("");
    LOKA_VERIFY(!rig.controller->queryNaturalWidth(&rig.node, out) && out == 123);
    rig.apply("a\nb");
    LOKA_VERIFY(!rig.controller->queryNaturalWidth(&rig.node, out) && out == 123);
    LOKA_VERIFY(!rig.controller->queryNaturalHeight(&rig.node, out) && out == 123);
    rig.apply("a", TextStyle(), BlockStyle().wrap(TEXT_WRAP_CHAR));
    LOKA_VERIFY(!rig.controller->queryNaturalHeight(&rig.node, out) && out == 123);
    rig.apply("a");
    webtest::measuredWidth = SHRT_MAX + 1;
    LOKA_VERIFY(!rig.controller->queryNaturalWidth(&rig.node, out) && out == 123);
    webtest::measuredWidth = SHRT_MAX - 1;
    LOKA_VERIFY(rig.controller->queryNaturalWidth(&rig.node, out) && out == SHRT_MAX - 1);
    webtest::measuredWidth = SHRT_MAX;
    LOKA_VERIFY(rig.controller->queryNaturalWidth(&rig.node, out) && out == SHRT_MAX);
    webtest::measuredWidth = -1;
    const unsigned builds = webtest::textBuilds, resources = webtest::resources.size();
    const uint32_t tokenBefore = rig.session.tokens().take();
    LOKA_VERIFY(rig.controller->queryNaturalWidth(&rig.node, out) && out == 7);
    LOKA_VERIFY(rig.controller->queryNaturalWidth(&rig.node, out) && out == 7);
    LOKA_VERIFY(webtest::textBuilds == builds && webtest::resources.size() == resources && !rig.controller->requests);
    LOKA_VERIFY(rig.session.tokens().take() == tokenBefore + 1);
    const char *labels[] = {"", "abc", "a\nb"};
    for (unsigned i = 0; i < 3; ++i)
    {
      ButtonNode button(Button(labels[i]).props);
      LOKA_VERIFY(rig.controller->queryNaturalWidth(&button, out)
                  && out == static_cast<int>(std::strlen(labels[i]) * 7 + 18));
      LOKA_VERIFY(rig.controller->queryNaturalHeight(&button, out) && out == 23);
      webtest::measuredWidth = SHRT_MAX - 17;
      out = 123;
      LOKA_VERIFY(!rig.controller->queryNaturalWidth(&button, out) && out == 123);
      webtest::measuredWidth = -1;
      WebTextSnapshot *label = 0;
      LOKA_VERIFY(WebTextLayout::build(loka::core::String::Literal(labels[i]),
                                       WebTextConstraint(16, BlockStyle().wrap(TEXT_WRAP_WORD), 7),
                                       true,
                                       label)
                  == WEB_MEASURE_OK);
      WebTextSlot labelSlot;
      labelSlot.replace(label);
      LOKA_VERIFY(std::string(label->joined(), label->joinedLength()) == (i == 2 ? "a b" : labels[i]));
      LOKA_VERIFY(label->extent().height == 13);
    }
    webtest::measureCalls = 0;
    WebTextSnapshot *built = 0;
    LOKA_VERIFY(
        WebTextLayout::build(loka::core::String::Literal("abc"), WebTextConstraint(16, BlockStyle(), 100), false, built)
        == WEB_MEASURE_OK);
    WebTextSlot slot;
    slot.replace(built);
    LOKA_VERIFY(webtest::measureCalls == 3); // Count/fill and final fragment all share range memo.
  }
} // namespace
void WebTextProjectionTests()
{
  webtest::reset();
  Normalization();
  ReuseAndKeepLast();
  FailureExits();
  AllocationStages();
  NaturalAndMemo();
  SceneRouting();
  std::puts("6 Web text projection groups passed");
}
void WebTextProtocolDeath()
{
  Rig rig;
  webtest::measureStatus = WEB_MEASURE_PROTOCOL;
  webtest::failMeasure = 1;
  rig.visit();
}
